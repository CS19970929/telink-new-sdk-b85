#include "bms_config_store.h"

#include "bms_soc_defs.h"
#include "bms_features.h"
#include "btname_modbus.h"
#include "bms_storage_platform.h"
#include "storage_record.h"
#include <string.h>

#define BMS_CONFIG_RECORD_MAGIC          0x43464731u /* CFG1 */
#define BMS_CONFIG_SCHEMA_VERSION        1u
#define BMS_CONFIG_PROTECT_WORDS         65u
#define BMS_CONFIG_SYSTEM_WORDS          10u
#define BMS_CONFIG_AFE_WORDS             35u
#define BMS_CONFIG_BTNAME_BYTES          24u

#define BMS_CONFIG_PROTECT_BYTES         (BMS_CONFIG_PROTECT_WORDS * 2u)
#define BMS_CONFIG_SYSTEM_BYTES          (BMS_CONFIG_SYSTEM_WORDS * 4u)
#define BMS_CONFIG_AFE_BYTES             (BMS_CONFIG_AFE_WORDS * 2u)
#define BMS_CONFIG_CONTROL_BYTES         ((u16)BMS_CONFIG_CTRL_COUNT * 4u)
#define BMS_CONFIG_PAYLOAD_BYTES         (BMS_CONFIG_PROTECT_BYTES + BMS_CONFIG_SYSTEM_BYTES + BMS_CONFIG_AFE_BYTES + BMS_CONFIG_CONTROL_BYTES + BMS_CONFIG_BTNAME_BYTES)

#if (BTNAME_SUFFIX_MAX_LEN >= BMS_CONFIG_BTNAME_BYTES)
#error "BMS_CONFIG_BTNAME_BYTES must leave room for NUL"
#endif

typedef char bms_config_protect_layout_must_be_65_words[(sizeof(struct PRT_E2ROM_PARAS) == BMS_CONFIG_PROTECT_BYTES) ? 1 : -1];
typedef char bms_config_system_layout_must_be_10_words[(sizeof(bms_config_system_params_t) == BMS_CONFIG_SYSTEM_BYTES) ? 1 : -1];
typedef char bms_config_afe_layout_must_be_35_words[(sizeof(bms_afe_hw_profile_t) == BMS_CONFIG_AFE_BYTES) ? 1 : -1];

typedef struct {
    struct PRT_E2ROM_PARAS protect;
    bms_config_system_params_t system;
    bms_afe_hw_profile_t afe_hw;
    u32 control[BMS_CONFIG_CTRL_COUNT];
    char bt_name_suffix[BMS_CONFIG_BTNAME_BYTES];
    bms_feature_params_t feature;
} bms_config_cache_t;

static storage_record_store_t g_bms_config_store;
static bms_config_cache_t g_bms_config;
static u8 g_bms_config_ready;

static void bms_config_put_u16le(u8 *buf, u16 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)(value >> 8);
}

static void bms_config_put_u32le(u8 *buf, u32 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)((value >> 8) & 0xFFu);
    buf[2] = (u8)((value >> 16) & 0xFFu);
    buf[3] = (u8)((value >> 24) & 0xFFu);
}

static u16 bms_config_get_u16le(const u8 *buf)
{
    return (u16)((u16)buf[0] | ((u16)buf[1] << 8));
}

static u32 bms_config_get_u32le(const u8 *buf)
{
    return ((u32)buf[0]) | ((u32)buf[1] << 8) |
           ((u32)buf[2] << 16) | ((u32)buf[3] << 24);
}


/*
 * D011 preserves Config schema 1. The previously reserved system flags/reserved0
 * words now own the compact Heater/Balance feature record so deployed software
 * protection and AFE profile records are not invalidated by this feature.
 *
 * flags:
 *   bit0      heater_enable
 *   bits1:11  heater_start_x10
 *   bits12:22 heater_stop_x10
 *   bit23     balance_enable
 *   bits24:31 balance_start_mv[7:0]
 * reserved0:
 *   bits0:4   balance_start_mv[12:8]
 *   bits5:14  balance_start_delta_mv
 *   bits15:24 balance_stop_delta_mv
 */
