/*
 * 文件功能：CFG2 持久配置的缓存、校验与编解码；按产品 tag 和独立更新编号恢复/更新各
 * 参数组。
 * bms/core/bms_config_store.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_diag.h"
#include "bms_update_policy.h"
#include "bms_config_store.h"
#include "bms_product.h"

#include "bms_soc_defs.h"
#include "bms_sw_protection.h"
#include "bms_features.h"
#include "btname_modbus.h"
#include "bms_storage_platform.h"
#include "storage_record.h"
#include <string.h>

#define BMS_CONFIG_RECORD_MAGIC          0x43464732u /* 配置记录标识：CFG2。 */
#define BMS_CONFIG_SCHEMA_VERSION        3u
#define BMS_CONFIG_PROTECT_WORDS         65u
#define BMS_CONFIG_SYSTEM_WORDS          5u
#define BMS_CONFIG_AFE_WORDS             35u
#define BMS_CONFIG_BTNAME_BYTES          24u
#define BMS_CONFIG_USER_BYTES            54u
#define BMS_CONFIG_USER_BUSINESS_WORDS   7u

#define BMS_CONFIG_PROTECT_BYTES         (BMS_CONFIG_PROTECT_WORDS * 2u)
#define BMS_CONFIG_SYSTEM_BYTES          (BMS_CONFIG_SYSTEM_WORDS * 4u)
#define BMS_CONFIG_AFE_BYTES             (BMS_CONFIG_AFE_WORDS * 2u)
#define BMS_CONFIG_PAYLOAD_BYTES         (4u + BMS_CONFIG_PROTECT_BYTES + BMS_CONFIG_SYSTEM_BYTES + BMS_CONFIG_AFE_BYTES + BMS_CONFIG_BTNAME_BYTES + 8u + BMS_CONFIG_USER_BYTES + BMS_UPDATE_CONFIG_GROUP_COUNT * 2u)

#if (BTNAME_SUFFIX_MAX_LEN >= BMS_CONFIG_BTNAME_BYTES)
#error "BMS_CONFIG_BTNAME_BYTES must leave room for NUL"
#endif

typedef char bms_config_protect_layout_must_be_65_words[(sizeof(bms_protection_params_t) == BMS_CONFIG_PROTECT_BYTES) ? 1 : -1];
typedef char bms_config_system_layout_must_be_5_words[(sizeof(bms_config_system_params_t) == BMS_CONFIG_SYSTEM_BYTES) ? 1 : -1];
typedef char bms_config_afe_layout_must_be_35_words[(sizeof(bms_afe_hw_profile_t) == BMS_CONFIG_AFE_BYTES) ? 1 : -1];
/* 只共享连续的七个 u16；校准字段仍逐字段编码。TC32 的 stddef.h 与 SDK size_t 冲突。 */
typedef char bms_config_user_business_layout_must_be_7_words[(__builtin_offsetof(bms_user_params_t, balance_stop_delta_mv) + sizeof(u16) == BMS_CONFIG_USER_BUSINESS_WORDS * 2u) ? 1 : -1];

typedef struct {
    u16 revisions[BMS_UPDATE_CONFIG_GROUP_COUNT];
    bms_protection_params_t protect;
    bms_config_system_params_t system;
    bms_afe_hw_profile_t afe_hw;
    bms_soc_config_t soc;
    bms_user_params_t user;
    char bt_name_suffix[BMS_CONFIG_BTNAME_BYTES];
} bms_config_cache_t;

static storage_record_store_t g_bms_config_store;
/* 已接受的参数缓存；保存失败不得把未落盘候选当作已提交配置发布。 */
static bms_config_cache_t g_bms_config;
static u8 g_bms_config_ready;
/* 仅在加载/发布配置时重算，不逐样本计算。 */
static u8 g_bms_config_user_valid;
static u8 g_bms_config_needs_save;
static void bms_config_user_defaults(bms_user_params_t *value);

/* 将 16 位值按小端写入存储缓冲区。 */
static void bms_config_put_u16le(u8 *buf, u16 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)(value >> 8);
}

/* 将 32 位值按小端写入存储缓冲区。 */
static void bms_config_put_u32le(u8 *buf, u32 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)((value >> 8) & 0xFFu);
    buf[2] = (u8)((value >> 16) & 0xFFu);
    buf[3] = (u8)((value >> 24) & 0xFFu);
}

/* 从存储缓冲区按小端读取 16 位值。 */
static u16 bms_config_get_u16le(const u8 *buf)
{
    return (u16)((u16)buf[0] | ((u16)buf[1] << 8));
}

