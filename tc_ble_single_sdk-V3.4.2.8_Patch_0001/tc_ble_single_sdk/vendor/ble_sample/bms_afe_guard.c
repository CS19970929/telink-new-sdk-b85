#include "bms_afe.h"
#include "bms_features.h"
#include <string.h>

#define BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT 3u
#define BMS_AFE_REINIT_TRIGGER 3u
#define BMS_AFE_REINIT_COOLDOWN_SAMPLES 25u

typedef struct { uint8_t requested_charge_on, requested_discharge_on, output_enabled, comm_inhibit, valid_snapshot_streak, comm_failures, reinit_cooldown; } bms_afe_guard_state_t;
static bms_afe_guard_state_t s_guard;

#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
#define AFE_INIT() dvc1124_backend_init()
#define AFE_SAMPLE() dvc1124_backend_sample()
#define AFE_SLEEP() dvc1124_backend_sleep()
#define AFE_APPLY() dvc1124_backend_apply_protection_config()
#define AFE_FETS(c,d) dvc1124_backend_set_fets((c),(d))
#define AFE_OUTPUT(e) dvc1124_backend_set_output_enabled((e))
#define AFE_AUX(m) dvc1124_backend_get_aux_measurements((m))
#define AFE_FEATURE(s) dvc1124_backend_get_feature_snapshot((s))
#define AFE_BAL_SET(m) dvc1124_backend_set_balance_mask((m))
#define AFE_BAL_GET(m) dvc1124_backend_get_balance_mask((m))
#define AFE_OW_START() dvc1124_backend_openwire_start()
#define AFE_OW_POLL(r) dvc1124_backend_openwire_poll((r))
#else
#define AFE_INIT() sh3673510_bms_afe_init()
#define AFE_SAMPLE() sh3673510_bms_afe_sample()
#define AFE_SLEEP() sh3673510_bms_afe_sleep()
#define AFE_APPLY() sh3673510_bms_afe_apply_protection_config()
#define AFE_FETS(c,d) sh3673510_bms_afe_set_fets((c),(d))
#define AFE_OUTPUT(e) sh3673510_bms_afe_set_output_enabled((e))
#define AFE_AUX(m) sh3673510_bms_afe_get_aux_measurements((m))
#define AFE_FEATURE(s) sh3673510_backend_get_feature_snapshot((s))
#define AFE_BAL_SET(m) sh3673510_backend_set_balance_mask((m))
#define AFE_BAL_GET(m) sh3673510_backend_get_balance_mask((m))
#define AFE_OW_START() sh3673510_backend_openwire_start()
#define AFE_OW_POLL(r) sh3673510_backend_openwire_poll((r))
#endif

static uint8_t apply_requested(void)
{
    uint8_t c=s_guard.requested_charge_on,d=s_guard.requested_discharge_on;
    if(!s_guard.output_enabled||s_guard.comm_inhibit){c=0u;d=0u;}
    if(bms_features_charge_blocked())c=0u;
    if(bms_features_discharge_blocked())d=0u;
    return AFE_FETS(c,d);
}
static void inhibit(void){s_guard.comm_inhibit=1u;s_guard.valid_snapshot_streak=0u;bms_features_on_afe_invalid();(void)AFE_BAL_SET(0u);(void)AFE_FETS(0u,0u);}
static void note_invalid(void){inhibit();if(s_guard.comm_failures!=0xFFu)++s_guard.comm_failures;if(s_guard.comm_failures>=BMS_AFE_REINIT_TRIGGER&&s_guard.reinit_cooldown==0u){s_guard.reinit_cooldown=BMS_AFE_REINIT_COOLDOWN_SAMPLES;s_guard.comm_failures=0u;AFE_INIT();AFE_OUTPUT(s_guard.output_enabled);(void)AFE_FETS(0u,0u);}}
void bms_afe_init(void){memset(&s_guard,0,sizeof(s_guard));s_guard.comm_inhibit=1u;AFE_INIT();AFE_OUTPUT(0u);(void)AFE_FETS(0u,0u);bms_features_init();(void)AFE_BAL_SET(0u);}
void bms_afe_sample(void){bms_afe_aux_measurements_t m;if(s_guard.reinit_cooldown)--s_guard.reinit_cooldown;AFE_SAMPLE();memset(&m,0,sizeof(m));if(!AFE_AUX(&m)){note_invalid();return;}s_guard.comm_failures=0u;if(s_guard.valid_snapshot_streak<BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT)++s_guard.valid_snapshot_streak;if(s_guard.valid_snapshot_streak>=BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT)s_guard.comm_inhibit=0u;bms_features_service();if(!apply_requested())note_invalid();}
void bms_afe_sleep(void){inhibit();s_guard.comm_failures=0u;AFE_SLEEP();}
uint8_t bms_afe_apply_protection_config(void){uint8_t ok=AFE_APPLY();if(!ok)note_invalid();return ok;}
uint8_t bms_afe_set_fets(uint8_t c,uint8_t d){s_guard.requested_charge_on=c?1u:0u;s_guard.requested_discharge_on=d?1u:0u;if(!apply_requested()){note_invalid();return 0u;}return 1u;}
void bms_afe_set_output_enabled(uint8_t e){s_guard.output_enabled=e?1u:0u;AFE_OUTPUT(s_guard.output_enabled);if(!s_guard.output_enabled){bms_features_on_afe_invalid();(void)AFE_BAL_SET(0u);(void)AFE_FETS(0u,0u);return;}if(!apply_requested())note_invalid();}
uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m){if(!m)return 0u;if(!AFE_AUX(m)){memset(m,0,sizeof(*m));return 0u;}return 1u;}
uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *s){if(!s||s_guard.comm_inhibit)return 0u;return AFE_FEATURE(s);}
uint8_t bms_afe_set_balance_mask(uint32_t m){if(m&&s_guard.comm_inhibit)return 0u;return AFE_BAL_SET(m);}
uint8_t bms_afe_get_balance_mask(uint32_t *m){return AFE_BAL_GET(m);}
uint8_t bms_afe_openwire_start(void){if(s_guard.comm_inhibit)return 0u;(void)AFE_BAL_SET(0u);if(!AFE_FETS(0u,0u))return 0u;return AFE_OW_START();}
bms_afe_diag_state_t bms_afe_openwire_poll(bms_afe_openwire_result_t *r){if(s_guard.comm_inhibit)return BMS_AFE_DIAG_ERROR;return AFE_OW_POLL(r);}