static void bms_feature_pack(bms_config_system_params_t *system,
                             const bms_feature_params_t *value)
{
    u32 flags;
    u32 reserved0;
    if (system == 0 || value == 0) return;
    flags = ((u32)(value->heater_enable & 1u)) |
            (((u32)value->heater_start_x10 & 0x07FFu) << 1) |
            (((u32)value->heater_stop_x10 & 0x07FFu) << 12) |
            (((u32)(value->balance_enable & 1u)) << 23) |
            (((u32)value->balance_start_mv & 0x00FFu) << 24);
    reserved0 = (((u32)value->balance_start_mv >> 8) & 0x1Fu) |
                (((u32)value->balance_start_delta_mv & 0x03FFu) << 5) |
                (((u32)value->balance_stop_delta_mv & 0x03FFu) << 15);
    system->flags = flags;
    system->reserved0 = reserved0;
}

static void bms_feature_unpack(const bms_config_system_params_t *system,
                               bms_feature_params_t *value)
{
    u32 flags;
    u32 reserved0;
    if (system == 0 || value == 0) return;
    flags = system->flags;
    reserved0 = system->reserved0;
    value->heater_enable = (u16)(flags & 1u);
    value->heater_start_x10 = (u16)((flags >> 1) & 0x07FFu);
    value->heater_stop_x10 = (u16)((flags >> 12) & 0x07FFu);
    value->balance_enable = (u16)((flags >> 23) & 1u);
    value->balance_start_mv = (u16)(((flags >> 24) & 0xFFu) |
                                   ((reserved0 & 0x1Fu) << 8));
    value->balance_start_delta_mv = (u16)((reserved0 >> 5) & 0x03FFu);
    value->balance_stop_delta_mv = (u16)((reserved0 >> 15) & 0x03FFu);
}

void bms_config_feature_defaults(bms_feature_params_t *value)
{
    if (value == 0) return;
    memset(value, 0, sizeof(*value));
    value->heater_enable = BMS_HEATER_ENABLE_DEFAULT;
    value->heater_start_x10 = BMS_HEATER_START_TEMP_X10;
    value->heater_stop_x10 = BMS_HEATER_STOP_TEMP_X10;
    value->balance_enable = BMS_BALANCE_ENABLE_DEFAULT;
    value->balance_start_mv = BMS_BALANCE_START_VOLTAGE_MV_DEFAULT;
    value->balance_start_delta_mv = BMS_BALANCE_START_DELTA_MV_DEFAULT;
    value->balance_stop_delta_mv = BMS_BALANCE_STOP_DELTA_MV_DEFAULT;
}

int bms_config_feature_valid(const bms_feature_params_t *value)
{
    if (value == 0) return 0;
    if (value->heater_enable > 1u ||
        value->heater_start_x10 >= value->heater_stop_x10 ||
        value->heater_stop_x10 > 1650u) return 0;
    if (value->balance_enable > 1u ||
        value->balance_start_mv < BMS_BALANCE_CELL_PLAUSIBLE_MIN_MV ||
        value->balance_start_mv > 4500u ||
        value->balance_start_delta_mv == 0u ||
        value->balance_start_delta_mv > BMS_BALANCE_SUSPECT_DELTA_MV ||
        value->balance_stop_delta_mv >= value->balance_start_delta_mv)
        return 0;
    return 1;
}

void bms_config_store_get_default_protect(struct PRT_E2ROM_PARAS *protect)
{
    struct PRT_E2ROM_PARAS defaults = E2P_PROTECT_DEFAULT_PRT;
    if (protect != 0) *protect = defaults;
}

void bms_config_store_get_default_system(bms_config_system_params_t *system)
{
    if (system == 0) return;
    memset(system, 0, sizeof(*system));
    system->bms_type = FD_BMS_TYPE;
    system->series_num = SeriesNum;
    system->capacity_factory = CapacityFactory;
#ifdef AFE_ODC2
    system->afe_odc2 = AFE_ODC2;
#else
    system->afe_odc2 = 0u;
#endif
    system->fac_init_soc = FAC_INIT_soc;
    system->init_soc = FAC_INIT_soc;
    system->battery_chemistry = BMS_SOC_CHEMISTRY_AUTO;
    system->soc_profile_id = BMS_SOC_PROFILE_AUTO;
}

static void bms_config_defaults(bms_config_cache_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    bms_config_store_get_default_protect(&cfg->protect);
    bms_config_store_get_default_system(&cfg->system);
    bms_config_feature_defaults(&cfg->feature);
    bms_feature_pack(&cfg->system, &cfg->feature);
}

