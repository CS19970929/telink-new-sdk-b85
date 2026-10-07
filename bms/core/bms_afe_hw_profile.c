/*
 * 文件功能：独立 AFE 硬件保护参数；负责默认值、校验、持久化和实际量化值，
 * 不代替软件三级保护。
 * bms/core/bms_afe_hw_profile.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_afe_hw_profile.h"
#include "bms_afe.h"
#include "bms_afe_hw_access.h"
#include "bms_afe_backend.h"
#include "bms_config_store.h"
#include <string.h>

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "bms_product.h"
#include "dvc1124_config_service.h"
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#include "sh3673510_quantize.h"
#endif

/* 将 10 ms 单位延时换算为毫秒。 */
static u16 ms10_to_ms(u16 filter_10ms)
{
    u32 ms = (u32)filter_10ms * 10u;
    return (u16)((ms > 65535u) ? 65535u : ms);
}

/* 取得当前产品要求的 AFE 型号标识。 */
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

/* 取得当前 AFE 硬件保护能力掩码。 */
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

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
/* 将硬件保护值限制到指定 16 位上限。 */
static u16 dvc_clamp_u16_max(u16 value, u16 max_value)
{
    return (value > max_value) ? max_value : value;
}

/* 从启用的保护级别中选择最小触发值。 */
static u16 dvc_min_enabled_trip(u16 enable_mask,
                                u16 bit1, u16 value1,
                                u16 bit2, u16 value2)
{
    u16 min_value = 0u;

    if ((enable_mask & bit1) && value1 != 0u) min_value = value1;
    if ((enable_mask & bit2) && value2 != 0u && (min_value == 0u || value2 < min_value))
        min_value = value2;
    return min_value;
}

/*
 * 将编译默认种子限制到既有 DVC 可表示范围。客户写入始终严格校验，不归一化；
 * 此处不用旧 Flash 或运行时软件保护值。
 */
static void dvc_normalize_default_profile(bms_afe_hw_profile_t *p)
{
    u16 min_trip;
    u32 max_a10;

    if (p == 0) return;

    if (p->cov_mv == 0u) {
        p->enable_mask &= (u16)~BMS_AFE_HW_EN_COV;
    } else {
        if (p->cov_mv < 501u) p->cov_mv = 501u;
        if (p->cov_mv > 4595u) p->cov_mv = 4595u;
        if (p->cov_recover_mv >= p->cov_mv)
            p->cov_recover_mv = (u16)(p->cov_mv - 1u);
    }

    if (p->cuv_mv == 0u) {
        p->enable_mask &= (u16)~BMS_AFE_HW_EN_CUV;
    } else {
        if (p->cuv_mv > 4095u) p->cuv_mv = 4095u;
        if (p->cuv_recover_mv <= p->cuv_mv)
            p->cuv_recover_mv = (p->cuv_mv < 65535u) ? (u16)(p->cuv_mv + 1u) : p->cuv_mv;
    }

    p->cov_delay_ms = dvc_clamp_u16_max(p->cov_delay_ms, 8000u);
    p->cuv_delay_ms = dvc_clamp_u16_max(p->cuv_delay_ms, 8000u);
    p->ocd1_delay_ms = dvc_clamp_u16_max(p->ocd1_delay_ms, 2048u);
    p->occ1_delay_ms = dvc_clamp_u16_max(p->occ1_delay_ms, 2048u);
    p->ocd2_delay_ms = dvc_clamp_u16_max(p->ocd2_delay_ms, 1024u);
    p->occ2_delay_ms = dvc_clamp_u16_max(p->occ2_delay_ms, 1024u);

    if (DVC1124_DEFAULT_SHUNT_UOHM != 0u) {
        max_a10 = (63750u * 10u) / DVC1124_DEFAULT_SHUNT_UOHM;
        if ((u32)p->ocd1_a10 > max_a10) p->ocd1_a10 = (u16)max_a10;
        if ((u32)p->occ1_a10 > max_a10) p->occ1_a10 = (u16)max_a10;

        max_a10 = (256000u * 10u) / DVC1124_DEFAULT_SHUNT_UOHM;
        if ((u32)p->ocd2_a10 > max_a10) p->ocd2_a10 = (u16)max_a10;
        if ((u32)p->occ2_a10 > max_a10) p->occ2_a10 = (u16)max_a10;
    }

    if (p->ocd1_a10 == 0u) p->enable_mask &= (u16)~BMS_AFE_HW_EN_OCD1;
    if (p->ocd2_a10 == 0u) p->enable_mask &= (u16)~BMS_AFE_HW_EN_OCD2;
    if (p->occ1_a10 == 0u) p->enable_mask &= (u16)~BMS_AFE_HW_EN_OCC1;
    if (p->occ2_a10 == 0u) p->enable_mask &= (u16)~BMS_AFE_HW_EN_OCC2;

    min_trip = dvc_min_enabled_trip(p->enable_mask,
                                    BMS_AFE_HW_EN_OCD1, p->ocd1_a10,
                                    BMS_AFE_HW_EN_OCD2, p->ocd2_a10);
    if (min_trip != 0u && p->ocd_recover_a10 >= min_trip)
        p->ocd_recover_a10 = (u16)(min_trip - 1u);

    min_trip = dvc_min_enabled_trip(p->enable_mask,
                                    BMS_AFE_HW_EN_OCC1, p->occ1_a10,
                                    BMS_AFE_HW_EN_OCC2, p->occ2_a10);
    if (min_trip != 0u && p->occ_recover_a10 >= min_trip)
        p->occ_recover_a10 = (u16)(min_trip - 1u);
}
#endif

