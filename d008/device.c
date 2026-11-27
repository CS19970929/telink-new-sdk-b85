#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "device.h"
#include "link.h"
#include "afe.h"
#include "history.h"
#include "dvc_bus.h"
#include "update_policy.h"
#include <string.h>

Device device;
static enum { SETUP, INIT_STORAGE, SAMPLING, QUIET, SHUTDOWN } mode;
static bool init_storage_pending;
static bool failing;
static uint32_t fail_ms, quiet_ms, normal_low_ms, low_ms, uv_ms;
static uint32_t service_ms, command_shutdown_ms, soc_space_ms, config_retry_ms;
static bool command_shutdown_pending;
enum { OUTPUT_SAMPLE_MAX_AGE_MS = 250, CURRENT_MAX_AGE_MS = 400, COMMAND_REPLY_MS = 500 };
static uint32_t last_good_ms, last_current_ms;
static bool previous_good, current_seen;
static bool soc_init_pending;
static uint32_t soc_retry_ms, soc_rest_ms;
static bool soc_resting;
static const uint32_t soc_update_id=D008_SOC_UPDATE_ID, cycle_update_id=D008_CYCLE_UPDATE_ID;
typedef char SocPolicy[(D008_SOC_UPDATE_PERCENT<=101u && D008_CYCLE_UPDATE_COUNT<UINT16_MAX) ? 1 : -1];

static void begin_setup(void)
{
    mode=SETUP; device.valid=false; previous_good=current_seen=soc_resting=false;
    control_gap(&device.control);
    afe_begin(&device.config);
}

static void restore_soc(void)
{
    SocSnapshot saved;
    StoreResult status=store_open(SOC_DATA,&saved,sizeof(saved));
    soc_init_pending=status!=STORE_VALID || !soc_restore(&device.soc,&device.config,&saved);
}

static void restore_runtime_storage(void)
{
    Device *d=&device;
    if (d->configured) restore_soc();
    StoreResult log=store_open(EVENT_DATA,0,2);
    d->storage_fault=!d->configured || !store_available(SOC_DATA) || log==STORE_IO_ERROR;
    if (d->configured) history_note(1,0);
}

static void load_config(void)
{
    Config assembly;
    config_defaults(&assembly,D008_CELLS,D008_NMC);
    StoreResult state=store_open_config(&device.config,&assembly);
    if (state!=STORE_VALID) device.config=assembly;
    bool updated=config_apply_updates(&device.config);
    bool legal=config_valid(&device.config);
    device.configured=state==STORE_VALID && legal && !updated;
    init_storage_pending=legal && ((state==STORE_VALID && updated) ||
        state==STORE_BLANK || state==STORE_BAD || state==STORE_IO_ERROR);
    if (!legal) device.config=assembly;
}

void device_init(void)
{
    load_config();
    device.allowed=device.configured ? CHARGE|DISCHARGE : 0;
    restore_runtime_storage();
    if (init_storage_pending) bls_ll_setAdvEnable(BLC_ADV_DISABLE);
    gpio_write(GPIO_PD7,1); dvc_bus_init();
    begin_setup();
}

static SocWriteResult save_soc(const Soc *candidate)
{
    if (!candidate->initialized || !device.configured || !store_available(SOC_DATA))
        return SOC_WRITE_FAILED;
    SocSnapshot s=soc_snapshot(candidate,&device.config);
    if (!store_append(SOC_DATA,&s,sizeof(s))) {
        if (!store_deferred) device.storage_fault=true;
        return store_deferred?SOC_WRITE_DEFERRED:SOC_WRITE_FAILED;
    }
    soc_init_pending=false;
    return SOC_WRITE_DONE;
}

bool device_save_soc(void)
{
    return save_soc(&device.soc)==SOC_WRITE_DONE;
}

SocWriteResult device_set_soc(unsigned percent,uint32_t ms)
{
    if (percent>100 || !device.configured || mode==SHUTDOWN || command_shutdown_pending || update_active())
        return SOC_WRITE_FAILED;
    Soc candidate=device.soc;
    soc_manual(&candidate,&device.config,percent,ms);
    SocWriteResult result=save_soc(&candidate);
    if (result==SOC_WRITE_DONE) device.soc=candidate;
    return result;
}