static void bms_config_encode(const bms_config_cache_t *cfg, u8 *payload)
{
    const u16 *protect_words = (const u16 *)&cfg->protect;
    const u32 *system_words = (const u32 *)&cfg->system;
    const u16 *afe_words = (const u16 *)&cfg->afe_hw;
    u16 off = 0u;
    u16 i;

    for (i = 0u; i < BMS_CONFIG_PROTECT_WORDS; ++i) {
        bms_config_put_u16le(&payload[off], protect_words[i]); off = (u16)(off + 2u);
    }
    for (i = 0u; i < BMS_CONFIG_SYSTEM_WORDS; ++i) {
        bms_config_put_u32le(&payload[off], system_words[i]); off = (u16)(off + 4u);
    }
    for (i = 0u; i < BMS_CONFIG_AFE_WORDS; ++i) {
        bms_config_put_u16le(&payload[off], afe_words[i]); off = (u16)(off + 2u);
    }
    for (i = 0u; i < (u16)BMS_CONFIG_CTRL_COUNT; ++i) {
        bms_config_put_u32le(&payload[off], cfg->control[i]); off = (u16)(off + 4u);
    }
    for (i = 0u; i < BMS_CONFIG_BTNAME_BYTES; ++i) payload[off++] = (u8)cfg->bt_name_suffix[i];
}

