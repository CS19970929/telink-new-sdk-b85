#include "bms_afe_hw_profile.h"
#include "bms_afe_backend.h"
#include "bms_cold_kv_store.h"
#include <string.h>

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "dvc1124_project_config.h"
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
#include "sh3673510_project_config.h"
#endif

static u16 ms10_to_ms(u16 filter_10ms)
{
    u32 ms = (u32)filter_10ms * 10u;
    return (u16)((ms > 65535u) ? 65535u : ms);
}

u16 bms_afe_hw_profile_expected_model(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    return BMS_AFE_HW_MODEL_DVC1124;
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    return BMS_AFE_HW_MODEL_SH3673510;
#else
    return 0u;
#endif
}

u16 bms_afe_hw_profile_capabilities(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    return (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                 BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                 BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_OCC2 |
                 BMS_AFE_HW_EN_SC);
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    return (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                 BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                 BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_SC |
                 BMS_AFE_HW_EN_TEMP);
#else
    return 0u;
#endif
}

static u16 sh_sc_multiplier(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    static const u16 values[4] = {2u, 3u, 4u, 6u};
    u8 code = SH3673510_D011_SC_MULTIPLIER_CODE;
    return (code < 4u) ? values[code] : 2u;
#else
    return 0u;
#endif
}

static u16 sh_sc_delay_us(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    static const u16 values[8] = {2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u};
    u8 code = SH3673510_D011_SC_DELAY_CODE;
    return (code < 8u) ? values[code] : 256u;
#else
    return 0u;
#endif
}

void bms_afe_hw_profile_build_migration_default(bms_afe_hw_profile_t *p)
{
    const struct PRT_E2ROM_PARAS *s = &g_tParam.protect;
    if (p == 0) return;
    memset(p, 0, sizeof(*p));
    p->schema_version = BMS_AFE_HW_PROFILE_SCHEMA_VERSION;
    p->afe_model = bms_afe_hw_profile_expected_model();
    p->cov_mv = s->u16VcellOvp_Third;
    p->cov_delay_ms = ms10_to_ms(s->u16VcellOvp_Filter);
    p->cov_recover_mv = s->u16VcellOvp_Rcv;
    p->cov_recover_ms = ms10_to_ms(s->u16VcellOvp_Filter);
    p->cuv_mv = s->u16VcellUvp_Third;
    p->cuv_delay_ms = ms10_to_ms(s->u16VcellUvp_Filter);
    p->cuv_recover_mv = s->u16VcellUvp_Rcv;
    p->cuv_recover_ms = ms10_to_ms(s->u16VcellUvp_Filter);
    p->ocd1_a10 = s->u16IdsgOcp_First;
    p->ocd1_delay_ms = ms10_to_ms(s->u16IdsgOcp_Filter);
    p->ocd2_a10 = s->u16IdsgOcp_Second;
    p->ocd2_delay_ms = ms10_to_ms(s->u16IdsgOcp_Filter);
    p->ocd_recover_a10 = s->u16IdsgOcp_Rcv;
    p->occ1_a10 = s->u16IchgOcp_First;
    p->occ1_delay_ms = ms10_to_ms(s->u16IchgOcp_Filter);
    p->occ2_a10 = s->u16IchgOcp_Second;
    p->occ2_delay_ms = ms10_to_ms(s->u16IchgOcp_Filter);
    p->occ_recover_a10 = s->u16IchgOcp_Rcv;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    p->ocd_recover_ms = 0u; /* preserve legacy one-sample DVC latch release */
    p->occ_recover_ms = 0u;
    if (DVC1124_HW_SCD_THRESHOLD_MV != 0u && DVC1124_DEFAULT_SHUNT_UOHM != 0u)
        p->sc_a10 = (u16)(((u32)DVC1124_HW_SCD_THRESHOLD_MV * 10000u) /
                          DVC1124_DEFAULT_SHUNT_UOHM);
    p->sc_delay_us = DVC1124_HW_SCD_DELAY_US;
    p->sc_recover_ms = 0u; /* SCD remains backend-latched until load release is validated */
    p->enable_mask = (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                           BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                           BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_OCC2);
    if (p->sc_a10 != 0u) p->enable_mask |= BMS_AFE_HW_EN_SC;
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    p->ocd_recover_ms = 2000u;
    p->occ_recover_ms = ms10_to_ms(s->u16IchgOcp_Filter);
    p->sc_a10 = (u16)((u32)p->ocd2_a10 * sh_sc_multiplier());
    p->sc_delay_us = sh_sc_delay_us();
    p->sc_recover_ms = 2000u;
    p->chg_ot_x10 = s->u16TChgOTp_Third;
    p->chg_ot_recover_x10 = s->u16TChgOTp_Rcv;
    p->chg_ut_x10 = s->u16TchgUTp_Third;
    p->chg_ut_recover_x10 = s->u16TchgUTp_Rcv;
    p->dsg_ot_x10 = s->u16TdischgOTp_Third;
    p->dsg_ot_recover_x10 = s->u16TdischgOTp_Rcv;
    p->dsg_ut_x10 = s->u16TdischgUTp_Third;
    p->dsg_ut_recover_x10 = s->u16TdischgUTp_Rcv;
    p->temp_recover_ms = ms10_to_ms(s->u16TChgOTp_Filter);
    p->enable_mask = (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                           BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                           BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_SC |
                           BMS_AFE_HW_EN_TEMP);
#endif
}

