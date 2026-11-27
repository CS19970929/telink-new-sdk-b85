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
static enum { SETUP, INIT_STORAGE, SAMPLING, QUIET, SHUTDOWN_SAVE, SHUTDOWN } mode;
static bool init_storage_pending, boot_storage_pending;
static bool failing;
static uint32_t fail_ms, quiet_ms, normal_low_ms, low_ms, uv_ms;
static uint32_t service_ms, command_shutdown_ms, soc_space_ms;
enum { CONFIG_RETRY_MS=5000 };
static uint32_t config_retry_ms=0u-CONFIG_RETRY_MS;
static bool command_shutdown_pending, shutdown_acc, shutdown_bus, shutdown_soc_saved;
static uint32_t shutdown_ms, shutdown_attempt_ms;
enum { SHUTDOWN_SAVE_MS=900, SHUTDOWN_RETRY_MS=50, SHUTDOWN_FLASH_MARGIN_MS=350 };
enum { OUTPUT_SAMPLE_MAX_AGE_MS = 250, CURRENT_MAX_AGE_MS = 400, COMMAND_REPLY_MS = 500 };
static uint32_t last_good_ms, last_current_ms;
static bool previous_good, current_seen;
static bool soc_init_pending;
static uint32_t soc_retry_ms, soc_rest_ms, factory_retry_ms, log_retry_ms;
static bool soc_resting;
static const uint32_t log_clear_id=D008_LOG_CLEAR_ID;
static const uint32_t soc_update_id=D008_SOC_UPDATE_ID, cycle_update_id=D008_CYCLE_UPDATE_ID;
typedef char SocPolicy[(D008_SOC_UPDATE_PERCENT<=101u && D008_CYCLE_UPDATE_COUNT<UINT16_MAX) ? 1 : -1];

static bool factory_pending(void)
{
    return device.config.factory_generation&1u;
}

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
    StoreResult log=store_open_log();
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
    boot_storage_pending=init_storage_pending;
    if (init_storage_pending) bls_ll_setAdvEnable(BLC_ADV_DISABLE);
    gpio_write(GPIO_PD7,1); dvc_bus_init();
    begin_setup();
}

static SocWriteResult save_soc(const Soc *candidate)
{
    store_deferred=false;
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
    return !factory_pending() && save_soc(&device.soc)==SOC_WRITE_DONE;
}

SocWriteResult device_set_soc(unsigned percent,uint32_t ms)
{
    if (percent>100 || factory_pending() || !device.configured || mode>=SHUTDOWN_SAVE || command_shutdown_pending || update_active())
        return SOC_WRITE_FAILED;
    Soc candidate=device.soc;
    soc_manual(&candidate,&device.config,percent,ms);
    SocWriteResult result=save_soc(&candidate);
    if (result==SOC_WRITE_DONE) device.soc=candidate;
    return result;
}