/* 从存储缓冲区按小端读取 32 位值。 */
static u32 bms_config_get_u32le(const u8 *buf)
{
    return ((u32)buf[0]) | ((u32)buf[1] << 8) |
           ((u32)buf[2] << 16) | ((u32)buf[3] << 24);
}

/* memcpy 读取原生值，避免 packed 参数块的未对齐访问和类型别名问题。 */
static void bms_config_encode_words(u8 *payload, const void *values, u16 count)
{
    u16 i, word;
    for (i = 0u; i < count; ++i) {
        memcpy(&word, (const u8 *)values + i * 2u, sizeof(word));
        bms_config_put_u16le(payload + i * 2u, word);
    }
}

static void bms_config_decode_words(void *values, const u8 *payload, u16 count)
{
    u16 i, word;
    for (i = 0u; i < count; ++i) {
        word = bms_config_get_u16le(payload + i * 2u);
        memcpy((u8 *)values + i * 2u, &word, sizeof(word));
    }
}

/* 原软件保护默认表来源：Copyright (C), 2012-2013, www.armfly.com。 */
static const bms_protection_params_t s_default_protection = {
    .cell_ovp_first_mv = BMS_DEFAULT_CELL_OVP_MV,
    .cell_ovp_second_mv = BMS_DEFAULT_CELL_OVP_MV,
    .cell_ovp_third_mv = BMS_DEFAULT_CELL_OVP_MV,
    .cell_ovp_recover_mv = BMS_DEFAULT_CELL_OVP_RECOVER_MV,
    .cell_ovp_filter_10ms = 100,
    .cell_uvp_first_mv = BMS_DEFAULT_CELL_UVP_ALARM_MV,
    .cell_uvp_second_mv = BMS_DEFAULT_CELL_UVP_ALARM_MV,
    .cell_uvp_third_mv = BMS_DEFAULT_CUV3_MV,
    .cell_uvp_recover_mv = BMS_DEFAULT_CELL_UVP_RECOVER_MV,
    .cell_uvp_filter_10ms = BMS_DEFAULT_CUV3_FILTER,
    .pack_ovp_first_10mv = (BMS_DEFAULT_PACK_OVP_FIRST_CELL_MV / 10u * BMS_PRODUCT_CELL_COUNT),
    .pack_ovp_second_10mv = (BMS_DEFAULT_PACK_OVP_SECOND_CELL_MV / 10u * BMS_PRODUCT_CELL_COUNT),
    .pack_ovp_third_10mv = (BMS_DEFAULT_PACK_OVP_THIRD_CELL_MV / 10u * BMS_PRODUCT_CELL_COUNT),
    .pack_ovp_recover_10mv = (BMS_DEFAULT_PACK_OVP_RECOVER_CELL_MV / 10u * BMS_PRODUCT_CELL_COUNT),
    .pack_ovp_filter_10ms = 100,
    .pack_uvp_first_10mv = (BMS_DEFAULT_PACK_UVP_FIRST_CELL_MV / 10u * BMS_PRODUCT_CELL_COUNT),
    .pack_uvp_second_10mv = (BMS_DEFAULT_PACK_UVP_SECOND_CELL_MV / 10u * BMS_PRODUCT_CELL_COUNT),
    .pack_uvp_third_10mv = (BMS_DEFAULT_PACK_UVP_THIRD_CELL_MV / 10u * BMS_PRODUCT_CELL_COUNT),
    .pack_uvp_recover_10mv = (BMS_DEFAULT_PACK_UVP_RECOVER_CELL_MV / 10u * BMS_PRODUCT_CELL_COUNT),
    .pack_uvp_filter_10ms = 100,
    .charge_ocp_first_a10 = (100),
    .charge_ocp_second_a10 = (150),
    .charge_ocp_third_a10 = (200),
    .charge_ocp_recover_a10 = (100),
    .charge_ocp_filter_10ms = 10,
    .discharge_ocp_first_a10 = (100),
    .discharge_ocp_second_a10 = (150),
    .discharge_ocp_third_a10 = (200),
    .discharge_ocp_recover_a10 = (100),
    .discharge_ocp_filter_10ms = 10,
    .charge_otp_first_x10 = ((40 + 40) * 10),
    .charge_otp_second_x10 = ((50 + 40) * 10),
    .charge_otp_third_x10 = ((55 + 40) * 10),
    .charge_otp_recover_x10 = ((50 + 40) * 10),
    .charge_otp_filter_10ms = 100,
    .charge_utp_first_x10 = ((5 + 40) * 10),
    .charge_utp_second_x10 = ((3 + 40) * 10),
    .charge_utp_third_x10 = ((0 + 40) * 10),
    .charge_utp_recover_x10 = ((3 + 40) * 10),
    .charge_utp_filter_10ms = 100,
    .discharge_otp_first_x10 = ((50 + 40) * 10),
    .discharge_otp_second_x10 = ((50 + 40) * 10),
    .discharge_otp_third_x10 = ((60 + 40) * 10),
    .discharge_otp_recover_x10 = ((50 + 40) * 10),
    .discharge_otp_filter_10ms = 100,
    .discharge_utp_first_x10 = ((-10 + 40) * 10),
    .discharge_utp_second_x10 = ((-15 + 40) * 10),
    .discharge_utp_third_x10 = ((-20 + 40) * 10),
    .discharge_utp_recover_x10 = ((-10 + 40) * 10),
    .discharge_utp_filter_10ms = 100,
    .mos_otp_first_x10 = ((75 + 40) * 10),
    .mos_otp_second_x10 = ((85 + 40) * 10),
    .mos_otp_third_x10 = ((95 + 40) * 10),
    .mos_otp_recover_x10 = ((80 + 40) * 10),
    .mos_otp_filter_10ms = 100,
    .cell_delta_first_mv = 600,
    .cell_delta_second_mv = 800,
    .cell_delta_third_mv = 1000,
    .cell_delta_recover_mv = 800,
    .cell_delta_filter_10ms = 100,
    .soc_low_first_percent = 20,
    .soc_low_second_percent = 10,
    .soc_low_third_percent = 5,
    .soc_low_recover_percent = 11,
    .soc_low_filter_10ms = 100,
};

