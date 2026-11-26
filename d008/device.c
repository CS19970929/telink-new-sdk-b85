#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "device.h"
#include "link.h"
#include "afe.h"
#include "history.h"
#include "dvc_bus.h"
#include <string.h>

Device device;
static enum { STOPPED, SETUP, SAMPLING, QUIET, SHUTDOWN } mode;
static bool failing, probing, wire_settling;
static uint32_t fail_ms, quiet_ms, probe_ms, normal_low_ms, low_ms, uv_ms;
static uint32_t service_ms;
static uint32_t last_good_ms;
static bool previous_good;
typedef struct { int64_t uas; uint16_t capacity, percent; } SavedSoc;

static void begin_setup(void)
{
    mode=SETUP; device.valid=false; previous_good=false; probing=wire_settling=false;
    gpio_write(GPIO_PA1,0);
    control_gap(&device.control);
    afe_begin(&device.config);
}

void device_init(void)
{
    Device *d=&device;
    d->allowed=3;
    Config assembly;
    config_defaults(&assembly,D008_CELLS,D008_NMC);
#if D008_COMM_TEST
    d->config=assembly; d->configured=config_valid(&assembly); d->allowed=0;
#else
    StoreResult cfg=store_open_config(&d->config,&assembly,sizeof(assembly));
    if (cfg==STORE_BLANK || cfg==STORE_INITIAL) {
        d->config=assembly;
        d->configured=config_valid(&assembly) && store_append(CONFIG_DATA,&d->config,sizeof(d->config));
    } else {
        d->configured=cfg==STORE_VALID && config_valid(&d->config) &&
            d->config.cells==assembly.cells && d->config.nmc==assembly.nmc;
    }
    if (!d->configured) d->config=assembly;
    SavedSoc saved;
    StoreResult state=store_open(SOC_DATA,&saved,sizeof(saved));
    if (state==STORE_VALID && d->configured && saved.capacity==d->config.capacity_dah &&
        saved.percent<=100 && saved.uas>=0 && saved.uas<=(int64_t)saved.capacity*360000000) {
        d->soc.initialized=true;
        d->soc.state.remaining_uas=saved.uas; d->soc.state.percent=saved.percent;
    }
    StoreResult log=store_open(EVENT_DATA,0,2);
    d->storage_fault=!d->configured || state==STORE_BAD || log==STORE_BAD;
    history_note(1,0);
#endif
    dvc_bus_init(); gpio_write(GPIO_PD7,1);
    if (d->configured) begin_setup();
    else {mode=STOPPED; (void)afe_outputs(0,0,0);}
}

bool device_save_soc(void)
{
    if (D008_COMM_TEST || !device.soc.initialized || !device.configured) return false;
    SavedSoc s={device.soc.state.remaining_uas,device.config.capacity_dah,device.soc.state.percent};
    return store_append(SOC_DATA,&s,sizeof(s));
}

bool device_set_soc(unsigned percent,uint32_t ms)
{
    if (percent>100 || !device.configured) return false;
    Soc candidate=device.soc;
    soc_manual(&candidate,&device.config,percent,ms);
    SavedSoc s={candidate.state.remaining_uas,device.config.capacity_dah,candidate.state.percent};
    if (!store_append(SOC_DATA,&s,sizeof(s))) return false;
    device.soc=candidate;
    return true;
}

static void silence(uint32_t ms)
{
    /* 随后完全停止 I²C 访问，连诊断和读请求也不得喂到硬件 WDT。 */
    (void)afe_outputs(0,0,ms);
    gpio_write(GPIO_PA1,0);
    device.valid=false; device.afe_fault=true;
    previous_good=false; control_gap(&device.control);
    mode=QUIET; quiet_ms=ms; failing=false;
}

bool device_busy(void) {return mode==SETUP || mode==SHUTDOWN;}