static void bms_config_decode(bms_config_cache_t *cfg, const u8 *payload)
{
    u16 *protect_words = (u16 *)&cfg->protect;
    u32 *system_words = (u32 *)&cfg->system;
    u16 *afe_words = (u16 *)&cfg->afe_hw;
    u16 off = 0u;
    u16 i;

    memset(cfg, 0, sizeof(*cfg));
    for (i = 0u; i < BMS_CONFIG_PROTECT_WORDS; ++i) {
        protect_words[i] = bms_config_get_u16le(&payload[off]); off = (u16)(off + 2u);
    }
    for (i = 0u; i < BMS_CONFIG_SYSTEM_WORDS; ++i) {
        system_words[i] = bms_config_get_u32le(&payload[off]); off = (u16)(off + 4u);
    }
    for (i = 0u; i < BMS_CONFIG_AFE_WORDS; ++i) {
        afe_words[i] = bms_config_get_u16le(&payload[off]); off = (u16)(off + 2u);
    }
    for (i = 0u; i < (u16)BMS_CONFIG_CTRL_COUNT; ++i) {
        cfg->control[i] = bms_config_get_u32le(&payload[off]); off = (u16)(off + 4u);
    }
    for (i = 0u; i < BMS_CONFIG_BTNAME_BYTES; ++i) cfg->bt_name_suffix[i] = (char)payload[off++];
    cfg->bt_name_suffix[BMS_CONFIG_BTNAME_BYTES - 1u] = '\0';
    bms_feature_unpack(&cfg->system, &cfg->feature);


static int bms_config_save_cache(const bms_config_cache_t *cfg)
{
    u8 payload[BMS_CONFIG_PAYLOAD_BYTES];
    bms_config_encode(cfg, payload);
    if (!storage_record_save(&g_bms_config_store, payload)) return 0;
    g_bms_config = *cfg;
    return 1;
}

static int bms_config_ensure_ready(void)
{
    return g_bms_config_ready ? 1 : bms_config_store_init();
}

int bms_config_store_init(void)
{
    const storage_port_t *port;
    storage_region_t region;
    u8 payload[BMS_CONFIG_PAYLOAD_BYTES];
    if (g_bms_config_ready) return 1;
    port = bms_storage_platform_port();
    if ((port == 0) || !bms_storage_platform_region(BMS_STORAGE_DOMAIN_CONFIG, &region) ||
        !storage_record_open(&g_bms_config_store, port, region, BMS_CONFIG_RECORD_MAGIC,
                             BMS_CONFIG_SCHEMA_VERSION, BMS_CONFIG_PAYLOAD_BYTES)) return 0;
    if (storage_record_load(&g_bms_config_store, payload)) {
        bms_config_decode(&g_bms_config, payload);
        if (!bms_config_feature_valid(&g_bms_config.feature)) {
            bms_config_feature_defaults(&g_bms_config.feature);
            bms_feature_pack(&g_bms_config.system, &g_bms_config.feature);
        }
    } else bms_config_defaults(&g_bms_config);
    g_bms_config_ready = 1u;
    return 1;
}

int bms_config_store_get_protect(struct PRT_E2ROM_PARAS *protect)
{
    if ((protect == 0) || !bms_config_ensure_ready()) return 0;
    *protect = g_bms_config.protect;
    return 1;
}

int bms_config_store_set_protect(const struct PRT_E2ROM_PARAS *protect)
{
    bms_config_cache_t next;
    if ((protect == 0) || !bms_config_ensure_ready()) return 0;
    if (memcmp(&g_bms_config.protect, protect, sizeof(*protect)) == 0) return 1;
    next = g_bms_config; next.protect = *protect;
    return bms_config_save_cache(&next);
}

int bms_config_store_get_system(bms_config_system_params_t *system)
{
    if ((system == 0) || !bms_config_ensure_ready()) return 0;
    *system = g_bms_config.system;
    return 1;
}

int bms_config_store_set_system(const bms_config_system_params_t *system)
{
    bms_config_cache_t next;
    if ((system == 0) || !bms_config_ensure_ready()) return 0;
    if ((system->battery_chemistry > BMS_SOC_CHEMISTRY_NMC) ||
        (system->soc_profile_id > BMS_SOC_PROFILE_GENERIC_NMC) ||
        ((system->battery_chemistry == BMS_SOC_CHEMISTRY_LFP) && (system->soc_profile_id == BMS_SOC_PROFILE_GENERIC_NMC)) ||
        ((system->battery_chemistry == BMS_SOC_CHEMISTRY_NMC) && (system->soc_profile_id == BMS_SOC_PROFILE_GENERIC_LFP))) return 0;
    if (memcmp(&g_bms_config.system, system, sizeof(*system)) == 0) return 1;
    next = g_bms_config;
    next.system = *system;
    /* flags/reserved0 are owned by feature config on D011. */
    bms_feature_pack(&next.system, &next.feature);
    return bms_config_save_cache(&next);
}

int bms_config_store_get_afe_hw_profile(bms_afe_hw_profile_t *profile)
{
    if ((profile == 0) || !bms_config_ensure_ready()) return 0;
    *profile = g_bms_config.afe_hw;
    return 1;
}

int bms_config_store_set_afe_hw_profile(const bms_afe_hw_profile_t *profile)
{
    bms_config_cache_t next;
    if ((profile == 0) || !bms_config_ensure_ready()) return 0;
    if (memcmp(&g_bms_config.afe_hw, profile, sizeof(*profile)) == 0) return 1;
    next = g_bms_config; next.afe_hw = *profile;
    return bms_config_save_cache(&next);
}

int bms_config_store_get_control_value(bms_config_control_param_id_t item, u32 *value)
{
    if ((value == 0) || ((u32)item >= (u32)BMS_CONFIG_CTRL_COUNT) || !bms_config_ensure_ready()) return 0;
    *value = g_bms_config.control[(u16)item];
    return 1;
}

int bms_config_store_set_control_value(bms_config_control_param_id_t item, u32 value)
{
    bms_config_cache_t next;
    if (((u32)item >= (u32)BMS_CONFIG_CTRL_COUNT) || !bms_config_ensure_ready()) return 0;
    if (g_bms_config.control[(u16)item] == value) return 1;
    next = g_bms_config; next.control[(u16)item] = value;
    return bms_config_save_cache(&next);
}

int bms_config_store_get_bt_name_suffix(char *suffix, u16 suffix_size)
{
    u16 i = 0u;
    u16 limit;
    if ((suffix == 0) || (suffix_size == 0u) || !bms_config_ensure_ready()) return 0;
    limit = (u16)(suffix_size - 1u);
    if (limit > BTNAME_SUFFIX_MAX_LEN) limit = BTNAME_SUFFIX_MAX_LEN;
    while ((i < limit) && (g_bms_config.bt_name_suffix[i] != '\0')) {
        suffix[i] = g_bms_config.bt_name_suffix[i]; ++i;
    }
    suffix[i] = '\0';
    return 1;
}

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

int bms_config_get_features(bms_feature_params_t *value)
{
    if ((value == 0) || !bms_config_ensure_ready()) return 0;
    if (!bms_config_feature_valid(&g_bms_config.feature)) return 0;
    *value = g_bms_config.feature;
    return 1;
}

int bms_config_set_features(const bms_feature_params_t *value)
{
    bms_config_cache_t next;
    if (!bms_config_feature_valid(value) || !bms_config_ensure_ready()) return 0;
    if (memcmp(&g_bms_config.feature, value, sizeof(*value)) == 0) return 1;
    next = g_bms_config;
    next.feature = *value;
    bms_feature_pack(&next.system, &next.feature);
    return bms_config_save_cache(&next);
}
