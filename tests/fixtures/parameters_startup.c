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
static uint16_t upgrade_status;
static void note(char action) { calls[count++]=action; calls[count]=0; }
int bms_config_store_validate_startup(void) { note('C'); return failed_domain!=1u; }
int bms_state_store_init(void) { note('S'); return failed_domain!=2u; }
int bms_event_log_init(void) { note('E'); return failed_domain!=3u; }
int bms_config_store_init(void) { assert(0 && "protection loading must not retry Config initialization"); return 0; }
int bms_config_store_get_protect(bms_protection_params_t *p) { note('G'); memset(p,0,sizeof(*p)); return failed_domain!=1u; }
void bms_config_store_get_default_protect(bms_protection_params_t *p) { note('D'); memset(p,0,sizeof(*p)); }
int bms_config_store_set_protect(const bms_protection_params_t *p) { (void)p; return 1; }
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
    return 0;
}