SocWriteResult device_set_cycle(unsigned cycles)
{
    if (cycles>=UINT16_MAX || !device.valid || mode!=SAMPLING ||
        command_shutdown_pending || update_active()) return SOC_WRITE_FAILED;
    Soc candidate=device.soc;
    soc_set_cycles(&candidate,&device.config,cycles);
    SocWriteResult result=save_soc(&candidate);
    if (result==SOC_WRITE_DONE) device.soc=candidate;
    return result;
}

static void service_soc(uint32_t ms)
{
    if (!store_available(SOC_DATA)) restore_soc();
    Soc candidate=device.soc;
    if (!candidate.initialized) return;
    bool updated=soc_init_pending;
    if (candidate.soc_id<soc_update_id &&
        (D008_SOC_UPDATE_PERCENT<=100u || (soc_resting && ms-soc_rest_ms>=5000))) {
        unsigned estimate=D008_SOC_UPDATE_PERCENT<=100u?D008_SOC_UPDATE_PERCENT:
            soc_estimate(&device.config,&device.sample);
        soc_manual(&candidate,&device.config,estimate,ms);
        candidate.soc_id=D008_SOC_UPDATE_ID;
        updated=true;
    }
    if (candidate.cycle_id<cycle_update_id) {
        soc_set_cycles(&candidate,&device.config,D008_CYCLE_UPDATE_COUNT);
        candidate.cycle_id=D008_CYCLE_UPDATE_ID;
        updated=true;
    }
    if (updated && save_soc(&candidate)==SOC_WRITE_DONE) device.soc=candidate;
}

static void silence(uint32_t ms)
{
    /* 随后完全停止 I²C 访问，连诊断和读请求也不得喂到硬件 WDT。 */
    (void)afe_disable_outputs(ms);
    device.valid=false; device.afe_fault=true;
    device.outputs=(Outputs){0};
    previous_good=current_seen=false; control_gap(&device.control);
    mode=QUIET; quiet_ms=ms; failing=false;
}

static bool current_qualified(uint32_t ms)
{
    /* 正常 CC2 转换慢于 MCU 采样周期。允许短暂复用，失败或超龄即撤销。 */
    return current_seen && ms-last_current_ms <= CURRENT_MAX_AGE_MS;
}

static bool revoke_outputs(uint8_t allowed,uint32_t ms)
{
    uint8_t keep=((allowed&CHARGE)?0x03u:0u) | ((allowed&DISCHARGE)?0x0cu:0u);
    uint8_t drive=device.outputs.drive & keep & ~device.outputs.reverse_modes;
    if (!afe_outputs(drive,0,ms)) {silence(ms);return false;}
    device.outputs.drive=drive;
    device.outputs.reverse_modes=0;
    device.outputs.balance=0;
    return true;
}

bool device_busy(void)
{
    return command_shutdown_pending || mode==SETUP || mode==INIT_STORAGE || mode==SHUTDOWN;
}

bool device_request_shutdown(uint32_t ms)
{
    if (mode==SHUTDOWN || update_active()) return false;
    if (!command_shutdown_pending) command_shutdown_ms=ms;
    command_shutdown_pending=true;
    return true;
}

uint16_t device_mos_status(void)
{
    /* Classic 诊断位依次为总授权、通信门禁、总线静默、故障锁存、采样合格。 */
    return (device.configured?0x0001u:0u) | (!device.valid?0x0002u:0u) |
        ((mode==QUIET || mode==SHUTDOWN)?0x0004u:0u) | (device.afe_fault?0x0008u:0u) |
        (device.valid?0x0010u:0u);
}