/* 编译默认值先检查等级及恢复关系，运行参数仍由完整 validator 校验。 */
#if (BMS_DEFAULT_CELL_UVP_ALARM_MV < BMS_DEFAULT_CUV3_MV) || \
    ((BMS_DEFAULT_CUV3_MV != 0u) && (BMS_DEFAULT_CELL_UVP_RECOVER_MV <= BMS_DEFAULT_CUV3_MV))
#error "CUV defaults require First >= Second >= Third and Recover > Third (unless Third=0)"
#endif

/* 取得产品软件保护默认配置。 */
void bms_config_store_get_default_protect(bms_protection_params_t *protect)
{
    if (protect != 0) *protect = s_default_protection;
}

/* 取得产品系统业务默认配置。 */
static void bms_config_store_get_default_system(bms_config_system_params_t *system)
{
    if (system == 0) return;
    memset(system, 0, sizeof(*system));
    system->bms_type = BMS_PRODUCT_WIRE_ID;
    system->series_num = BMS_PRODUCT_CELL_COUNT;
    system->capacity_factory = BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH;
    system->battery_chemistry = BMS_PRODUCT_CHEMISTRY;
    system->soc_profile_id = BMS_PRODUCT_SOC_PROFILE_ID;
}

/* 建立 CFG2 各参数组的默认缓存。 */
static void bms_config_defaults(bms_config_cache_t *cfg)
{
    unsigned group;
    memset(cfg, 0, sizeof(*cfg));
    for (group = 0u; group < BMS_UPDATE_CONFIG_GROUP_COUNT; ++group)
        cfg->revisions[group] = bms_update_revision((bms_update_group_t)group);
    bms_config_store_get_default_protect(&cfg->protect);
    bms_config_store_get_default_system(&cfg->system);
    bms_soc_get_default_config(&cfg->soc);
    cfg->soc.chemistry = (u8)cfg->system.battery_chemistry;
    cfg->soc.profile_id = (u8)cfg->system.soc_profile_id;
    bms_afe_hw_profile_build_default(&cfg->afe_hw);
    bms_config_user_defaults(&cfg->user);
}

