/*
 * 文件功能：独立 AFE 硬件保护参数；负责默认值、校验、持久化和实际量化值，
 * 不代替软件三级保护。
 * bms/core/bms_afe_hw_profile.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_afe_hw_profile.h"
#include "bms_afe_driver.h"
#include "bms_afe_hw_access.h"
#include "bms_product.h"
#include "bms_config_store.h"
#include <string.h>

#include "dvc1124_config_service.h"

/* 取得当前产品要求的 AFE 型号标识。 */
u16 bms_afe_hw_profile_expected_model(void)
{
    return BMS_AFE_HW_MODEL_DVC1124;
}

/* 取得当前 AFE 硬件保护能力掩码。 */
u16 bms_afe_hw_profile_capabilities(void)
{
    return (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                 BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                 BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_OCC2 |
                 BMS_AFE_HW_EN_SC);
}

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

/* 按产品输入构造独立 AFE 硬件保护默认值。 */
void bms_afe_hw_profile_build_default(bms_afe_hw_profile_t *p)
{
    if (p == 0) return;
    memset(p, 0, sizeof(*p));
    p->schema_version = BMS_AFE_HW_PROFILE_SCHEMA_VERSION;
    p->afe_model = bms_afe_hw_profile_expected_model();
    /* D008 硬件默认不再从软件三级保护表取初值。 */
    p->cov_mv = D008_AFE_COV_MV;
    p->cov_delay_ms = D008_AFE_COV_DELAY_MS;
    p->cov_recover_mv = D008_AFE_COV_RECOVER_MV;
    p->cov_recover_ms = D008_AFE_COV_RECOVER_MS;
    p->cuv_mv = D008_AFE_CUV_MV;
    p->cuv_delay_ms = D008_AFE_CUV_DELAY_MS;
    p->cuv_recover_mv = D008_AFE_CUV_RECOVER_MV;
    p->cuv_recover_ms = D008_AFE_CUV_RECOVER_MS;
    p->ocd1_a10 = D008_AFE_OCD1_A10;
    p->ocd1_delay_ms = D008_AFE_OCD1_DELAY_MS;
    p->ocd2_a10 = D008_AFE_OCD2_A10;
    p->ocd2_delay_ms = D008_AFE_OCD2_DELAY_MS;
    p->ocd_recover_a10 = D008_AFE_OCD_RECOVER_A10;
    p->occ1_a10 = D008_AFE_OCC1_A10;
    p->occ1_delay_ms = D008_AFE_OCC1_DELAY_MS;
    p->occ2_a10 = D008_AFE_OCC2_A10;
    p->occ2_delay_ms = D008_AFE_OCC2_DELAY_MS;
    p->occ_recover_a10 = D008_AFE_OCC_RECOVER_A10;
    if (D008_AFE_SC_THRESHOLD_MV != 0u && DVC1124_DEFAULT_SHUNT_UOHM != 0u)
        p->sc_a10 = (u16)(((u32)D008_AFE_SC_THRESHOLD_MV * 10000u) /
                          DVC1124_DEFAULT_SHUNT_UOHM);
    p->sc_delay_us = D008_AFE_SC_DELAY_US;
    if (D008_AFE_ENABLE_COV) p->enable_mask |= BMS_AFE_HW_EN_COV;
    if (D008_AFE_ENABLE_CUV) p->enable_mask |= BMS_AFE_HW_EN_CUV;
    if (D008_AFE_ENABLE_OCD1) p->enable_mask |= BMS_AFE_HW_EN_OCD1;
    if (D008_AFE_ENABLE_OCD2) p->enable_mask |= BMS_AFE_HW_EN_OCD2;
    if (D008_AFE_ENABLE_OCC1) p->enable_mask |= BMS_AFE_HW_EN_OCC1;
    if (D008_AFE_ENABLE_OCC2) p->enable_mask |= BMS_AFE_HW_EN_OCC2;
    if (D008_AFE_ENABLE_SC && p->sc_a10 != 0u) p->enable_mask |= BMS_AFE_HW_EN_SC;
    dvc_normalize_default_profile(p);
}

static u16 s_validation_word = 0xFFFFu;
static u8 reject_profile_word(u16 word) { if (s_validation_word==0xFFFFu) s_validation_word=word; return 0u; }
u16 bms_afe_hw_profile_validation_word(void) { return s_validation_word; }

