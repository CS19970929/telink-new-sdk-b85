#include "bms_afe_hw_profile.h"
#include "bms_afe_backend.h"
#include "bms_cold_kv_store.h"
#include <string.h>

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "dvc1124_project_config.h"
#include "dvc1124_config_service.h"
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#endif

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
static u16 ms10_to_ms(u16 filter_10ms)
{
    u32 ms = (u32)filter_10ms * 10u;
    return (u16)((ms > 65535u) ? 65535u : ms);
}
#endif

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

void bms_afe_hw_profile_build_default(bms_afe_hw_profile_t *p)
{
    if (p == 0) return;
    memset(p, 0, sizeof(*p));
    p->schema_version = BMS_AFE_HW_PROFILE_SCHEMA_VERSION;
    p->afe_model = bms_afe_hw_profile_expected_model();

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    {
        const struct PRT_E2ROM_PARAS *s = &g_tParam.protect;
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
        p->ocd_recover_ms = 0u;
        p->occ1_a10 = s->u16IchgOcp_First;
        p->occ1_delay_ms = ms10_to_ms(s->u16IchgOcp_Filter);
        p->occ2_a10 = s->u16IchgOcp_Second;
        p->occ2_delay_ms = ms10_to_ms(s->u16IchgOcp_Filter);
        p->occ_recover_a10 = s->u16IchgOcp_Rcv;
        p->occ_recover_ms = 0u;
        if (DVC1124_HW_SCD_THRESHOLD_MV != 0u && DVC1124_DEFAULT_SHUNT_UOHM != 0u)
            p->sc_a10 = (u16)(((u32)DVC1124_HW_SCD_THRESHOLD_MV * 10000u) /
                              DVC1124_DEFAULT_SHUNT_UOHM);
        p->sc_delay_us = DVC1124_HW_SCD_DELAY_US;
        p->sc_recover_ms = 0u;
        p->enable_mask = (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                               BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                               BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_OCC2);
        if (p->sc_a10 != 0u) p->enable_mask |= BMS_AFE_HW_EN_SC;
    }
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    /*
     * D014 hardware-protection defaults are product configuration, not a copy
     * of the software First/Second/Third protection table.
     */
    p->cov_mv = SH3673510_HW_DEFAULT_COV_MV;
    p->cov_delay_ms = SH3673510_HW_DEFAULT_COV_DELAY_MS;
    p->cov_recover_mv = SH3673510_HW_DEFAULT_COV_RECOVER_MV;
    p->cov_recover_ms = SH3673510_HW_DEFAULT_COV_RECOVER_MS;
    p->cuv_mv = SH3673510_HW_DEFAULT_CUV_MV;
    p->cuv_delay_ms = SH3673510_HW_DEFAULT_CUV_DELAY_MS;
    p->cuv_recover_mv = SH3673510_HW_DEFAULT_CUV_RECOVER_MV;
    p->cuv_recover_ms = SH3673510_HW_DEFAULT_CUV_RECOVER_MS;
    p->ocd1_a10 = SH3673510_HW_DEFAULT_OCD1_A10;
    p->ocd1_delay_ms = SH3673510_HW_DEFAULT_OCD1_DELAY_MS;
    p->ocd2_a10 = SH3673510_HW_DEFAULT_OCD2_A10;
    p->ocd2_delay_ms = SH3673510_HW_DEFAULT_OCD2_DELAY_MS;
    p->ocd_recover_a10 = SH3673510_HW_DEFAULT_OCD_RECOVER_A10;
    p->ocd_recover_ms = SH3673510_HW_DEFAULT_OCD_RECOVER_MS;
    p->occ1_a10 = SH3673510_HW_DEFAULT_OCC1_A10;
    p->occ1_delay_ms = SH3673510_HW_DEFAULT_OCC1_DELAY_MS;
    p->occ2_a10 = 0u;
    p->occ2_delay_ms = 0u;
    p->occ_recover_a10 = SH3673510_HW_DEFAULT_OCC_RECOVER_A10;
    p->occ_recover_ms = SH3673510_HW_DEFAULT_OCC_RECOVER_MS;
    p->sc_a10 = SH3673510_HW_DEFAULT_SC_A10;
    p->sc_delay_us = SH3673510_HW_DEFAULT_SC_DELAY_US;
    p->sc_recover_ms = SH3673510_HW_DEFAULT_SC_RECOVER_MS;
    p->chg_ot_x10 = SH3673510_HW_DEFAULT_CHG_OT_X10;
    p->chg_ot_recover_x10 = SH3673510_HW_DEFAULT_CHG_OT_RECOVER_X10;
    p->chg_ut_x10 = SH3673510_HW_DEFAULT_CHG_UT_X10;
    p->chg_ut_recover_x10 = SH3673510_HW_DEFAULT_CHG_UT_RECOVER_X10;
    p->dsg_ot_x10 = SH3673510_HW_DEFAULT_DSG_OT_X10;
    p->dsg_ot_recover_x10 = SH3673510_HW_DEFAULT_DSG_OT_RECOVER_X10;
    p->dsg_ut_x10 = SH3673510_HW_DEFAULT_DSG_UT_X10;
    p->dsg_ut_recover_x10 = SH3673510_HW_DEFAULT_DSG_UT_RECOVER_X10;
    p->temp_recover_ms = SH3673510_HW_DEFAULT_TEMP_RECOVER_MS;
    p->enable_mask = (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                           BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                           BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_SC |
                           BMS_AFE_HW_EN_TEMP);
#endif
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
static u16 sh3510_effective_current_a10(u16 requested_a10, u32 step_uv, u8 max_code)
{
    u32 sense_uv;
    u32 steps;
    u32 actual_uv;
    u32 actual_a10;

    if ((step_uv == 0u) || (SH3673510_D011_SHUNT_UOHM == 0u)) return 0u;
    sense_uv = ((u32)requested_a10 * SH3673510_D011_SHUNT_UOHM + 5u) / 10u;
    steps = (sense_uv + step_uv - 1u) / step_uv;
    if (steps == 0u) steps = 1u;
    if (steps > (u32)max_code + 1u) steps = (u32)max_code + 1u;
    actual_uv = steps * step_uv;
    actual_a10 = (actual_uv * 10u + SH3673510_D011_SHUNT_UOHM - 1u) /
                 SH3673510_D011_SHUNT_UOHM;
    return (u16)((actual_a10 > 65535u) ? 65535u : actual_a10);
}
#endif

static u8 validate_hysteresis(const bms_afe_hw_profile_t *p)
{
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) &&
        (p->cov_mv == 0u || p->cov_recover_mv >= p->cov_mv)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) &&
        (p->cuv_mv == 0u || p->cuv_recover_mv <= p->cuv_mv)) return 0u;

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    /*
     * Recovery must be below the threshold the AFE can actually encode.
     * Comparing against the requested value is wrong when the AFE rounds a
     * request upward to the next hardware step (D014 OCD1/OCC1 hit this case).
     */
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) &&
        (p->ocd1_a10 == 0u ||
         p->ocd_recover_a10 >= sh3510_effective_current_a10(p->ocd1_a10, 5000u, 15u))) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) &&
        (p->ocd2_a10 == 0u ||
         p->ocd_recover_a10 >= sh3510_effective_current_a10(p->ocd2_a10, 10000u, 15u))) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) &&
        (p->occ1_a10 == 0u ||
         p->occ_recover_a10 >= sh3510_effective_current_a10(p->occ1_a10, 1375u, 31u))) return 0u;