/* 按产品输入构造独立 AFE 硬件保护默认值。 */
void bms_afe_hw_profile_build_default(bms_afe_hw_profile_t *p)
{
    bms_protection_params_t defaults;
    const bms_protection_params_t *s = &defaults;
    if (p == 0) return;
    bms_config_store_get_default_protect(&defaults);
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
    p->ocd_recover_ms = 0u;
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
    dvc_normalize_default_profile(p);
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    /* D014 硬件保护默认值是产品配置，不复制软件 First/Second/Third 保护表。 */
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

/* 验证保护触发与恢复阈值的回差关系。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
#if defined(__GNUC__)
static u8 validate_hysteresis(const bms_afe_hw_profile_t *p) __attribute__((noinline));
#endif
static u8 validate_hysteresis(const bms_afe_hw_profile_t *p)
{
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) &&
        (p->cov_mv == 0u || p->cov_recover_mv >= p->cov_mv)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) &&
        (p->cuv_mv == 0u || p->cuv_recover_mv <= p->cuv_mv)) return 0u;

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    /*
     * 恢复值须低于 AFE 实际编码阈值。AFE 向上量化到下一步进时，与请求值比较会出错，
     * D014 OCD1/OCC1 即有此情况。
     */
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) &&
        (p->ocd1_a10 == 0u ||
         p->ocd_recover_a10 >= sh3673510_quantize_current_a10(p->ocd1_a10,
             SH3673510_BOARD_SHUNT_UOHM, 5000u, 15u, 0))) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) &&
        (p->ocd2_a10 == 0u ||
         p->ocd_recover_a10 >= sh3673510_quantize_current_a10(p->ocd2_a10,
             SH3673510_BOARD_SHUNT_UOHM, 10000u, 15u, 0))) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) &&
        (p->occ1_a10 == 0u ||
         p->occ_recover_a10 >= sh3673510_quantize_current_a10(p->occ1_a10,
             SH3673510_BOARD_SHUNT_UOHM, 1375u, 31u, 0))) return 0u;
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

/* 校验型号、能力、阈值和延时范围。 */
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
        p->ocd2_delay_ms > 400u || p->sc_delay_us > 576u) return 0u;
    sense_uv = ((u32)p->ocd1_a10 * SH3673510_BOARD_SHUNT_UOHM + 5u) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) && sense_uv > 80000u) return 0u;
    sense_uv = ((u32)p->ocd2_a10 * SH3673510_BOARD_SHUNT_UOHM + 5u) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) && sense_uv > 160000u) return 0u;
    sense_uv = ((u32)p->occ1_a10 * SH3673510_BOARD_SHUNT_UOHM + 5u) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) && sense_uv > 44000u) return 0u;
#endif
    return 1u;
}

/* 新写入按原厂保证范围与最小档位校验；旧参数读取保留原有兼容规则。 */
u8 bms_afe_hw_profile_validate_write(const bms_afe_hw_profile_t *p)
{
    if (!bms_afe_hw_profile_validate(p)) return 0u;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    /* DVC RM V1.2 p18–20：OC1最低250uV、OC2最低4mV，延时200/8/4ms。 */
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) && p->cov_delay_ms < 200u) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) && p->cuv_delay_ms < 200u) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) &&
        ((u32)p->ocd1_a10 * DVC1124_DEFAULT_SHUNT_UOHM < 2500u || p->ocd1_delay_ms < 8u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) &&
        ((u32)p->occ1_a10 * DVC1124_DEFAULT_SHUNT_UOHM < 2500u || p->occ1_delay_ms < 8u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) &&
        ((u32)p->ocd2_a10 * DVC1124_DEFAULT_SHUNT_UOHM < 40000u || p->ocd2_delay_ms < 4u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC2) &&
        ((u32)p->occ2_a10 * DVC1124_DEFAULT_SHUNT_UOHM < 40000u || p->occ2_delay_ms < 4u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_SC) && p->sc_delay_us > 1992u) return 0u;