bool device_service(uint32_t ms)
{
    if (mode==SHUTDOWN) return false;
    service_ms=ms;
    if (command_shutdown_pending) {
        /* 先让 Modbus 应答离开传输队列，按旧合同最多等 500 ms。 */
        if (ms-command_shutdown_ms<COMMAND_REPLY_MS &&
            (wired_reply_pending() || product_radio_reply_pending())) return true;
        device_powerdown(false);
        return false;
    }
    if (mode==SETUP) {
        AfeResult result=afe_setup_step();
        if (result==AFE_ERROR) {
            if (init_storage_pending) bls_ll_setAdvEnable(BLC_ADV_ENABLE);
            silence(ms);
        } else if (result==AFE_OK) {
            mode=init_storage_pending?INIT_STORAGE:SAMPLING;
            failing=false;
        }
    } else if (mode==INIT_STORAGE) {
        /* 提交可能已经生效，读回未确认时先重新扫描。 */
        if (!store_available(CONFIG_DATA)) load_config();
        bool saved=device.configured || (init_storage_pending &&
            store_append(CONFIG_DATA,&device.config,sizeof(device.config)));
        config_retry_ms=ms;
        bls_ll_setAdvEnable(BLC_ADV_ENABLE);
        if (saved) {
            init_storage_pending=false;
            device.configured=true;
            device.allowed=CHARGE|DISCHARGE;
            restore_runtime_storage();
            begin_setup();
        } else {
            device.storage_fault=true;
            mode=SAMPLING;
        }
    } else if (mode==QUIET && ms-quiet_ms>=5000) {
        begin_setup();
    } else if (mode==SAMPLING && init_storage_pending && device.valid &&
               ms-config_retry_ms>=5000 && !wired_busy() && !update_active() &&
               blc_ll_getCurrentState()!=BLS_LINK_STATE_CONN) {
        mode=INIT_STORAGE;
    }
    if (mode==SAMPLING && device.valid && device.configured && !update_active() && !wired_busy() &&
        blc_ll_getCurrentState()!=BLS_LINK_STATE_CONN && (!soc_space_ms || ms-soc_space_ms>=5000)) {
        soc_space_ms=ms;
        if (!store_prepare_soc_space() && !store_deferred) device.storage_fault=true;
    }
    if (mode==SAMPLING && device.valid && device.configured && !device_busy() && !update_active() &&
        ms-soc_retry_ms>=5000 && (soc_init_pending || !store_available(SOC_DATA) ||
        device.soc.soc_id<soc_update_id || device.soc.cycle_id<cycle_update_id)) {
        soc_retry_ms=ms;
        service_soc(ms);
    }
    if (device.configured && !update_active() && !device_busy() && !history_flush(ms))
        device.storage_fault=true;
    return true;
}