/* 按 CFG2 固定字段顺序编码配置记录。 */
static void bms_config_encode(const bms_config_cache_t *cfg, u8 *payload)
{
    u32 system_word;
    u16 off = 4u;
    u16 i;

    bms_config_put_u32le(payload, BMS_PRODUCT_ID);
    bms_config_encode_words(&payload[off], &cfg->protect, BMS_CONFIG_PROTECT_WORDS);
    off += BMS_CONFIG_PROTECT_BYTES;
    for (i = 0u; i < BMS_CONFIG_SYSTEM_WORDS; ++i) {
        memcpy(&system_word, (const u8 *)&cfg->system + i * 4u, sizeof(system_word));
        bms_config_put_u32le(&payload[off], system_word); off = (u16)(off + 4u);
    }
    bms_config_encode_words(&payload[off], &cfg->afe_hw, BMS_CONFIG_AFE_WORDS);
    off += BMS_CONFIG_AFE_BYTES;
    memcpy(&payload[off], cfg->bt_name_suffix, BMS_CONFIG_BTNAME_BYTES);
    off += BMS_CONFIG_BTNAME_BYTES;
    bms_config_put_u16le(&payload[off], cfg->soc.current_deadband_ma); off += 2u;
    bms_config_put_u16le(&payload[off], cfg->soc.ocv_rest_prepare_s); off += 2u;
    payload[off++] = cfg->soc.ocv_error_band_percent;
    payload[off++] = 0u; /* 原学习开关槽保留，功能已删除。 */
    payload[off++] = 0u;
    payload[off++] = 0u;
    bms_config_encode_words(&payload[off], &cfg->user, BMS_CONFIG_USER_BUSINESS_WORDS);
    off += BMS_CONFIG_USER_BUSINESS_WORDS * 2u;
    bms_config_put_u32le(&payload[off], (u32)cfg->user.current_offset_ma); off += 4u;
    bms_config_put_u32le(&payload[off], cfg->user.current_gain_ppm); off += 4u;
    memcpy(&payload[off], cfg->user.serial, sizeof(cfg->user.serial));
    off += sizeof(cfg->user.serial);
    bms_config_encode_words(&payload[off], cfg->revisions, BMS_UPDATE_CONFIG_GROUP_COUNT);
}

/* 解码固定字段；调用者负责版本、产品 tag 和参数有效性校验。 */
static void bms_config_decode(bms_config_cache_t *cfg, const u8 *payload)
{
    u32 system_word;
    u16 off = 4u;
    u16 i;

    memset(cfg, 0, sizeof(*cfg));
    bms_config_decode_words(&cfg->protect, &payload[off], BMS_CONFIG_PROTECT_WORDS);
    off += BMS_CONFIG_PROTECT_BYTES;
    for (i = 0u; i < BMS_CONFIG_SYSTEM_WORDS; ++i) {
        system_word = bms_config_get_u32le(&payload[off]); off = (u16)(off + 4u);
        memcpy((u8 *)&cfg->system + i * 4u, &system_word, sizeof(system_word));
    }
    bms_config_decode_words(&cfg->afe_hw, &payload[off], BMS_CONFIG_AFE_WORDS);
    off += BMS_CONFIG_AFE_BYTES;
    memcpy(cfg->bt_name_suffix, &payload[off], BMS_CONFIG_BTNAME_BYTES);
    off += BMS_CONFIG_BTNAME_BYTES;
    cfg->bt_name_suffix[BMS_CONFIG_BTNAME_BYTES - 1u] = '\0';
    cfg->soc.chemistry = (u8)cfg->system.battery_chemistry;
    cfg->soc.profile_id = (u8)cfg->system.soc_profile_id;
    cfg->soc.current_deadband_ma = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->soc.ocv_rest_prepare_s = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->soc.ocv_error_band_percent = payload[off++];
    off += 2u; /* 保留 CFG2 字节位置；旧学习设置不再消费。 */
    off++; /* 预留字段。 */
    bms_config_decode_words(&cfg->user, &payload[off], BMS_CONFIG_USER_BUSINESS_WORDS);
    off += BMS_CONFIG_USER_BUSINESS_WORDS * 2u;
    cfg->user.current_offset_ma = (int32_t)bms_config_get_u32le(&payload[off]); off += 4u;
    cfg->user.current_gain_ppm = bms_config_get_u32le(&payload[off]); off += 4u;
    memcpy(cfg->user.serial, &payload[off], sizeof(cfg->user.serial));
    off += sizeof(cfg->user.serial);
    bms_config_decode_words(cfg->revisions, &payload[off], BMS_UPDATE_CONFIG_GROUP_COUNT);
}

/*
 * 数据和更新编号位于同一条 CRC/提交标记保护的记录中。这里只准备 RAM 值；
 * 启动验证完成并提交成功后才允许 AFE 输出。
 */