/* 验证保护触发与恢复阈值的回差关系。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
#if defined(__GNUC__)
static u8 validate_hysteresis(const bms_afe_hw_profile_t *p) __attribute__((noinline));
#endif
static u8 validate_hysteresis(const bms_afe_hw_profile_t *p)
{
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) &&
        (p->cov_mv == 0u || p->cov_recover_mv >= p->cov_mv)) return reject_profile_word(p->cov_mv==0u?2u:4u);
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) &&
        (p->cuv_mv == 0u || p->cuv_recover_mv <= p->cuv_mv)) return reject_profile_word(p->cuv_mv==0u?6u:8u);

    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) &&
        (p->ocd1_a10 == 0u || p->ocd_recover_a10 >= p->ocd1_a10)) return reject_profile_word(14u);
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) &&
        (p->ocd2_a10 == 0u || p->ocd_recover_a10 >= p->ocd2_a10)) return reject_profile_word(14u);
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) &&
        (p->occ1_a10 == 0u || p->occ_recover_a10 >= p->occ1_a10)) return reject_profile_word(20u);
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC2) &&
        (p->occ2_a10 == 0u || p->occ_recover_a10 >= p->occ2_a10)) return reject_profile_word(20u);

    if (p->enable_mask & BMS_AFE_HW_EN_TEMP) {
        if(p->chg_ot_recover_x10>=p->chg_ot_x10) return reject_profile_word(26u);
        if(p->dsg_ot_recover_x10>=p->dsg_ot_x10) return reject_profile_word(30u);
        if(p->chg_ut_recover_x10<=p->chg_ut_x10) return reject_profile_word(28u);
        if(p->dsg_ut_recover_x10<=p->dsg_ut_x10) return reject_profile_word(32u);
    }
    return 1u;
}

/* 校验型号、能力、阈值和延时范围。 */
u8 bms_afe_hw_profile_validate(const bms_afe_hw_profile_t *p)
{
    u32 sense_uv;
    if (p == 0) return reject_profile_word(0xFFFFu);
    if (p->schema_version != BMS_AFE_HW_PROFILE_SCHEMA_VERSION) return reject_profile_word(0u);
    if (p->afe_model != bms_afe_hw_profile_expected_model()) return reject_profile_word(1u);
    if ((p->enable_mask & (u16)~bms_afe_hw_profile_capabilities()) != 0u) return reject_profile_word(34u);
    if (!validate_hysteresis(p)) return 0u;
    {
        const u16 *words=(const u16 *)p;
        u8 i;
        for(i=25u;i<=32u;++i) if(words[i]>1450u) return reject_profile_word(i);
    }

    if ((p->enable_mask & BMS_AFE_HW_EN_COV) && (p->cov_mv < 501u || p->cov_mv > 4595u)) return reject_profile_word(2u);
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) && p->cuv_mv > 4095u) return reject_profile_word(6u);
    if (p->cov_delay_ms > 8000u) return reject_profile_word(3u);
    if (p->cuv_delay_ms > 8000u) return reject_profile_word(7u);
    if (p->ocd1_delay_ms > 2048u) return reject_profile_word(11u);
    if (p->occ1_delay_ms > 2048u) return reject_profile_word(17u);
    if (p->ocd2_delay_ms > 1024u) return reject_profile_word(13u);
    if (p->occ2_delay_ms > 1024u) return reject_profile_word(19u);
    sense_uv = ((u32)p->ocd1_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) && sense_uv > 63750u) return reject_profile_word(10u);
    sense_uv = ((u32)p->occ1_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) && sense_uv > 63750u) return reject_profile_word(16u);
    sense_uv = ((u32)p->ocd2_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) && sense_uv > 256000u) return reject_profile_word(12u);
    sense_uv = ((u32)p->occ2_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC2) && sense_uv > 256000u) return reject_profile_word(18u);
    sense_uv = ((u32)p->sc_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_SC) &&
        (sense_uv < 10000u || sense_uv > 630000u)) return reject_profile_word(22u);
    return 1u;
}

/* 新写入按原厂保证范围与最小档位校验；旧参数读取保留原有兼容规则。 */
u8 bms_afe_hw_profile_validate_write(const bms_afe_hw_profile_t *p)
{
    s_validation_word=0xFFFFu;
    if (!bms_afe_hw_profile_validate(p)) return 0u;
    /* DVC RM V1.2 p18–20：OC1最低250uV、OC2最低4mV，延时200/8/4ms。 */
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) && p->cov_delay_ms < 200u) return reject_profile_word(3u);
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) && p->cuv_delay_ms < 200u) return reject_profile_word(7u);
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) &&
        ((u32)p->ocd1_a10 * DVC1124_DEFAULT_SHUNT_UOHM < 2500u || p->ocd1_delay_ms < 8u)) return reject_profile_word((p->ocd1_delay_ms<8u?11u:10u));
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) &&
        ((u32)p->occ1_a10 * DVC1124_DEFAULT_SHUNT_UOHM < 2500u || p->occ1_delay_ms < 8u)) return reject_profile_word((p->occ1_delay_ms<8u?17u:16u));
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) &&
        ((u32)p->ocd2_a10 * DVC1124_DEFAULT_SHUNT_UOHM < 40000u || p->ocd2_delay_ms < 4u)) return reject_profile_word((p->ocd2_delay_ms<4u?13u:12u));
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC2) &&
        ((u32)p->occ2_a10 * DVC1124_DEFAULT_SHUNT_UOHM < 40000u || p->occ2_delay_ms < 4u)) return reject_profile_word((p->occ2_delay_ms<4u?19u:18u));
    if ((p->enable_mask & BMS_AFE_HW_EN_SC) && p->sc_delay_us > 1992u) return reject_profile_word(23u);
    return 1u;
}