static u8 validate_hysteresis(const bms_afe_hw_profile_t *p)
{
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) &&
        (p->cov_mv == 0u || p->cov_recover_mv >= p->cov_mv)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) &&
        (p->cuv_mv == 0u || p->cuv_recover_mv <= p->cuv_mv)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) &&
        (p->ocd1_a10 == 0u || p->ocd_recover_a10 >= p->ocd1_a10)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) &&
        (p->ocd2_a10 == 0u || p->ocd_recover_a10 >= p->ocd2_a10)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) &&
        (p->occ1_a10 == 0u || p->occ_recover_a10 >= p->occ1_a10)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC2) &&
        (p->occ2_a10 == 0u || p->occ_recover_a10 >= p->occ2_a10)) return 0u;
    if (p->enable_mask & BMS_AFE_HW_EN_TEMP) {
        if (p->chg_ot_recover_x10 >= p->chg_ot_x10 ||
            p->dsg_ot_recover_x10 >= p->dsg_ot_x10 ||
            p->chg_ut_recover_x10 <= p->chg_ut_x10 ||
            p->dsg_ut_recover_x10 <= p->dsg_ut_x10) return 0u;
    }
    return 1u;
}

u8 bms_afe_hw_profile_validate(const bms_afe_hw_profile_t *p)
{
    u32 sense_uv;
    if (p == 0) return 0u;
    if (p->schema_version != BMS_AFE_HW_PROFILE_SCHEMA_VERSION) return 0u;
    if (p->afe_model != bms_afe_hw_profile_expected_model()) return 0u;
    if ((p->enable_mask & (u16)~bms_afe_hw_profile_capabilities()) != 0u) return 0u;
    if (!validate_hysteresis(p)) return 0u;
    if (p->chg_ot_x10 > 1450u || p->chg_ot_recover_x10 > 1450u ||
        p->chg_ut_x10 > 1450u || p->chg_ut_recover_x10 > 1450u ||
        p->dsg_ot_x10 > 1450u || p->dsg_ot_recover_x10 > 1450u ||
        p->dsg_ut_x10 > 1450u || p->dsg_ut_recover_x10 > 1450u) return 0u;

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) && (p->cov_mv < 501u || p->cov_mv > 4595u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) && p->cuv_mv > 4095u) return 0u;
    if (p->cov_delay_ms > 8000u || p->cuv_delay_ms > 8000u ||
        p->ocd1_delay_ms > 2048u || p->occ1_delay_ms > 2048u ||
        p->ocd2_delay_ms > 1024u || p->occ2_delay_ms > 1024u) return 0u;
    sense_uv = ((u32)p->ocd1_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) && sense_uv > 63750u) return 0u;
    sense_uv = ((u32)p->occ1_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) && sense_uv > 63750u) return 0u;
    sense_uv = ((u32)p->ocd2_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) && sense_uv > 256000u) return 0u;
    sense_uv = ((u32)p->occ2_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC2) && sense_uv > 256000u) return 0u;
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) && (p->cov_mv == 0u || p->cov_mv > 5115u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) && (p->cuv_mv == 0u || p->cuv_mv > 5115u)) return 0u;
    if (p->cov_delay_ms > 10010u || p->cuv_delay_ms > 10010u ||
        p->ocd1_delay_ms > 10010u || p->occ1_delay_ms > 10010u ||
        p->ocd2_delay_ms > 400u || p->sc_delay_us > 256u) return 0u;
    sense_uv = ((u32)p->ocd1_a10 * SH3673510_D011_SHUNT_UOHM + 5u) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) && sense_uv > 80000u) return 0u;
    sense_uv = ((u32)p->ocd2_a10 * SH3673510_D011_SHUNT_UOHM + 5u) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) && sense_uv > 160000u) return 0u;
    sense_uv = ((u32)p->occ1_a10 * SH3673510_D011_SHUNT_UOHM + 5u) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) && sense_uv > 44000u) return 0u;
#endif
    return 1u;
}

u8 bms_afe_hw_profile_init(void)
{
    bms_afe_hw_profile_t p;
    if (!bms_cold_kv_store_get_afe_hw_profile(&p)) return 0u;
    if (p.schema_version == 0u && p.afe_model == 0u) {
        bms_afe_hw_profile_build_migration_default(&p);
        if (!bms_afe_hw_profile_validate(&p)) return 0u;
        return bms_cold_kv_store_set_afe_hw_profile(&p) ? 1u : 0u;
    }
    return bms_afe_hw_profile_validate(&p);
}

u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *p)
{
    if (p == 0) return 0u;
    if (!bms_afe_hw_profile_init()) return 0u;
    if (!bms_cold_kv_store_get_afe_hw_profile(p)) return 0u;
    return bms_afe_hw_profile_validate(p);
}

u8 bms_afe_hw_profile_set(const bms_afe_hw_profile_t *p)
{
    if (!bms_afe_hw_profile_validate(p)) return 0u;
    return bms_cold_kv_store_set_afe_hw_profile(p) ? 1u : 0u;
}
