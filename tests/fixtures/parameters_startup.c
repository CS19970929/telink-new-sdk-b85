#define BMS_SERVICE_DIAG_ENABLE 1
/* 生产参数 TU，替换域 I/O：验证调用顺序、单次域初始化和失败门禁。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bms_parameters.h"
#include "bms_config_store.h"
#include "bms_event_log.h"
#include "bms_state_store.h"
#include "bms_error.h"
#include "bms_diag.h"
static char calls[32];
static unsigned count, failed_domain;
static unsigned factory_pending;
enum {
    FACTORY_STATE_FAIL = 5u,
    FACTORY_EVENT_FAIL,
    FACTORY_FINISH_FAIL
};
static uint16_t upgrade_status;
static void note(char action) { calls[count++]=action; calls[count]=0; }
int bms_config_store_validate_startup(void) { note('C'); return failed_domain!=1u; }
int bms_config_store_factory_reset_pending(void) { return factory_pending!=0u; }
int bms_state_store_factory_reset(void) { assert(factory_pending); note('R'); return failed_domain!=FACTORY_STATE_FAIL; }
int bms_event_log_factory_reset(void) { assert(factory_pending); note('T'); return failed_domain!=FACTORY_EVENT_FAIL; }
int bms_config_store_finish_factory_reset(void) {
    assert(factory_pending); note('F');
    if(failed_domain==FACTORY_FINISH_FAIL) return 0;
    factory_pending=0u;
    return 1;
}
int bms_state_store_init(void) { note('S'); return failed_domain!=2u; }
int bms_event_log_init(void) { note('E'); return failed_domain!=3u; }
int bms_config_store_init(void) { assert(0 && "protection loading must not retry Config initialization"); return 0; }
int bms_config_store_get_protect(bms_protection_params_t *p) { note('G'); memset(p,0,sizeof(*p)); return failed_domain!=1u; }
void bms_config_store_get_default_protect(bms_protection_params_t *p) { note('D'); memset(p,0,sizeof(*p)); }
int bms_config_store_set_protect(const bms_protection_params_t *p) { (void)p; return 1; }
int bms_config_store_get_soc(bms_soc_config_t *p) { memset(p,0,sizeof(*p)); return 1; }
uint8_t bms_soc_reload_configuration(void) { return 1u; }
uint8_t bms_sw_protection_validate_params(const bms_protection_params_t *p) { note('P'); return p!=0 && failed_domain!=4u; }
void bms_diag_boot_word(uint16_t offset,uint16_t value) { (void)offset; (void)value; }
void bms_diag_upgrade(uint16_t status,uint16_t invalid) { upgrade_status=status; (void)invalid; }
void bms_diag_params(uint8_t valid,uint8_t startup) { (void)valid; (void)startup; }
void bms_error_raise(bms_error_id_t error) { assert(error==BMS_ERROR_EEPROM_STORE); }
int main(void) {
    for(failed_domain=0;failed_domain<5;failed_domain++) {
        count=0; upgrade_status=0; bms_parameters_init();
        assert(!strcmp(calls,failed_domain==1u ? "CSEGD" : "CSEGP"));
        assert(bms_protection_params_valid()==(failed_domain!=1 && failed_domain!=4));
        if(failed_domain==2) assert(upgrade_status==DIAG_UPGRADE_STATE);
        if(failed_domain==3) assert(upgrade_status==DIAG_UPGRADE_EVENT);
        if(failed_domain==1) assert(upgrade_status==0); /* Config 自身的失败诊断不被覆盖。 */
        if(failed_domain>0 && failed_domain<4) {
            assert(bms_protection_params_commit(&g_bms_protection_params));
            assert(bms_protection_params_valid()==(failed_domain!=1));
        }
    }
    failed_domain=0;count=0;bms_parameters_init();assert(bms_protection_params_valid());
    puts("PASS production parameter startup: every domain once despite earlier failure, Config gate sticky across commit, State/Event degraded, explicit restart");
    /* 出厂事务的 State/Event 失败必须阻断，与普通启动的独立降级不同。 */
    const unsigned factory_failures[] = {0u, FACTORY_STATE_FAIL, FACTORY_EVENT_FAIL, FACTORY_FINISH_FAIL};
    for(unsigned i=0u;i<sizeof(factory_failures)/sizeof(factory_failures[0]);++i) {
        failed_domain=factory_failures[i];factory_pending=1u;count=0u;
        bms_parameters_init();
        assert(!strcmp(calls,failed_domain==FACTORY_STATE_FAIL ? "CRGP" :
                            failed_domain==FACTORY_EVENT_FAIL ? "CRTGP" : "CRTFGP"));
        assert(bms_protection_params_valid()==(failed_domain==0u));
        assert(factory_pending==(failed_domain!=0u));
        if(failed_domain!=0u) {
            assert(bms_protection_params_commit(&g_bms_protection_params));
            assert(!bms_protection_params_valid());
        }
    }
    failed_domain=0u;count=0u;bms_parameters_init();
    assert(bms_protection_params_valid() && !factory_pending);
    puts("PASS production factory startup: pending -> State -> Event -> done, each failure inhibits outputs, ordinary commits cannot bypass, retry completes");
    return 0;
}
