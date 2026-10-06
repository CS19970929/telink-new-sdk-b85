/*
 * 文件功能：独立 AFE 硬件保护参数；负责默认值、校验、持久化和实际量化值，
 * 不代替软件三级保护。
 * bms/core/bms_afe_hw_profile.h；实际编译归属见各产品 sources.txt。
 */
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

/* 取得当前产品要求的 AFE 型号标识。 */
u16 bms_afe_hw_profile_expected_model(void);
/* 取得当前 AFE 硬件保护能力掩码。 */
u16 bms_afe_hw_profile_capabilities(void);
/* 校验型号、能力、阈值和延时范围。 */
u8 bms_afe_hw_profile_validate(const bms_afe_hw_profile_t *profile);
/* 新授权写入的保证范围/硬件能力校验；不重置旧持久参数。 */
u8 bms_afe_hw_profile_validate_write(const bms_afe_hw_profile_t *profile);
/* 取得缓存的请求硬件保护配置。 */
u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *profile);
/* 返回当前 AFE 量化后实际可表示的值。 */
u8 bms_afe_hw_profile_get_effective(bms_afe_hw_profile_t *profile);
/* 按产品输入构造独立 AFE 硬件保护默认值。 */
void bms_afe_hw_profile_build_default(bms_afe_hw_profile_t *profile);

typedef enum
{
    BMS_AFE_HW_APPLY_IDLE = 0u,
    BMS_AFE_HW_APPLY_OK = 1u,
    BMS_AFE_HW_APPLY_ROLLBACK_OK = 2u,
    BMS_AFE_HW_APPLY_INCONSISTENT = 3u,
} bms_afe_hw_apply_state_t;

typedef enum
{
    BMS_AFE_HW_ERROR_NONE = 0u,
    BMS_AFE_HW_ERROR_AUTH = 1u,
    BMS_AFE_HW_ERROR_VALIDATION = 2u,
    BMS_AFE_HW_ERROR_STORE = 3u,
    BMS_AFE_HW_ERROR_APPLY_VERIFY = 4u,
    BMS_AFE_HW_ERROR_ROLLBACK = 5u,
} bms_afe_hw_error_t;

/* 与帧/传输无关的完整 35-word 大端载荷；解析或访问存储/AFE 前先检查现有写会话。 */
bms_afe_hw_error_t bms_afe_hw_profile_commit_be(const u8 *pdata, u16 qty);
/* 查询最近一次硬件配置应用状态。 */
u16 bms_afe_hw_profile_apply_state(void);
/* 取得最近一次硬件保护事务错误。 */
u16 bms_afe_hw_profile_last_error(void);

#endif /* 头文件保护：BMS_AFE_HW_PROFILE_H_。 */
