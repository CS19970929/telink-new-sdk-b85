#include "../device.h"
#include "../link.h"
#include "../afe.h"
#include "../dvc_bus.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash[0x10000];
static uint8_t drive;
static unsigned setup_steps, shutdown_count, flash_writes;
static uint32_t ms, fail_read_once;
static bool powered, fail_setup, fail_verify, busy_ota, blackout_log, deny_writes;
static bool connected, terminate, power_cut, fail_off;
static bool wrote_config, write_before_setup;

bool nor_read(uint32_t at,void *data,unsigned n)
{
    assert(at>=0x40000 && at+n<=0x50000);
    if (fail_read_once>=at && fail_read_once-at<n) {
        fail_read_once=0;
        return false;
    }
    if (blackout_log && at>=0x4c000) return false;
    memcpy(data,flash+at-0x40000,n);
    return true;
}

bool nor_write(uint32_t at,const void *data,unsigned n)
{
    assert(at>=0x40000 && at+n<=0x50000 && n<=256-(at&255));
    assert(!busy_ota);
    if (deny_writes) return false;
    ++flash_writes;
    if (at<0x42000) {
        wrote_config=true;
        if (setup_steps<3) write_before_setup=true;
    }
    for (unsigned i=0;i<n;++i) flash[at-0x40000+i]&=((const uint8_t *)data)[i];
    if (fail_verify && at<0x42000 && n==1) {
        fail_read_once=at;
        fail_verify=false;
    }
    return true;
}

bool nor_erase(uint32_t at)
{
    assert(at>=0x40000 && at+4096<=0x50000 && !(at&4095));
    if (deny_writes) return false;
    if (connected) {store_deferred=true;return false;}
    memset(flash+at-0x40000,255,4096);
    return true;
}

void dvc_bus_init(void) {}
void afe_begin(const Config *c) {assert(config_valid(c));setup_steps=0;}
AfeResult afe_setup_step(void)
{
    if (!powered || fail_setup) return AFE_ERROR;
    drive=0;
    return ++setup_steps>=5?AFE_OK:AFE_WAIT;
}
bool afe_setup_safe(void) {return setup_steps>=3;}
bool afe_disable_outputs(uint32_t now) {(void)now;drive=0;return powered;}
bool afe_confirm_off(uint32_t now) {return !fail_off && afe_disable_outputs(now);}
bool afe_shutdown(void) {assert(!drive);++shutdown_count;return true;}
bool afe_ack(uint8_t flags,uint8_t *remaining) {(void)flags;*remaining=0;return true;}
bool afe_outputs(uint8_t requested,uint32_t balance,uint32_t now)
{
    (void)balance;(void)now;
    if (requested) assert(device.configured && !(device.config.factory_generation&1u) && !busy_ota);
    drive=requested;
    return true;
}
AfeResult afe_sample(const Config *c,uint32_t now,Sample *s)
{
    (void)now;
    if (!powered) return AFE_ERROR;
    *s=(Sample){.min_mv=3200,.max_mv=3200,.sum_mv=3200u*c->cells,.fet=drive?3:0,
                .temp_x10={650,650,650},.current_fresh=true};
    for (unsigned i=0;i<c->cells;++i) s->mv[i]=3200;
    return AFE_OK;
}

void gpio_write(unsigned pin,unsigned value) {if (pin==GPIO_PC4 && !value) power_cut=true;}
unsigned gpio_read(unsigned pin) {(void)pin;return 0;}
void start_reboot(void) {assert(0);}
void cpu_set_gpio_wakeup(unsigned pin,unsigned level,unsigned enabled) {(void)pin;(void)level;(void)enabled;}
void cpu_sleep_wakeup(unsigned mode,unsigned source,unsigned tick) {(void)mode;(void)source;(void)tick;}
void bls_ll_setAdvEnable(unsigned enable) {(void)enable;}
void bls_pm_setSuspendMask(unsigned mask) {(void)mask;}
void bls_pm_setAppWakeupLowPower(uint32_t tick,unsigned enabled) {(void)tick;(void)enabled;}
void bls_ll_terminateConnection(unsigned reason) {(void)reason;terminate=true;}
unsigned blc_ll_getCurrentState(void) {return connected?BLS_LINK_STATE_CONN:0;}
void blt_sdk_main_loop(void) {}
void wired_stop(void) {}
bool wired_busy(void) {return false;}
bool wired_reply_pending(void) {return false;}
bool product_radio_reply_pending(void) {return false;}
bool update_active(void) {return busy_ota;}