SocWriteResult device_set_cycle(unsigned cycles)
{
    if (cycles>=UINT16_MAX || factory_pending() || !device.valid || mode!=SAMPLING ||
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

static bool factory_step(uint32_t ms)
{
    uint32_t id=device.config.factory_generation;
    if (!store_available(SOC_DATA)) restore_soc();
    if (device.soc.factory_id!=id) {
        Soc candidate=device.soc;
        /* 恢复出厂采用产品的初始中值，随后按正常算法积分和端点校准。 */
        soc_manual(&candidate,&device.config,50,ms);
        soc_set_cycles(&candidate,&device.config,0);
        candidate.factory_id=id;
        if (save_soc(&candidate)!=SOC_WRITE_DONE) return false;
        device.soc=candidate;
        return true;
    }
    if (!store_available(EVENT_DATA) || store_log_factory_id()!=id)
        return history_clear(0,id);
    Config done=device.config;
    ++done.factory_generation;
    if (!store_available(CONFIG_DATA)) {
        Config selected;
        if (store_open_config(&selected,&device.config)!=STORE_VALID ||
            (selected.factory_generation!=id && selected.factory_generation!=done.factory_generation))
            return false;
    }
    if (!store_append(CONFIG_DATA,&done,sizeof(done))) return false;
    device.config=done;
    return true;
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
    return command_shutdown_pending || mode==SETUP || mode==INIT_STORAGE || mode>=SHUTDOWN_SAVE;
}

bool device_request_shutdown(uint32_t ms)
{
    if (mode>=SHUTDOWN_SAVE || update_active()) return false;
    if (!command_shutdown_pending) command_shutdown_ms=ms;
    command_shutdown_pending=true;
    return true;
}

uint16_t device_mos_status(void)
{
    /* Classic 诊断位依次为总授权、通信门禁、总线静默、故障锁存、采样合格。 */
    return (device.configured?0x0001u:0u) | (!device.valid?0x0002u:0u) |
        ((mode>=QUIET)?0x0004u:0u) | (device.afe_fault?0x0008u:0u) |
        (device.valid?0x0010u:0u);
}

bool device_prepare_ota(void)
{
    if (device_busy() || factory_pending() || mode!=SAMPLING ||
        !device.valid || !device.configured) return false;
    if (!afe_confirm_off(service_ms)) {silence(service_ms);return false;}
    device.outputs=(Outputs){0};
    return true;
}

static void finish_powerdown(void)
{
    mode=SHUTDOWN;
    if (shutdown_bus) (void)afe_shutdown();
    if (!shutdown_acc) {gpio_write(GPIO_PC4,0);return;}
    if (!gpio_read(GPIO_PA0)) start_reboot();
    cpu_set_gpio_wakeup(GPIO_PA0,Level_Low,1);
    cpu_sleep_wakeup(DEEPSLEEP_MODE,PM_WAKEUP_PAD,0);
    start_reboot();
}

bool device_service(uint32_t ms)
{
    service_ms=ms;
    if (mode==SHUTDOWN) return false;
    if (mode==SHUTDOWN_SAVE) {
        uint32_t elapsed=ms-shutdown_ms;
        if (elapsed>=SHUTDOWN_SAVE_MS-SHUTDOWN_FLASH_MARGIN_MS) finish_powerdown();
        else if (ms-shutdown_attempt_ms>=SHUTDOWN_RETRY_MS &&
                 blc_ll_getCurrentState()!=BLS_LINK_STATE_CONN) {
            shutdown_attempt_ms=ms;
            if (!device.configured || factory_pending() || update_active()) finish_powerdown();
            else if (shutdown_soc_saved) {
                (void)history_flush(ms);
                finish_powerdown();
            } else shutdown_soc_saved=device_save_soc();
        }
        return false;
    }
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
        } else if (init_storage_pending && afe_setup_safe() && ms-config_retry_ms>=CONFIG_RETRY_MS) {
            mode=INIT_STORAGE;
        } else if (result==AFE_OK) {
            mode=SAMPLING;
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
            if (boot_storage_pending) device.allowed=CHARGE|DISCHARGE;
            boot_storage_pending=false;
            restore_runtime_storage();
            begin_setup();
        } else {
            device.storage_fault=true;
            mode=SETUP;
        }
    } else if (mode==QUIET && ms-quiet_ms>=5000) {
        begin_setup();
    } else if (mode==SAMPLING && init_storage_pending && device.valid &&
               ms-config_retry_ms>=CONFIG_RETRY_MS && !wired_busy() && !update_active() &&
               blc_ll_getCurrentState()!=BLS_LINK_STATE_CONN) {
        mode=INIT_STORAGE;
    }
    if (mode==SAMPLING && device.configured && factory_pending() && !update_active() &&
        (!factory_retry_ms || ms-factory_retry_ms>=5000)) {
        if (factory_step(ms)) factory_retry_ms=0;
        else {factory_retry_ms=ms; device.storage_fault=true;}
        return true;
    }
    if (mode==SAMPLING && device.valid && device.configured && !update_active() && !wired_busy() &&
        blc_ll_getCurrentState()!=BLS_LINK_STATE_CONN && (!soc_space_ms || ms-soc_space_ms>=5000)) {
        soc_space_ms=ms;
        if (!store_prepare_soc_space() && !store_deferred) device.storage_fault=true;
    }
    if (mode==SAMPLING && device.valid && device.configured && !factory_pending() && !device_busy() && !update_active() &&
        ms-soc_retry_ms>=5000 && (soc_init_pending || !store_available(SOC_DATA) ||
        device.soc.soc_id<soc_update_id || device.soc.cycle_id<cycle_update_id)) {
        soc_retry_ms=ms;
        service_soc(ms);
    }
    if (device.configured && !factory_pending() && !update_active() && !device_busy()) {
        if (store_available(EVENT_DATA) && store_log_update_id()<log_clear_id) {
            if (!log_retry_ms || ms-log_retry_ms>=5000) {
                log_retry_ms=ms;
                if (!history_clear(log_clear_id,0)) device.storage_fault=true;
            }
        } else if (!history_flush(ms)) device.storage_fault=true;
    }
    return true;
}