static u8 bms_config_apply_update_policy(bms_config_cache_t *cfg)
{
    bms_config_cache_t defaults;
    unsigned group;
    u8 changed = 0u;
    bms_config_defaults(&defaults);
    for (group = 0u; group < BMS_UPDATE_CONFIG_GROUP_COUNT; ++group) {
        if (cfg->revisions[group] == defaults.revisions[group]) continue;
        switch ((bms_update_group_t)group) {
        case BMS_UPDATE_SW:
            cfg->protect = defaults.protect;
            break;
        case BMS_UPDATE_AFE:
            cfg->afe_hw = defaults.afe_hw;
            break;
        case BMS_UPDATE_BUSINESS:
            cfg->system.capacity_factory = defaults.system.capacity_factory;
            cfg->user.heater_enable = defaults.user.heater_enable;
            cfg->user.heater_start_x10 = defaults.user.heater_start_x10;
            cfg->user.heater_stop_x10 = defaults.user.heater_stop_x10;
            cfg->user.balance_enable = defaults.user.balance_enable;
            cfg->user.balance_start_mv = defaults.user.balance_start_mv;
            cfg->user.balance_start_delta_mv = defaults.user.balance_start_delta_mv;
            cfg->user.balance_stop_delta_mv = defaults.user.balance_stop_delta_mv;
            break;
        case BMS_UPDATE_SOC:
            cfg->soc = defaults.soc;
            cfg->system.battery_chemistry = defaults.system.battery_chemistry;
            cfg->system.soc_profile_id = defaults.system.soc_profile_id;
            break;
        case BMS_UPDATE_CALIBRATION:
            cfg->user.current_offset_ma = defaults.user.current_offset_ma;
            cfg->user.current_gain_ppm = defaults.user.current_gain_ppm;
            break;
        case BMS_UPDATE_IDENTITY:
            memcpy(cfg->user.serial, defaults.user.serial, sizeof(cfg->user.serial));
            memcpy(cfg->bt_name_suffix, defaults.bt_name_suffix, sizeof(cfg->bt_name_suffix));
            break;
        default:
            break;
        }
        cfg->revisions[group] = defaults.revisions[group];
        changed = 1u;
    }
    return changed;
}

/* 保存完整配置缓存并返回持久化结果。 */
static int bms_config_save_cache(const bms_config_cache_t *cfg)
{
    u8 payload[BMS_CONFIG_PAYLOAD_BYTES];
    u8 previous[BMS_CONFIG_PAYLOAD_BYTES];
    bms_config_encode(cfg, payload);
    bms_config_encode(&g_bms_config, previous);
    if (g_bms_config_store.has_latest && !g_bms_config_needs_save &&
        memcmp(payload, previous, sizeof(payload)) == 0) return 1;
    if (!storage_record_save(&g_bms_config_store, payload)) {
        bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_SAVE); return 0;
    }
    g_bms_config = *cfg;
    g_bms_config_user_valid = bms_config_user_valid(&g_bms_config.user) ? 1u : 0u;
    g_bms_config_needs_save = 0u;
    return 1;
}

/* 确保配置存储已初始化并可访问。 */
static int bms_config_ensure_ready(void)
{
    return g_bms_config_ready ? 1 : bms_config_store_init();
}

/* 加载、校验 CFG2 记录并建立参数缓存。 */
int bms_config_store_init(void)
{
    const storage_port_t *port;
    storage_region_t region;
    u8 payload[BMS_CONFIG_PAYLOAD_BYTES];
    if (g_bms_config_ready) return 1;
    bms_diag_attempt(BMS_STORAGE_DOMAIN_CONFIG);
    port = bms_storage_platform_port();
    if (port == 0) { bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_PORT); return 0; }
    if (!bms_storage_platform_region(BMS_STORAGE_DOMAIN_CONFIG, &region)) { return 0; }
    if (!g_bms_config_store.ready && !storage_record_open(&g_bms_config_store, port, region, BMS_CONFIG_RECORD_MAGIC,
                             BMS_CONFIG_SCHEMA_VERSION, BMS_CONFIG_PAYLOAD_BYTES)) {
        bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_OPEN); return 0;
    }
    g_bms_config_needs_save = 0u;
    if (storage_record_load(&g_bms_config_store, payload) &&
        bms_config_get_u32le(payload) == BMS_PRODUCT_ID) {
        bms_config_decode(&g_bms_config, payload);
        /* 装配串数属于板级身份，不能把另一种装配的参数直接用于当前板。 */
        if (g_bms_config.system.series_num != BMS_PRODUCT_CELL_COUNT ||
            g_bms_config.system.bms_type != BMS_PRODUCT_WIRE_ID) {
            bms_config_defaults(&g_bms_config);
            g_bms_config_needs_save = 1u;
        } else {
            g_bms_config_needs_save = bms_config_apply_update_policy(&g_bms_config);
        }
    } else {
        if (g_bms_config_store.load_status == STORAGE_RECORD_LOAD_IO_ERROR) {
            bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_INVALID); return 0;
        }
        g_bms_config_store.has_latest = 0u;
        bms_config_defaults(&g_bms_config);
        g_bms_config_needs_save = 1u;
        bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_DEFAULTS);
    }
    g_bms_config_user_valid = bms_config_user_valid(&g_bms_config.user) ? 1u : 0u;
    g_bms_config_ready = 1u;
    bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_OK);
    return 1;
}

