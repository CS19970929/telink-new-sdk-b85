#ifndef BMS_AFE_HW_PROFILE_H_
#define BMS_AFE_HW_PROFILE_H_

#include "tl_common.h"
#include "param.h"

#define BMS_AFE_HW_PROFILE_SCHEMA_VERSION 1u
#define BMS_AFE_HW_MODEL_DVC1124          0x1124u
#define BMS_AFE_HW_MODEL_SH3673510        0x3510u

#define BMS_AFE_HW_EN_COV   (1u << 0)
#define BMS_AFE_HW_EN_CUV   (1u << 1)
#define BMS_AFE_HW_EN_OCD1  (1u << 2)
#define BMS_AFE_HW_EN_OCD2  (1u << 3)
#define BMS_AFE_HW_EN_OCC1  (1u << 4)
#define BMS_AFE_HW_EN_OCC2  (1u << 5)
#define BMS_AFE_HW_EN_SC    (1u << 6)
#define BMS_AFE_HW_EN_TEMP  (1u << 7)

typedef struct
{
    u16 schema_version;
    u16 afe_model;
    u16 cov_mv;
    u16 cov_delay_ms;
    u16 cov_recover_mv;
    u16 cov_recover_ms;
    u16 cuv_mv;
    u16 cuv_delay_ms;
    u16 cuv_recover_mv;
    u16 cuv_recover_ms;
    u16 ocd1_a10;
    u16 ocd1_delay_ms;
    u16 ocd2_a10;
    u16 ocd2_delay_ms;
    u16 ocd_recover_a10;
    u16 ocd_recover_ms;
    u16 occ1_a10;
    u16 occ1_delay_ms;
    u16 occ2_a10;
    u16 occ2_delay_ms;
    u16 occ_recover_a10;
    u16 occ_recover_ms;
    u16 sc_a10;
    u16 sc_delay_us;
    u16 sc_recover_ms;
    u16 chg_ot_x10;
    u16 chg_ot_recover_x10;
    u16 chg_ut_x10;
    u16 chg_ut_recover_x10;
    u16 dsg_ot_x10;
    u16 dsg_ot_recover_x10;
    u16 dsg_ut_x10;
    u16 dsg_ut_recover_x10;
    u16 temp_recover_ms;
    u16 enable_mask;
} bms_afe_hw_profile_t;

#define BMS_AFE_HW_PROFILE_WORD_COUNT ((u16)(sizeof(bms_afe_hw_profile_t) / sizeof(u16)))

typedef char bms_afe_hw_profile_word_layout_must_be_35[
    (sizeof(bms_afe_hw_profile_t) == (35u * sizeof(u16))) ? 1 : -1];

u16 bms_afe_hw_profile_expected_model(void);
u16 bms_afe_hw_profile_capabilities(void);
u8 bms_afe_hw_profile_validate(const bms_afe_hw_profile_t *profile);
u8 bms_afe_hw_profile_init(void);
u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *profile);
u8 bms_afe_hw_profile_set(const bms_afe_hw_profile_t *profile);
/* Returns the values actually represented by the active AFE after quantization. */
u8 bms_afe_hw_profile_get_effective(bms_afe_hw_profile_t *profile);
void bms_afe_hw_profile_build_migration_default(bms_afe_hw_profile_t *profile);

#endif /* BMS_AFE_HW_PROFILE_H_ */