bool device_set_config(const Config *c)
{
    if (!device.configured || factory_pending() || command_shutdown_pending || mode!=SAMPLING || !device.valid || update_active() ||
        c->cells!=device.config.cells || c->nmc!=device.config.nmc || !config_valid(c)) return false;
    if (!memcmp(c,&device.config,sizeof(*c))) return true;
    bool hardware=(c->factory_generation&1u) || memcmp(&c->hw_ov_mv,&device.config.hw_ov_mv,
               (const uint8_t *)&c->balance_on-(const uint8_t *)&c->hw_ov_mv)!=0;
    if (hardware) {
        if (!afe_confirm_off(service_ms)) {silence(service_ms);return false;}
        device.outputs=(Outputs){0};
        device.valid=false;
    }
    if (!store_append(CONFIG_DATA,c,sizeof(*c))) {
        if (!store_deferred) device.storage_fault=true;
        if (!store_available(CONFIG_DATA)) {
            /* 读回失去权威时不再运行旧 RAM 参数，下次先读已提交记录。 */
            device.configured=false;
            init_storage_pending=true;
            silence(service_ms);
        } else if (hardware) begin_setup();
        return false;
    }
    soc_resize(&device.soc,device.config.capacity_dah,c->capacity_dah);
    device.config=*c;
    control_gap(&device.control);
    if (!hardware) return true;

    /* 稳定持久化之后只从 device.config 同步。保留当前锁存，失败由前台重试。 */
    begin_setup();
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    for (unsigned i=0;i<40;++i) {
        AfeResult result=afe_setup_step();
        if (result==AFE_OK) {mode=SAMPLING;return true;}
        if (result==AFE_ERROR) {silence(service_ms);return false;}
        blt_sdk_main_loop();
    }
    return false;
}

bool device_restore(unsigned group,uint32_t ms)
{
    if (device_busy() || factory_pending() || update_active() || !device.configured || !device.valid) return false;
    if (group==RESET_SOC) {
        if (!soc_resting || ms-soc_rest_ms<5000) return false;
        return device_set_soc(soc_estimate(&device.config,&device.sample),ms)==SOC_WRITE_DONE;
    }
    if (group==RESET_CYCLE) return device_set_cycle(0)==SOC_WRITE_DONE;
    if (group==RESET_LOG) return history_clear(0,0);
    Config c=device.config, defaults;
    config_defaults(&defaults,c.cells,c.nmc);
    if (group==RESET_FACTORY) {
        if (c.factory_generation>=UINT32_MAX-1u) return false;
        defaults.offset_ma=c.offset_ma; defaults.gain_ppm=c.gain_ppm;
        memcpy(defaults.sn,c.sn,sizeof(c.sn));
        memcpy(defaults.update_id,c.update_id,sizeof(c.update_id));
        defaults.factory_generation=c.factory_generation+1u;
        return device_set_config(&defaults);
    }
    switch (group) {
    case RESET_SW:
        memcpy(c.sw,defaults.sw,sizeof(c.sw));
        break;
    case RESET_HW:
        memcpy(&c.hw_ov_mv,&defaults.hw_ov_mv,
               (const uint8_t *)&c.balance_on-(const uint8_t *)&c.hw_ov_mv);
        break;
    case RESET_BUSINESS:
        c.capacity_dah=defaults.capacity_dah;
        /* 业务组按兼容协议同时恢复均衡。 */
        /* fall through */
    case RESET_BALANCE:
        c.balance_on=defaults.balance_on; c.balance_mv=defaults.balance_mv;
        c.balance_start_mv=defaults.balance_start_mv; c.balance_stop_mv=defaults.balance_stop_mv;
        break;
    case RESET_ENDPOINTS:
        c.full_mv=defaults.full_mv; c.empty_mv=defaults.empty_mv;
        break;
    case RESET_CALIBRATION:
        c.offset_ma=defaults.offset_ma; c.gain_ppm=defaults.gain_ppm;
        break;
    default: return false;
    }
    return device_set_config(&c);
}

void device_powerdown(bool acc)
{
    if (mode>=SHUTDOWN_SAVE) return;
    shutdown_bus=mode!=QUIET && afe_confirm_off(service_ms);
    shutdown_acc=acc;
    shutdown_soc_saved=false;
    shutdown_ms=service_ms;
    shutdown_attempt_ms=service_ms-SHUTDOWN_RETRY_MS;
    mode=SHUTDOWN_SAVE;
    command_shutdown_pending=false;
    gpio_write(D008_RUN_LED_PIN,0);
    device.valid=false;
    device.outputs=(Outputs){0};
    wired_stop();
    if (device.configured && !factory_pending()) (void)history_note(2,service_ms);
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    bls_pm_setAppWakeupLowPower(0,0);
    bls_ll_setAdvEnable(BLC_ADV_DISABLE);
    if (blc_ll_getCurrentState()==BLS_LINK_STATE_CONN)
        bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN);
}

void device_measure(uint32_t tick,uint32_t ms)
{
    Device *d=&device;
    if (mode>=SHUTDOWN_SAVE) return;
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
            if (result==AFE_OK && sampled && (!d->configured || factory_pending()) && next.fet) {
                /* 发现驱动异常时重新发送硬关断并让 AFE watchdog 接管。 */
                silence(ms);
                result=AFE_ERROR;
            }
            if (result==AFE_OK && sampled && d->configured && !factory_pending()) {
                Outputs out=control_sample(&d->control,&d->config,&next,ms,update_active()?0:d->allowed,
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
    if (!d->configured || factory_pending()) return;
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
    if (update_active() || mode>=SHUTDOWN_SAVE || allowed>(CHARGE|DISCHARGE) || (allowed && (!device.configured || factory_pending()))) return false;
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