#else
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) &&
        (p->ocd1_a10 == 0u || p->ocd_recover_a10 >= p->ocd1_a10)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) &&
        (p->ocd2_a10 == 0u || p->ocd_recover_a10 >= p->ocd2_a10)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) &&
        (p->occ1_a10 == 0u || p->occ_recover_a10 >= p->occ1_a10)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC2) &&
        (p->occ2_a10 == 0u || p->occ_recover_a10 >= p->occ2_a10)) return 0u;
#endif

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
    sense_uv = ((u32)p->sc_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_SC) &&
        (sense_uv < 10000u || sense_uv > 630000u)) return 0u;
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
    if (p.schema_version != BMS_AFE_HW_PROFILE_SCHEMA_VERSION ||
        p.afe_model != bms_afe_hw_profile_expected_model()) {
        bms_afe_hw_profile_build_default(&p);
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

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
static u8 dvc_effective_u16(dvc1124_config_field_t field, u16 *out)
{
    u32 value;
    if (out == 0) return 0u;
    if (DVC1124_ConfigServiceRead(field, &value) != DVC1124_CFG_OK || value > 65535u) return 0u;
    *out = (u16)value;
    return 1u;
}
#endif

u8 bms_afe_hw_profile_get_effective(bms_afe_hw_profile_t *p)
{
    bms_afe_hw_profile_t requested;
    if (p == 0 || !bms_afe_hw_profile_get(&requested)) return 0u;
    *p = requested;

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    {
        u16 sc_mv = 0u;
        if (!dvc_effective_u16(DVC1124_CFG_EFF_COV_MV, &p->cov_mv) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_COV_DELAY_MS, &p->cov_delay_ms) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_CUV_MV, &p->cuv_mv) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_CUV_DELAY_MS, &p->cuv_delay_ms) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_OCD1_X10A, &p->ocd1_a10) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_OCD1_DELAY_MS, &p->ocd1_delay_ms) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_OCC1_X10A, &p->occ1_a10) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_OCC1_DELAY_MS, &p->occ1_delay_ms) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_OCD2_X10A, &p->ocd2_a10) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_OCD2_DELAY_MS, &p->ocd2_delay_ms) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_OCC2_X10A, &p->occ2_a10) ||
            !dvc_effective_u16(DVC1124_CFG_EFF_OCC2_DELAY_MS, &p->occ2_delay_ms)) return 0u;
        if (requested.enable_mask & BMS_AFE_HW_EN_SC) {
            if (!dvc_effective_u16(DVC1124_CFG_EFF_SCD_MV, &sc_mv) ||
                !dvc_effective_u16(DVC1124_CFG_EFF_SCD_DELAY_US, &p->sc_delay_us)) return 0u;
            if (DVC1124_DEFAULT_SHUNT_UOHM == 0u) return 0u;
            p->sc_a10 = (u16)(((u32)sc_mv * 10000u) / DVC1124_DEFAULT_SHUNT_UOHM);
        }
    }
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    {
        sh3673510_protection_actual_t actual;
        static const u16 sc_mult[4] = {2u, 3u, 4u, 6u};
        static const u16 sc_delay[8] = {2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u};
        u8 mult_code = 0u;
        u8 delay_code = 0u;
        if (!sh3673510_control_get_protection_actual(&actual)) return 0u;
        p->cov_mv = actual.ov_mv;
        p->cuv_mv = actual.uv_mv;
        p->ocd1_a10 = actual.ocd1_a10;
        p->ocd2_a10 = actual.ocd2_a10;
        p->occ1_a10 = actual.occ_a10;
        p->cov_delay_ms = actual.ov_delay_ms;
        p->cuv_delay_ms = actual.uv_delay_ms;
        p->ocd1_delay_ms = actual.ocd1_delay_ms;
        p->ocd2_delay_ms = actual.ocd2_delay_ms;
        p->occ1_delay_ms = actual.occ_delay_ms;
        if (requested.enable_mask & BMS_AFE_HW_EN_SC) {
            while (mult_code < 3u && (u32)actual.ocd2_a10 * sc_mult[mult_code] < requested.sc_a10) ++mult_code;
            while (delay_code < 7u && sc_delay[delay_code] < requested.sc_delay_us) ++delay_code;
            p->sc_a10 = (u16)((u32)actual.ocd2_a10 * sc_mult[mult_code]);
            p->sc_delay_us = sc_delay[delay_code];
        }
    }
#endif
    return 1u;
}