/* 取得缓存的请求硬件保护配置。 */
u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *p)
{
    if (p == 0) return 0u;
    if (!bms_config_store_get_afe_hw_profile(p)) return 0u;
    return bms_afe_hw_profile_validate(p);
}

/* 取得硬件实际可表示的保护值及量化状态。 */
u8 bms_afe_hw_profile_get_effective(bms_afe_hw_profile_t *p)
{
    bms_afe_hw_profile_t requested;
    if (p == 0 || !bms_afe_hw_profile_get(&requested)) return 0u;
    *p = requested;

    {
        if (!DVC1124_ReadProtectionEffective(p)) return 0u;
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
    return 1u;
}

static u16 s_afe_hw_apply_state = BMS_AFE_HW_APPLY_IDLE;
static u16 s_afe_hw_last_error = BMS_AFE_HW_ERROR_NONE;
/* version, sequence, word, apply(reg/expected/actual/reason), rollback(同前)。 */
static u16 s_hw_diagnostic[11] = {1u,0u,0xFFFFu,0xFFFFu,0xFFFFu,0xFFFFu,0u,0xFFFFu,0xFFFFu,0xFFFFu,0u};
u16 bms_afe_hw_profile_diagnostic_word(u16 offset)
{
    return offset<11u?s_hw_diagnostic[offset]:0xFFFFu;
}


/* 按协议大端字序取出硬件保护配置字。 */
static u16 afe_hw_profile_word_be(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

/* 驱动逐寄存器核对写入编码，随后验证对外读回链。 */
static u8 apply_verified(const bms_afe_hw_profile_t *request)
{
    bms_afe_hw_profile_t effective = *request;
    return bms_afe_driver_apply_protection_config(request) &&
        DVC1124_ReadProtectionEffective(&effective);
}

bms_afe_hw_error_t bms_afe_hw_profile_commit_be(const u8 *pdata, u16 qty)
{
    bms_afe_hw_profile_t before, candidate;
    bms_afe_hw_error_t error;
    u16 i;

    ++s_hw_diagnostic[1];
    for (i = 2u; i < 11u; ++i) s_hw_diagnostic[i] = 0xFFFFu;
    s_hw_diagnostic[6] = s_hw_diagnostic[10] = 0u;
    s_afe_hw_apply_state = BMS_AFE_HW_APPLY_IDLE;

    if (!bms_afe_hw_access_is_active()) error = BMS_AFE_HW_ERROR_AUTH;
    else if (!pdata || qty != BMS_AFE_HW_PROFILE_WORD_COUNT) error = BMS_AFE_HW_ERROR_VALIDATION;
    else if (bms_config_store_commit_unknown()) error = BMS_AFE_HW_ERROR_STORE_UNKNOWN;
    else if (!bms_afe_hw_profile_get(&before)) error = BMS_AFE_HW_ERROR_STORE;
    else {
        for (i = 0u; i < qty; ++i) {
            u16 value = afe_hw_profile_word_be(pdata + i * 2u);
            memcpy((u8 *)&candidate + i * 2u, &value, sizeof(value));
        }
        if (!bms_afe_hw_profile_validate_write(&candidate)) {
            s_hw_diagnostic[2] = bms_afe_hw_profile_validation_word();
            error = BMS_AFE_HW_ERROR_VALIDATION;
        } else if (!bms_config_afe_compatible(&candidate)) {
            error = BMS_AFE_HW_ERROR_VALIDATION;
        } else if (!bms_afe_begin_protection_update()) {
            error = BMS_AFE_HW_ERROR_APPLY_VERIFY;
        } else {
            if (!apply_verified(&candidate)) {
                s_hw_diagnostic[6] = 1u;
                error = BMS_AFE_HW_ERROR_APPLY_VERIFY;
            } else if (!bms_config_store_set_afe_hw_profile(&candidate)) {
                error = bms_config_store_commit_unknown() ?
                    BMS_AFE_HW_ERROR_STORE_UNKNOWN : BMS_AFE_HW_ERROR_STORE;
            } else {
                bms_afe_finish_protection_update();
                bms_afe_hw_access_close();
                s_afe_hw_apply_state = BMS_AFE_HW_APPLY_OK;
                s_afe_hw_last_error = BMS_AFE_HW_ERROR_NONE;
                return BMS_AFE_HW_ERROR_NONE;
            }

            if (error != BMS_AFE_HW_ERROR_STORE_UNKNOWN && apply_verified(&before)) {
                bms_afe_finish_protection_update();
                s_afe_hw_apply_state = BMS_AFE_HW_APPLY_ROLLBACK_OK;
            } else {
                if (error != BMS_AFE_HW_ERROR_STORE_UNKNOWN) {
                    s_hw_diagnostic[10] = 1u;
                    error = BMS_AFE_HW_ERROR_ROLLBACK;
                }
                bms_afe_invalidate_configuration();
                bms_afe_hw_access_close();
                s_afe_hw_apply_state = BMS_AFE_HW_APPLY_INCONSISTENT;
            }
        }
    }
    s_afe_hw_last_error = error;
    return error;
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