#else
    u32 pair = p->enable_mask & (BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2);
    /* SH p34：OCD1/2 共用使能；p49给出保证范围，不按位宽扩展工作范围。 */
    if (pair != 0u && pair != (BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) && (p->cov_mv < 3000u || p->cov_mv > 4500u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) && (p->cuv_mv < 1000u || p->cuv_mv > 3500u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) && p->cov_delay_ms < 140u) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) && p->cuv_delay_ms < 490u) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_TEMP) &&
        (p->chg_ot_x10 < 800u || p->chg_ot_x10 > 1100u ||
         p->dsg_ot_x10 < 850u || p->dsg_ot_x10 > 1200u ||
         p->chg_ut_x10 < 200u || p->chg_ut_x10 > 500u || p->dsg_ut_x10 > 500u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) &&
        ((u32)p->ocd1_a10 * SH3673510_BOARD_SHUNT_UOHM < 50000u || p->ocd1_delay_ms < 140u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) &&
        ((u32)p->ocd2_a10 * SH3673510_BOARD_SHUNT_UOHM < 100000u || p->ocd2_delay_ms < 25u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) &&
        ((u32)p->occ1_a10 * SH3673510_BOARD_SHUNT_UOHM < 13750u || p->occ1_delay_ms < 140u)) return 0u;
    if (p->enable_mask & BMS_AFE_HW_EN_SC) {
        u32 sense = (u32)p->ocd2_a10 * SH3673510_BOARD_SHUNT_UOHM;
        u32 base = sh3673510_quantize_current_a10(p->ocd2_a10,
            SH3673510_BOARD_SHUNT_UOHM, 10000u, 15u, 0);
        u32 multiple;
        /* SC 仍依赖 OCD2V，即使 OCD 未使能也必须有可表示的基准。 */
        if (sense < 100000u || sense > 1600000u) return 0u;
        if ((u32)p->sc_a10 < 2u * base || (u32)p->sc_a10 > 6u * base) return 0u;
        multiple = ((u32)p->sc_a10 <= 2u * base) ? 2u :
                   ((u32)p->sc_a10 <= 3u * base) ? 3u :
                   ((u32)p->sc_a10 <= 4u * base) ? 4u : 6u;
        if (base * multiple > 65535u) return 0u;
    }
#endif
    return 1u;
}

/* 取得缓存的请求硬件保护配置。 */
u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *p)
{
    if (p == 0) return 0u;
    if (!bms_config_store_get_afe_hw_profile(p)) return 0u;
    return bms_afe_hw_profile_validate(p);
}

/* 校验并持久化请求硬件保护配置。 */
static u8 bms_afe_hw_profile_set(const bms_afe_hw_profile_t *p)
{
    if (!bms_afe_hw_profile_validate(p)) return 0u;
    return bms_config_store_set_afe_hw_profile(p) ? 1u : 0u;
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
/* 将 DVC 实际配置诊断值转换为协议字段。 */
static u8 dvc_effective_u16(dvc1124_config_field_t field, u16 *out)
{
    u32 value;
    if (out == 0) return 0u;
    if (DVC1124_ConfigServiceRead(field, &value) != DVC1124_CFG_OK || value > 65535u) return 0u;
    *out = (u16)value;
    return 1u;
}
#endif

/* 取得硬件实际可表示的保护值及量化状态。 */
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
#if !DVC1124_HW_PROTECT_ENABLE
        /*
         * 请求配置仍持久化且可读；实际生效状态须体现编译期台架隔离：
         * DVC 硬件实际未启用阈值保护。
         */
        p->enable_mask = 0u;
        p->cov_mv = 0u;
        p->cov_delay_ms = 0u;
        p->cuv_mv = 0u;
        p->cuv_delay_ms = 0u;
        p->ocd1_a10 = 0u;
        p->ocd1_delay_ms = 0u;
        p->ocd2_a10 = 0u;
        p->ocd2_delay_ms = 0u;
        p->occ1_a10 = 0u;
        p->occ1_delay_ms = 0u;
        p->occ2_a10 = 0u;
        p->occ2_delay_ms = 0u;
        p->sc_a10 = 0u;
        p->sc_delay_us = 0u;
#endif
    }
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    {
        sh3673510_protection_actual_t actual;
        if (!sh3673510_control_get_protection_actual(&actual)) return 0u;
        /* p34：旧请求可单独置一位，但已验证的硬件OCD使能实际同时控制两级。 */
        if (p->enable_mask & (BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2))
            p->enable_mask |= BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2;
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
            p->sc_a10 = actual.sc_a10;
            p->sc_delay_us = actual.sc_delay_us;
        }
    }