static void run_for(unsigned duration)
{
    uint32_t end=ms+duration;
    while (ms<end) {
        ms+=50;
        if (!(ms%200)) device_measure(ms*TICKS_PER_MS,ms);
        device_service(ms);
    }
}

static void seed_flash(bool restart)
{
    memset(flash,255,sizeof(flash));
    Config c;
    config_defaults(&c,16,false);
    Soc s={0};
    soc_manual(&s,&c,restart?50:70,0);
    if (restart) {
        c.factory_generation=1;
        s.factory_id=1;
        assert(store_open_config(&device.config,&c)==STORE_BLANK);
        assert(store_append(CONFIG_DATA,&c,sizeof(c)));
        powered=true;
    } else {
        memset(flash,0x51,8192);
        soc_set_cycles(&s,&c,5);
        s.factory_id=9;
    }
    SocSnapshot saved=soc_snapshot(&s,&c);
    assert(store_open(SOC_DATA,0,sizeof(saved))==STORE_BLANK && store_append(SOC_DATA,&saved,sizeof(saved)));
    assert(store_open_log()==STORE_BLANK);
    if (!restart) assert(store_log_clear(0,9));
    assert(store_log_append(0x55ab));
    wrote_config=write_before_setup=false;
}

int main(int argc,char **argv)
{
    bool restart=argc>1 && !strcmp(argv[1],"factory-restart");
    bool save_fails=argc>1 && !strcmp(argv[1],"shutdown-io");
    seed_flash(restart);
    device_init();
    if (restart) {
        blackout_log=true;
        run_for(1000);
        assert(device.config.factory_generation==1 && !drive && device.soc.factory_id==1);
        blackout_log=false;
        run_for(6000);
        assert(device.config.factory_generation==2 && drive==15);
        for (unsigned i=0;i<store_events();++i) {
            uint16_t event=0;
            assert(store_event_at(i,&event) && event!=0x55ab);
        }
        puts("PASS lifecycle: restart resumes factory after SOC receipt");
        return 0;
    }
    run_for(1000);
    assert(!device.configured && !drive && !wrote_config);
    powered=true;
    deny_writes=true;
    run_for(5000);
    assert(!device.configured && !drive);
    deny_writes=false;
    run_for(6000);
    assert(device.configured && device.valid && drive==15 && !write_before_setup);
    assert(device.soc.state.percent==70 && soc_cycles(&device.soc,&device.config)==5);
    uint16_t event=0;
    assert(store_events()>=2 && store_event_at(store_events()-1,&event) && event==0x55ab);

    if (argc>1 && !strcmp(argv[1],"factory-generation")) {
        assert(device_restore(RESET_FACTORY,ms));
        assert(device.config.factory_generation==11 && !drive);
        run_for(1000);
        assert(device.config.factory_generation==12 && drive==15);
        assert(device.soc.factory_id==11 && soc_cycles(&device.soc,&device.config)==0);
        puts("PASS lifecycle: new factory generation after independent CONFIG reset");
        return 0;
    }

    Config c=device.config;
    c.capacity_dah=300;
    fail_verify=true;
    assert(!device_set_config(&c) && !device.configured && !drive);
    run_for(6500);
    assert(device.configured && device.valid && drive==15 && device.config.capacity_dah==300);
    assert(device.soc.state.percent==70 && device.soc.state.remaining_uas==75600000000LL);

    c=device.config;
    c.hw_sc_a10=5600;
    fail_setup=true;
    assert(!device_set_config(&c) && !device.valid && !drive && device.config.hw_sc_a10==5600);
    fail_setup=false;
    run_for(6000);
    assert(device.valid && drive==15 && device.config.hw_sc_a10==5600);
    fail_off=true;
    assert(!device_prepare_ota());
    fail_off=false;
    run_for(6000);
    assert(device_prepare_ota() && !drive);
    unsigned writes=flash_writes;
    busy_ota=true;
    run_for(2000);
    assert(!drive && flash_writes==writes && !device_request_shutdown(ms));
    busy_ota=false;
    run_for(400);
    assert(drive==15);

    connected=true;
    assert(device_request_shutdown(ms));
    run_for(100);
    assert(!drive && !power_cut && terminate);
    connected=false;
    deny_writes=save_fails;
    device.soc.state.remaining_uas-=1000;
    run_for(900);
    assert(power_cut && shutdown_count==1 && !drive);
    assert(device.soc.state.percent==70);
    puts("PASS lifecycle: AFE recovery, unknown commit, DVC retry, OTA and shutdown");
    return 0;
}