void device_service(uint32_t ms)
{
    if (mode==SETUP) {
        AfeResult result=afe_setup_step();
        if (result==AFE_ERROR) silence(ms);
        else if (result==AFE_OK) {mode=SAMPLING; failing=false;}
    } else if (mode==QUIET && ms-quiet_ms>=5000) {
        begin_setup();
    }
    service_ms=ms;
    if (!D008_COMM_TEST && !update_active() && !device_busy() && !history_flush(ms))
        device.storage_fault=true;
}

bool device_set_config(const Config *c)
{
    if (!device.configured || mode!=SAMPLING || !device.valid || update_active() ||
        c->cells!=device.config.cells || c->nmc!=device.config.nmc || !config_valid(c)) return false;
    if (!memcmp(c,&device.config,sizeof(*c))) return true;
    bool hardware=memcmp(&c->hw_ov_mv,&device.config.hw_ov_mv,
               (const uint8_t *)&c->balance_on-(const uint8_t *)&c->hw_ov_mv)!=0;
    if (hardware) {
        gpio_write(GPIO_PA1,0);
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        afe_begin(c);
        AfeResult result=AFE_WAIT;
        for (unsigned i=0;i<40 && result==AFE_WAIT;++i) {
            result=afe_setup_step();
            blt_sdk_main_loop();
        }
        if (result!=AFE_OK) {begin_setup();return false;}
    }
    if (!store_append(CONFIG_DATA,c,sizeof(*c))) {
        if (!store_deferred) device.storage_fault=true;
        if (hardware) begin_setup();
        return false;
    }
    if (c->capacity_dah!=device.config.capacity_dah && device.soc.initialized)
        soc_manual(&device.soc,c,device.soc.state.percent,last_good_ms);
    device.config=*c;
    if (hardware) {device.valid=false; previous_good=false;}
    return true;
}

void device_powerdown(bool acc)
{
    if (mode==SHUTDOWN) return;
    bool bus_allowed=mode!=QUIET;
    wired_stop();
    mode=SHUTDOWN;
    gpio_write(GPIO_PA1,0);
    if (bus_allowed) {(void)afe_outputs(0,0,0); (void)afe_shutdown();}
    if (!D008_COMM_TEST) {
        (void)history_note(2,service_ms);
        (void)history_flush(service_ms);
    }
    (void)device_save_soc();
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    bls_pm_setAppWakeupLowPower(0,0);
    if (blc_ll_getCurrentState()==BLS_LINK_STATE_CONN)
        bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN);
    bls_ll_setAdvEnable(BLC_ADV_DISABLE);
    if (acc) {
        if (!gpio_read(GPIO_PA0)) start_reboot();
        cpu_set_gpio_wakeup(GPIO_PA0,Level_Low,1);
        cpu_sleep_wakeup(DEEPSLEEP_MODE,PM_WAKEUP_PAD,0);
        start_reboot();
    } else gpio_write(GPIO_PC4,0);
}