bool device_set_config(const Config *c)
{
    if (!device.configured || mode!=SAMPLING || !device.valid || update_active() ||
        c->cells!=device.config.cells || c->nmc!=device.config.nmc || !config_valid(c)) return false;
    if (!memcmp(c,&device.config,sizeof(*c))) return true;
    bool hardware=memcmp(&c->hw_ov_mv,&device.config.hw_ov_mv,
               (const uint8_t *)&c->balance_on-(const uint8_t *)&c->hw_ov_mv)!=0;
    if (hardware) {
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
    soc_resize(&device.soc,device.config.capacity_dah,c->capacity_dah);
    device.config=*c;
    if (hardware) {device.valid=false; previous_good=current_seen=false;}
    return true;
}

void device_powerdown(bool acc)
{
    if (mode==SHUTDOWN) return;
    bool bus_allowed=mode!=QUIET;
    wired_stop();
    mode=SHUTDOWN;
    command_shutdown_pending=false;
    gpio_write(D008_RUN_LED_PIN,0);
    device.valid=false;
    device.outputs=(Outputs){0};
    if (bus_allowed) {(void)afe_disable_outputs(service_ms); (void)afe_shutdown();}
    if (device.configured) {
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
    uint8_t observed_alarms=0;
    if (mode==SAMPLING) {
        Sample next = {0};
        AfeResult result=afe_sample(&d->config,ms,&next);
        sampled=result==AFE_OK;
        if (sampled) {
            d->sample=next;
            observed_alarms=next.alarms;
            if (next.current_fresh) {last_current_ms=ms; current_seen=true;}
            uint8_t remaining;
            if (next.alarms & 0x80) {
                if (d->afe_fault && afe_ack(0x80,&remaining) && !(remaining&0x80)) sampled=false;
                else result=AFE_ERROR;
            }
            if (result==AFE_OK && sampled && !d->configured && next.fet) {
                /* 发现驱动异常时重新发送硬关断并让 AFE watchdog 接管。 */
                silence(ms);
                result=AFE_ERROR;
            }
            if (result==AFE_OK && sampled && d->configured) {
                Outputs out=control_sample(&d->control,&d->config,&next,ms,d->allowed,
                                           current_qualified(ms),gpio_read(GPIO_PB1)!=0);
                bool closing=(d->outputs.drive & ~out.drive)!=0;
                bool output_ok=afe_outputs(out.drive,out.balance,ms);
                if (output_ok) d->outputs=out;
                if (!output_ok && closing) silence(ms);
                if (!output_ok || (out.ack && !afe_ack(out.ack,&remaining))) result=AFE_ERROR;
                else if (out.ack) {
                    control_acknowledged(&d->control,out.ack,remaining);
                    d->sample.alarms=remaining;
                }
            }
            if (result==AFE_OK && sampled) {d->afe_fault=false; failing=false;}
        }
        if (result==AFE_ERROR) {
            sampled=false;
            if (mode==SAMPLING) {
                if (!failing) {failing=true; fail_ms=ms;}
                if (ms-fail_ms>=1000 || (next.alarms & 0x80)) silence(ms);
            }
        }
    }
    d->valid=sampled;
    if (!sampled) {
        current_seen=false;
        control_gap(&d->control);
        if (mode==SAMPLING && (d->outputs.reverse_modes || d->outputs.balance))
            (void)revoke_outputs(d->allowed,ms);
    }
    if (!d->configured) return;
    soc_sample(&d->soc,&d->config,&d->sample,&d->outputs,sampled,
               sampled && d->sample.min_mv>=500 && d->sample.max_mv<=4800,tick,ms);
    bool rest=sampled && d->sample.ma>=-320 && d->sample.ma<=320 &&
        d->sample.min_mv>=1500 && d->sample.max_mv<=4500 &&
        d->sample.max_mv-d->sample.min_mv<=100 && !d->sample.alarms &&
        !(d->control.latched & ~(1u<<SOC_LOW)) && current_qualified(ms);
    if (rest && (!soc_resting || !previous_good || ms-last_good_ms>1000)) soc_rest_ms=ms;
    soc_resting=rest;
    if (sampled && d->soc.initialized) control_soc(&d->control,&d->config,d->soc.state.percent,ms);
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
    uint16_t active=d->control.latched;
    if (sampled) {
        if (observed_alarms&0x40) active|=1u<<OV;
        if (observed_alarms&0x20) active|=1u<<UV;
        if (observed_alarms&0x0a) active|=1u<<CHARGE_OC;
        if (observed_alarms&0x15) active|=1u<<DISCHARGE_OC;
    }
    if (d->afe_fault) active|=1u<<13;
    if (d->storage_fault) active|=1u<<14;
    if (!history_observe(active,ms)) d->storage_fault=true;
}

bool device_set_allowed(uint8_t allowed,uint32_t ms)
{
    if (allowed>(CHARGE|DISCHARGE) || (allowed && !device.configured)) return false;
    uint8_t previous=device.allowed;
    if (allowed==previous) return true;
    bool fresh=mode==SAMPLING && device.valid && ms-last_good_ms<=OUTPUT_SAMPLE_MAX_AGE_MS &&
        current_qualified(ms);
    if ((allowed & ~previous) && !fresh) return false;
    /* 关闭请求即使没有新样本也生效。静默期保存请求，保持总线关闭。 */
    device.allowed=allowed;
    if (!fresh) return mode!=SAMPLING || revoke_outputs(allowed,ms);
    Outputs next=control_sample(&device.control,&device.config,&device.sample,ms,allowed,
                                true,gpio_read(GPIO_PB1)!=0);
    if (!afe_outputs(next.drive,next.balance,ms)) {
        device.allowed &= previous;
        silence(ms);
        return false;
    }
    device.outputs=next;
    return true;
}