#endif
    return 1u;
}

static u16 s_afe_hw_apply_state = BMS_AFE_HW_APPLY_IDLE;
static u16 s_afe_hw_last_error = BMS_AFE_HW_ERROR_NONE;

/* 按协议大端字序取出硬件保护配置字。 */
static u16 afe_hw_profile_word_be(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

/* 比较协议字段是否与当前配置一致。 */
static u8 afe_hw_profile_words_equal(const bms_afe_hw_profile_t *a,
                                     const bms_afe_hw_profile_t *b)
{
    const u16 *wa = (const u16 *)a;
    const u16 *wb = (const u16 *)b;
    u16 i;
    for (i = 0u; i < BMS_AFE_HW_PROFILE_WORD_COUNT; ++i)
        if (wa[i] != wb[i]) return 0u;
    return 1u;
}

/* 事务失败时恢复原硬件保护配置并记录回滚状态。 */
static bms_afe_hw_error_t afe_hw_profile_rollback(const bms_afe_hw_profile_t *before)
{
    bms_afe_hw_profile_t verify;
    bms_afe_hw_profile_t effective;

    if (before == 0 ||
        !bms_afe_hw_profile_set(before) ||
        !bms_afe_apply_protection_config() ||
        !bms_afe_hw_profile_get(&verify) ||
        !afe_hw_profile_words_equal(before, &verify) ||
        !bms_afe_hw_profile_get_effective(&effective))
    {
        s_afe_hw_apply_state = BMS_AFE_HW_APPLY_INCONSISTENT;
        bms_afe_invalidate_configuration();
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_ROLLBACK;
        bms_afe_hw_access_close();
        return (bms_afe_hw_error_t)s_afe_hw_last_error;
    }

    s_afe_hw_apply_state = BMS_AFE_HW_APPLY_ROLLBACK_OK;
    return (bms_afe_hw_error_t)s_afe_hw_last_error;
}

/* 校验完整大端配置块，持久化、应用并回读；失败执行回滚。 */
bms_afe_hw_error_t bms_afe_hw_profile_commit_be(const u8 *pdata, u16 qty)
{
    bms_afe_hw_profile_t before;
    bms_afe_hw_profile_t candidate;
    bms_afe_hw_profile_t verify;
    bms_afe_hw_profile_t effective;
    u16 i;

    if (!bms_afe_hw_access_is_active())
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_AUTH;
        return BMS_AFE_HW_ERROR_AUTH;
    }
    if (pdata == 0 || qty != BMS_AFE_HW_PROFILE_WORD_COUNT)
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_VALIDATION;
        return BMS_AFE_HW_ERROR_VALIDATION;
    }
    if (!bms_afe_hw_profile_get(&before))
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_STORE;
        return (bms_afe_hw_error_t)s_afe_hw_last_error;
    }

    candidate = before;
    for (i = 0u; i < qty; ++i)
        ((u16 *)&candidate)[i] = afe_hw_profile_word_be(&pdata[(u32)i * 2u]);

    if (!bms_afe_hw_profile_validate_write(&candidate))
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_VALIDATION;
        return BMS_AFE_HW_ERROR_VALIDATION;
    }

    if (!bms_afe_hw_profile_set(&candidate))
    {
        s_afe_hw_apply_state = BMS_AFE_HW_APPLY_IDLE;
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_STORE;
        return (bms_afe_hw_error_t)s_afe_hw_last_error;
    }

    bms_afe_invalidate_configuration();
    if (!bms_afe_apply_protection_config())
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_APPLY_VERIFY;
        return afe_hw_profile_rollback(&before);
    }

    if (!bms_afe_hw_profile_get(&verify) ||
        !afe_hw_profile_words_equal(&candidate, &verify) ||
        !bms_afe_hw_profile_get_effective(&effective))
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_APPLY_VERIFY;
        return afe_hw_profile_rollback(&before);
    }

    s_afe_hw_apply_state = BMS_AFE_HW_APPLY_OK;
    s_afe_hw_last_error = BMS_AFE_HW_ERROR_NONE;
    bms_afe_hw_access_close();
    return BMS_AFE_HW_ERROR_NONE;
}

/* 查询最近一次硬件配置应用状态。 */
u16 bms_afe_hw_profile_apply_state(void)
{
    return s_afe_hw_apply_state;
}

/* 取得最近一次硬件保护事务错误。 */
u16 bms_afe_hw_profile_last_error(void)
{
    return s_afe_hw_last_error;
}