/* 取得缓存的软件保护配置。 */
int bms_config_store_get_protect(bms_protection_params_t *protect)
{
    if ((protect == 0) || !g_bms_config_ready) return 0;
    *protect = g_bms_config.protect;
    return 1;
}

/* 校验并保存软件保护配置，按保存结果更新缓存。 */
int bms_config_store_set_protect(const bms_protection_params_t *protect)
{
    bms_config_cache_t next;
    if (!bms_sw_protection_validate_params(protect) || !bms_config_ensure_ready()) return 0;
    if (memcmp(&g_bms_config.protect, protect, sizeof(*protect)) == 0) return 1;
    next = g_bms_config; next.protect = *protect;
    return bms_config_save_cache(&next);
}

/* 取得缓存的系统业务配置。 */
int bms_config_store_get_system(bms_config_system_params_t *system)
{
    if ((system == 0) || !g_bms_config_ready) return 0;
    *system = g_bms_config.system;
    return 1;
}

/* 校验并保存系统业务配置，按保存结果更新缓存。 */
int bms_config_store_set_system(const bms_config_system_params_t *system)
{
    bms_config_cache_t next;
    if ((system == 0) || !bms_config_ensure_ready()) return 0;
    if (system->series_num != BMS_PRODUCT_CELL_COUNT || system->bms_type != BMS_PRODUCT_WIRE_ID) return 0;
    if ((system->battery_chemistry > BMS_SOC_CHEMISTRY_NMC) ||
        (system->soc_profile_id > BMS_SOC_PROFILE_GENERIC_NMC) ||
        ((system->battery_chemistry == BMS_SOC_CHEMISTRY_LFP) && (system->soc_profile_id == BMS_SOC_PROFILE_GENERIC_NMC)) ||
        ((system->battery_chemistry == BMS_SOC_CHEMISTRY_NMC) && (system->soc_profile_id == BMS_SOC_PROFILE_GENERIC_LFP))) return 0;
    if (memcmp(&g_bms_config.system, system, sizeof(*system)) == 0) return 1;
    if (system->capacity_factory == 0u || system->capacity_factory > BMS_SOC_CAPACITY_MAX_0P1AH) return 0;
    next = g_bms_config; next.system = *system;
    next.soc.chemistry = (u8)system->battery_chemistry;
    next.soc.profile_id = (u8)system->soc_profile_id;
    return bms_config_save_cache(&next);
}

/* 取得缓存的独立 AFE 硬件保护配置。 */
int bms_config_store_get_afe_hw_profile(bms_afe_hw_profile_t *profile)
{
    if ((profile == 0) || !g_bms_config_ready) return 0;
    *profile = g_bms_config.afe_hw;
    return 1;
}

/* 校验并保存独立 AFE 硬件保护配置，按保存结果更新缓存。 */
int bms_config_store_set_afe_hw_profile(const bms_afe_hw_profile_t *profile)
{
    bms_config_cache_t next;
    if (!bms_afe_hw_profile_validate(profile) || !bms_config_ensure_ready()) return 0;
    if (memcmp(&g_bms_config.afe_hw, profile, sizeof(*profile)) == 0) return 1;
    next = g_bms_config; next.afe_hw = *profile;
    return bms_config_save_cache(&next);
}

/* 取得缓存的BLE 名称后缀。 */
int bms_config_store_get_bt_name_suffix(char *suffix, u16 suffix_size)
{
    u16 i = 0u;
    u16 limit;
    if ((suffix == 0) || (suffix_size == 0u) || !g_bms_config_ready) return 0;
    limit = (u16)(suffix_size - 1u);
    if (limit > BTNAME_SUFFIX_MAX_LEN) limit = BTNAME_SUFFIX_MAX_LEN;
    while ((i < limit) && (g_bms_config.bt_name_suffix[i] != '\0')) {
        suffix[i] = g_bms_config.bt_name_suffix[i]; ++i;
    }
    suffix[i] = '\0';
    return 1;
}