void device_measure(uint32_t tick,uint32_t ms)
{
    Device *d=&device;
    if (mode==SHUTDOWN) return;
    /* PA0 是此装配的预留 ACC，不作为关机请求。 */
    bool sampled=false;
    if (mode==SAMPLING) {
        Sample next = {0};
        AfeResult result=afe_sample(&d->config,ms,&next);
        sampled=result==AFE_OK;
        if (sampled) {
            d->sample=next;
            /* 检测期间仍执行正常保护。COW 对电压有扰动，撤销端点资格。 */
            if (probing && ms-probe_ms>=600) {
                d->wire_fault=next.min_mv<500;
                if (!afe_openwire(false)) result=AFE_ERROR;
                probing=false; wire_settling=true; probe_ms=ms;
            } else if (wire_settling && ms-probe_ms>=600) wire_settling=false;
            else if (!probing && !wire_settling && (next.max_mv-next.min_mv>600 || d->wire_fault) &&
                     ms-probe_ms>=5000) {
                if (!afe_openwire(true)) result=AFE_ERROR;
                else {probing=true; probe_ms=ms;}
            }
            if (result==AFE_OK && (next.alarms & 0x80)) {
                if (d->afe_fault && afe_ack(0x80)) sampled=false;
                else result=AFE_ERROR;
            }
            if (result==AFE_OK && sampled) {
                d->outputs=control_sample(&d->control,&d->config,&next,ms,d->allowed,
                                          gpio_read(GPIO_PB1)!=0,d->wire_fault);
                if (D008_COMM_TEST) {d->outputs.drive=0; d->outputs.heat=false; d->outputs.fuse=false; d->outputs.balance=0;}
                if (!d->outputs.heat) gpio_write(GPIO_PA1,0);
                if (!afe_outputs(d->outputs.drive,d->outputs.balance,ms) ||
                    (d->outputs.ack && !afe_ack(d->outputs.ack))) result=AFE_ERROR;
                else {
                    gpio_write(GPIO_PA1,d->outputs.heat);
#if D008_FUSE_FITTED
                    if (d->outputs.fuse) gpio_write(GPIO_PD4,1);
#endif
                    d->afe_fault=false; failing=false;
                }
            }
        }
        if (result==AFE_ERROR) {
            sampled=false;
            if (!failing) {failing=true; fail_ms=ms;}
            if (ms-fail_ms>=1000 || (next.alarms & 0x80))
                silence(ms);
        }
    }
    d->valid=sampled;
    if (!sampled) {gpio_write(GPIO_PA1,0); control_gap(&d->control);}
    soc_sample(&d->soc,&d->config,&d->sample,&d->outputs,sampled, sampled && !probing && !wire_settling &&
               !d->wire_fault && d->sample.min_mv>=500 && d->sample.max_mv<=4800,tick,ms);
    uint32_t dt=sampled && previous_good ? ms-last_good_ms : 0;
    if (dt>1000) dt=0;
    previous_good=sampled;
    if (sampled) {
        last_good_ms=ms;
        bool charging=d->sample.ma < -1000;
        normal_low_ms=(!charging && d->sample.min_mv<=3000) ? normal_low_ms+dt : 0;
        low_ms=(!charging && d->sample.min_mv<=2800) ? low_ms+dt : 0;
        uv_ms=(!charging && d->outputs.empty_trip) ? uv_ms+dt : 0;
        if (normal_low_ms>=86400000u || low_ms>=3600000u || uv_ms>=180000u)
            device_powerdown(false);
    }
    if (!D008_COMM_TEST) {
        uint16_t active=d->control.latched;
        if (sampled) {
            if (d->sample.alarms&0x40) active|=1u<<OV;
            if (d->sample.alarms&0x20) active|=1u<<UV;
            if (d->sample.alarms&0x0a) active|=1u<<CHARGE_OC;
            if (d->sample.alarms&0x15) active|=1u<<DISCHARGE_OC;
        }
        if (d->wire_fault) active|=1u<<12;
        if (d->afe_fault) active|=1u<<13;
        if (d->storage_fault) active|=1u<<14;
        if (sampled && d->outputs.heat) active|=1u<<15;
        if (!history_observe(active,ms)) d->storage_fault=true;
    }
}

bool device_set_allowed(uint8_t allowed,uint32_t ms)
{
    if (allowed>3 || mode!=SAMPLING || !device.valid || ms-last_good_ms>250u) return false;
    /* 使用与周期保护相同的最终仲裁，并把实际寄存器读回结果返回给上位机。 */
    device.allowed=allowed;
    Outputs next=control_sample(&device.control,&device.config,&device.sample,ms,allowed,
                                gpio_read(GPIO_PB1)!=0,device.wire_fault);
    if (!next.heat) gpio_write(GPIO_PA1,0);
    if (!afe_outputs(next.drive,next.balance,ms)) {silence(ms);return false;}
    device.outputs=next;
    return true;
}