/* 校验并保存BLE 名称后缀，按保存结果更新缓存。 */
int bms_config_store_set_bt_name_suffix(const char *suffix)
{
    bms_config_cache_t next;
    u16 len = 0u;
    if ((suffix == 0) || !bms_config_ensure_ready()) return 0;
    next = g_bms_config;
    memset(next.bt_name_suffix, 0, sizeof(next.bt_name_suffix));
    while ((len < BTNAME_SUFFIX_MAX_LEN) && (suffix[len] != '\0')) {
        next.bt_name_suffix[len] = suffix[len]; ++len;
    }
    if (memcmp(g_bms_config.bt_name_suffix, next.bt_name_suffix, sizeof(next.bt_name_suffix)) == 0) return 1;
    return bms_config_save_cache(&next);
}

/* 授权 AFE 输出前先校验唯一开发 schema。 */
int bms_config_store_validate_startup(void)
{
    uint16_t invalid = 0u;
    bms_diag_upgrade(DIAG_UPGRADE_STARTED, 0u);
    if (!bms_config_ensure_ready()) return 0;
    if (!bms_sw_protection_validate_params(&g_bms_config.protect)) invalid |= DIAG_UPGRADE_BAD_SW;
    if (!bms_afe_hw_profile_validate(&g_bms_config.afe_hw)) invalid |= DIAG_UPGRADE_BAD_AFE;
    if (!bms_soc_config_valid(&g_bms_config.soc)) invalid |= DIAG_UPGRADE_BAD_SOC;
    if (!bms_config_user_valid(&g_bms_config.user)) invalid |= DIAG_UPGRADE_BAD_SW;
    if (g_bms_config.system.series_num != BMS_PRODUCT_CELL_COUNT ||
        g_bms_config.system.bms_type != BMS_PRODUCT_WIRE_ID ||
        g_bms_config.system.capacity_factory == 0u ||
        g_bms_config.system.capacity_factory > BMS_SOC_CAPACITY_MAX_0P1AH) invalid |= DIAG_UPGRADE_BAD_CAPACITY;
    if (invalid) { bms_diag_upgrade(DIAG_UPGRADE_VALIDATION, invalid); return 0; }
    if (g_bms_config_needs_save && !bms_config_save_cache(&g_bms_config)) return 0;
    bms_diag_upgrade(DIAG_UPGRADE_CONFIG_OK, 0u);
    return 1;
}

/* 取得缓存的SOC 算法配置。 */
int bms_config_store_get_soc(bms_soc_config_t *config)
{
    if (config == 0 || !g_bms_config_ready) return 0;
    *config = g_bms_config.soc;
    return bms_soc_config_valid(config);
}

/* 校验并保存SOC 算法配置，按保存结果更新缓存。 */
int bms_config_store_set_soc(const bms_soc_config_t *config)
{
    bms_config_cache_t next;
    if (!bms_soc_config_valid(config) || !bms_config_ensure_ready()) return 0;
    next = g_bms_config;
    next.soc = *config;
    next.system.battery_chemistry = config->chemistry;
    next.system.soc_profile_id = config->profile_id;
    if (g_bms_config.soc.chemistry == config->chemistry &&
        g_bms_config.soc.profile_id == config->profile_id &&
        g_bms_config.soc.current_deadband_ma == config->current_deadband_ma &&
        g_bms_config.soc.ocv_rest_prepare_s == config->ocv_rest_prepare_s &&
        g_bms_config.soc.ocv_error_band_percent == config->ocv_error_band_percent) return 1;
    return bms_config_save_cache(&next);
}

/* 构造用户业务参数默认值。 */
static void bms_config_user_defaults(bms_user_params_t *v)
{
    memset(v, 0, sizeof(*v));
    v->heater_enable = 1u;
    v->heater_start_x10 = BMS_HEATER_START_TEMP_X10;
    v->heater_stop_x10 = BMS_HEATER_STOP_TEMP_X10;
    v->balance_enable = BMS_BALANCE_ENABLE_DEFAULT;
    v->balance_start_mv = BMS_BALANCE_START_VOLTAGE_MV_DEFAULT;
    v->balance_start_delta_mv = BMS_BALANCE_START_DELTA_MV_DEFAULT;
    v->balance_stop_delta_mv = BMS_BALANCE_STOP_DELTA_MV_DEFAULT;
    v->current_gain_ppm = 1000000u;
    /* 工厂写入前，空 SN 使用编译期身份。 */
}

/* 检查用户业务参数范围与一致性。 */
int bms_config_user_valid(const bms_user_params_t *v)
{
    u16 i;
    if (!v || v->heater_enable > 1u || v->heater_start_x10 >= v->heater_stop_x10 ||
        v->heater_stop_x10 > 1650u) return 0;
    if (v->balance_enable > 1u ||
        v->balance_start_mv < BMS_BALANCE_CELL_PLAUSIBLE_MIN_MV ||
        v->balance_start_mv > 4500u ||
        v->balance_start_delta_mv == 0u ||
        v->balance_start_delta_mv > BMS_BALANCE_SUSPECT_DELTA_MV ||
        v->balance_stop_delta_mv >= v->balance_start_delta_mv)
        return 0;
    /* 这是运算/配置边界，不是保护阈值。 */
    if (v->current_offset_ma < -1000000 || v->current_offset_ma > 1000000 ||
        v->current_gain_ppm < 100000u || v->current_gain_ppm > 10000000u) return 0;
    for (i=0u; i<sizeof(v->serial); ++i)
        if (v->serial[i] && ((u8)v->serial[i] < 32u || (u8)v->serial[i] > 126u)) return 0;
    return 1;
}
/* 从配置缓存取得用户业务参数。 */
int bms_config_get_user(bms_user_params_t *v)
{
    if (!v || !g_bms_config_ready) return 0;
    *v = g_bms_config.user;
    return g_bms_config_user_valid;
}
/* 读取持久化电流校准偏移和比例。 */
int bms_config_get_current_calibration(int32_t *offset_ma, uint32_t *gain_ppm)
{
    if (!offset_ma || !gain_ppm || !g_bms_config_ready ||
        !g_bms_config_user_valid) return 0;
    *offset_ma = g_bms_config.user.current_offset_ma;
    *gain_ppm = g_bms_config.user.current_gain_ppm;
    return 1;
}
/* 校验并保存用户业务参数。 */
int bms_config_set_user(const bms_user_params_t *v)
{
    bms_config_cache_t next;
    if (!bms_config_user_valid(v) || !bms_config_ensure_ready()) return 0;
    next = g_bms_config; next.user = *v;
    if (!memcmp(&next, &g_bms_config, sizeof(next))) return 1;
    return bms_config_save_cache(&next);
}
/* 仅恢复业务参数类别，保留独立持久域边界。 */
int bms_config_reset_business(void)
{
    bms_config_cache_t next;
    if (!bms_config_ensure_ready()) return 0;
    next = g_bms_config;
    next.system.capacity_factory = BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH;
    next.user.heater_enable = 1u;
    next.user.heater_start_x10 = BMS_HEATER_START_TEMP_X10;
    next.user.heater_stop_x10 = BMS_HEATER_STOP_TEMP_X10;
    next.user.balance_enable = BMS_BALANCE_ENABLE_DEFAULT;
    next.user.balance_start_mv = BMS_BALANCE_START_VOLTAGE_MV_DEFAULT;
    next.user.balance_start_delta_mv = BMS_BALANCE_START_DELTA_MV_DEFAULT;
    next.user.balance_stop_delta_mv = BMS_BALANCE_STOP_DELTA_MV_DEFAULT;
    return bms_config_save_cache(&next);
}
/*
 * 精确计算 floor(magnitude * gain / 1000000)，
 * 不依赖固定 TC32 ABI 缺少的 64 位辅助函数。32 步中每步余数小于 1e6，
 * gain 不超过 1e7，使中间值不超过 11999998；溢出前饱和。
 */
static u32 current_scale_ppm(u32 magnitude, u32 gain)
{
    u32 quotient=0u, remainder=0u;
    int bit;
    if (gain==1000000u) return magnitude>2147483647u ? 2147483647u : magnitude;
    for (bit=31; bit>=0; --bit) {
        u32 next=remainder*2u + (((magnitude>>bit)&1u) ? gain : 0u);
        u32 add=next/1000000u;
        remainder=next%1000000u;
        if (quotient>(2147483647u-add)/2u) return 2147483647u;
        quotient=quotient*2u+add;
    }
    return quotient;
}
/* 对有符号电流应用限幅、零点与比例校准。 */
int32_t bms_config_calibrate_current(int32_t raw_ma)
{
    int32_t offset, delta;
    u32 magnitude, scaled;
    if (!g_bms_config_ready || !g_bms_config_user_valid) return raw_ma;
    offset=g_bms_config.user.current_offset_ma;
    if (offset>0 && raw_ma < -2147483647+offset) delta=-2147483647;
    else if (offset<0 && raw_ma > 2147483647+offset) delta=2147483647;
    else delta=raw_ma-offset;
    /* 所有路径采用相同的对称范围；INT32_MIN/零 offset 也先限幅再缩放。 */
    if (delta < -2147483647) delta=-2147483647;
    magnitude=delta<0 ? 0u-(u32)delta : (u32)delta;
    scaled=current_scale_ppm(magnitude,g_bms_config.user.current_gain_ppm);
    return delta<0 ? -(int32_t)scaled : (int32_t)scaled;
}
