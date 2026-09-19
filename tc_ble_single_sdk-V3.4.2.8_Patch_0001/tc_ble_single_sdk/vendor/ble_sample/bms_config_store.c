#include "bms_diag.h"
#include "bms_config_store.h"

#include "bms_soc_defs.h"
#include "d008_product_profile.h"
#include "bms_sw_protection.h"
#include "bms_features.h"
#include "btname_modbus.h"
#include "bms_storage_platform.h"
#include "storage_record.h"
#include <string.h>

#define BMS_CONFIG_RECORD_MAGIC          0x43464731u /* CFG1 */
#define BMS_CONFIG_SCHEMA_VERSION        4u
#define BMS_CONFIG_PROTECT_WORDS         65u
#define BMS_CONFIG_SYSTEM_WORDS          10u
#define BMS_CONFIG_AFE_WORDS             35u
#define BMS_CONFIG_BTNAME_BYTES          24u
#define BMS_CONFIG_USER_BYTES            54u

#define BMS_CONFIG_PROTECT_BYTES         (BMS_CONFIG_PROTECT_WORDS * 2u)
#define BMS_CONFIG_SYSTEM_BYTES          (BMS_CONFIG_SYSTEM_WORDS * 4u)
#define BMS_CONFIG_AFE_BYTES             (BMS_CONFIG_AFE_WORDS * 2u)
#define BMS_CONFIG_CONTROL_BYTES         ((u16)BMS_CONFIG_CTRL_COUNT * 4u)
#define BMS_CONFIG_PAYLOAD_BYTES         (BMS_CONFIG_PROTECT_BYTES + BMS_CONFIG_SYSTEM_BYTES + BMS_CONFIG_AFE_BYTES + BMS_CONFIG_CONTROL_BYTES + BMS_CONFIG_BTNAME_BYTES + 8u + BMS_CONFIG_USER_BYTES)

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
    bms_soc_config_t soc;
    bms_user_params_t user;
    char bt_name_suffix[BMS_CONFIG_BTNAME_BYTES];
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
    system->battery_chemistry = D008_PRODUCT_CHEMISTRY;
    system->soc_profile_id = D008_PRODUCT_SOC_PROFILE_ID;
}

static void bms_config_defaults(bms_config_cache_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    bms_config_store_get_default_protect(&cfg->protect);
    bms_config_store_get_default_system(&cfg->system);
    bms_soc_get_default_config(&cfg->soc);
    cfg->soc.chemistry = (u8)cfg->system.battery_chemistry;
    cfg->soc.profile_id = (u8)cfg->system.soc_profile_id;
    bms_afe_hw_profile_build_default(&cfg->afe_hw);
    bms_config_user_defaults(&cfg->user);
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
    bms_config_put_u16le(&payload[off], cfg->soc.current_deadband_ma); off += 2u;
    bms_config_put_u16le(&payload[off], cfg->soc.ocv_rest_prepare_s); off += 2u;
    payload[off++] = cfg->soc.ocv_error_band_percent;
    payload[off++] = cfg->soc.capacity_learning_enable;
    payload[off++] = cfg->soc.hide_capacity_until_learned;
    payload[off++] = 0u;
    bms_config_put_u16le(&payload[off], cfg->user.heater_enable); off += 2u;
    bms_config_put_u16le(&payload[off], cfg->user.heater_start_x10); off += 2u;
    bms_config_put_u16le(&payload[off], cfg->user.heater_stop_x10); off += 2u;
    bms_config_put_u16le(&payload[off], cfg->user.balance_enable); off += 2u;
    bms_config_put_u16le(&payload[off], cfg->user.balance_start_mv); off += 2u;
    bms_config_put_u16le(&payload[off], cfg->user.balance_start_delta_mv); off += 2u;
    bms_config_put_u16le(&payload[off], cfg->user.balance_stop_delta_mv); off += 2u;
    bms_config_put_u32le(&payload[off], (u32)cfg->user.current_offset_ma); off += 4u;
    bms_config_put_u32le(&payload[off], cfg->user.current_gain_ppm); off += 4u;
    memcpy(&payload[off], cfg->user.serial, sizeof(cfg->user.serial));

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
    cfg->soc.chemistry = (u8)cfg->system.battery_chemistry;
    cfg->soc.profile_id = (u8)cfg->system.soc_profile_id;
    cfg->soc.current_deadband_ma = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->soc.ocv_rest_prepare_s = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->soc.ocv_error_band_percent = payload[off++];
    cfg->soc.capacity_learning_enable = payload[off++];
    cfg->soc.hide_capacity_until_learned = payload[off++];
    off++; /* reserved */
    cfg->user.heater_enable = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->user.heater_start_x10 = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->user.heater_stop_x10 = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->user.balance_enable = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->user.balance_start_mv = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->user.balance_start_delta_mv = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->user.balance_stop_delta_mv = bms_config_get_u16le(&payload[off]); off += 2u;
    cfg->user.current_offset_ma = (int32_t)bms_config_get_u32le(&payload[off]); off += 4u;
    cfg->user.current_gain_ppm = bms_config_get_u32le(&payload[off]); off += 4u;
    memcpy(cfg->user.serial, &payload[off], sizeof(cfg->user.serial));

}

static int bms_config_save_cache(const bms_config_cache_t *cfg)
{
    u8 payload[BMS_CONFIG_PAYLOAD_BYTES];
    bms_config_encode(cfg, payload);
    if (!storage_record_save(&g_bms_config_store, payload)) {
        bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_SAVE); return 0;
    }
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
    bms_diag_attempt(BMS_STORAGE_DOMAIN_CONFIG);
    port = bms_storage_platform_port();
    if (port == 0) { bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_PORT); return 0; }
    if (!bms_storage_platform_region(BMS_STORAGE_DOMAIN_CONFIG, &region)) { return 0; }
    if (!storage_record_open(&g_bms_config_store, port, region, BMS_CONFIG_RECORD_MAGIC,
                             BMS_CONFIG_SCHEMA_VERSION, BMS_CONFIG_PAYLOAD_BYTES)) {
        bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_OPEN); return 0;
    }
    if (storage_record_load(&g_bms_config_store, payload)) bms_config_decode(&g_bms_config, payload);
    else { bms_config_defaults(&g_bms_config); bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_DEFAULTS); }
    g_bms_config_ready = 1u;
    bms_diag_result(BMS_STORAGE_DOMAIN_CONFIG, DIAG_OK);
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
    if (system->capacity_factory == 0u || system->capacity_factory > BMS_SOC_CAPACITY_MAX_0P1AH) return 0;
    next = g_bms_config; next.system = *system;
    next.soc.chemistry = (u8)system->battery_chemistry;
    next.soc.profile_id = (u8)system->soc_profile_id;
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

/* Startup only, before AFE initialization. All affected Config categories and
 * their revisions commit together; no RAM publication on failed persistence. */
int bms_config_store_apply_revisions(void)
{
    bms_config_cache_t next;
    bms_config_system_params_t defaults;
    u16 invalid_mask = 0u;
    bms_diag_upgrade(DIAG_UPGRADE_STARTED, 0u);
    if (!bms_config_ensure_ready()) {
        bms_diag_upgrade(DIAG_UPGRADE_CONFIG_LOAD, 0u); return 0;
    }
    bms_diag_boot_u32(106u, g_bms_config.control[BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH]);
    bms_diag_boot_u32(108u, FW_UPGRADE_RESET_PROTECT_EPOCH);
    bms_diag_boot_word(96u, g_bms_config.protect.u16VcellUvp_First);
    bms_diag_boot_word(97u, g_bms_config.protect.u16VcellUvp_Second);
    bms_diag_boot_word(98u, g_bms_config.protect.u16VcellUvp_Third);
    bms_diag_boot_word(99u, g_bms_config.protect.u16VcellUvp_Rcv);
    next = g_bms_config;
    bms_config_store_get_default_system(&defaults);
    if (next.control[BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH] != FW_UPGRADE_RESET_PROTECT_EPOCH) {
        bms_config_store_get_default_protect(&next.protect);
        next.control[BMS_CONFIG_CTRL_PROTECT_RESET_EPOCH] = FW_UPGRADE_RESET_PROTECT_EPOCH;
    }
    if (next.control[BMS_CONFIG_CTRL_SYSTEM_RESET_EPOCH] != FW_UPGRADE_RESET_SYSTEM_EPOCH) {
        /* SOC identity/capacity belong to the independent SOC-config revision. */
        defaults.battery_chemistry = next.system.battery_chemistry;
        defaults.soc_profile_id = next.system.soc_profile_id;
        defaults.capacity_factory = next.system.capacity_factory;
        next.system = defaults;
        next.control[BMS_CONFIG_CTRL_SYSTEM_RESET_EPOCH] = FW_UPGRADE_RESET_SYSTEM_EPOCH;
    }
    if (next.control[BMS_CONFIG_CTRL_AFE_HW_RESET_EPOCH] != FW_UPGRADE_RESET_AFE_HW_EPOCH) {
        bms_afe_hw_profile_build_default(&next.afe_hw);
        next.control[BMS_CONFIG_CTRL_AFE_HW_RESET_EPOCH] = FW_UPGRADE_RESET_AFE_HW_EPOCH;
    }
    if (next.control[BMS_CONFIG_CTRL_SOC_CONFIG_RESET_EPOCH] != FW_UPGRADE_RESET_SOC_CONFIG_EPOCH) {
        bms_soc_get_default_config(&next.soc);
        next.soc.chemistry = D008_PRODUCT_CHEMISTRY;
        next.soc.profile_id = D008_PRODUCT_SOC_PROFILE_ID;
        next.system.battery_chemistry = next.soc.chemistry;
        next.system.soc_profile_id = next.soc.profile_id;
        next.system.capacity_factory = CapacityFactory;
        next.control[BMS_CONFIG_CTRL_SOC_CONFIG_RESET_EPOCH] = FW_UPGRADE_RESET_SOC_CONFIG_EPOCH;
    }
    bms_diag_boot_word(100u, next.protect.u16VcellUvp_First);
    bms_diag_boot_word(101u, next.protect.u16VcellUvp_Second);
    bms_diag_boot_word(102u, next.protect.u16VcellUvp_Third);
    bms_diag_boot_word(103u, next.protect.u16VcellUvp_Rcv);
    /* Evaluate each pure validator so simultaneous failures remain visible. */
    if (!bms_sw_protection_validate_params(&next.protect)) invalid_mask |= DIAG_UPGRADE_BAD_SW;
    if (!bms_afe_hw_profile_validate(&next.afe_hw)) invalid_mask |= DIAG_UPGRADE_BAD_AFE;
    if (!bms_soc_config_valid(&next.soc)) invalid_mask |= DIAG_UPGRADE_BAD_SOC;
    if (next.system.capacity_factory == 0u || next.system.capacity_factory > BMS_SOC_CAPACITY_MAX_0P1AH)
        invalid_mask |= DIAG_UPGRADE_BAD_CAPACITY;
    if (!bms_config_user_valid(&next.user)) invalid_mask |= DIAG_UPGRADE_BAD_SW;
    if (invalid_mask) {
        bms_diag_upgrade(DIAG_UPGRADE_VALIDATION, invalid_mask); return 0;
    }
    if (!(g_bms_config_store.has_latest && memcmp(&next, &g_bms_config, sizeof(next)) == 0) &&
        !bms_config_save_cache(&next)) {
        bms_diag_upgrade(DIAG_UPGRADE_SAVE, 0u); return 0;
    }
    bms_diag_upgrade(DIAG_UPGRADE_CONFIG_OK, 0u);
    return 1;
}

int bms_config_store_get_soc(bms_soc_config_t *config)
{
    if (config == 0 || !bms_config_ensure_ready()) return 0;
    *config = g_bms_config.soc;
    return bms_soc_config_valid(config);
}

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
        g_bms_config.soc.ocv_error_band_percent == config->ocv_error_band_percent &&
        g_bms_config.soc.capacity_learning_enable == config->capacity_learning_enable &&
        g_bms_config.soc.hide_capacity_until_learned == config->hide_capacity_until_learned) return 1;
    return bms_config_save_cache(&next);
}

void bms_config_user_defaults(bms_user_params_t *v)
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
    /* Empty SN uses the compiled identity until factory provisioning. */
}

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
    /* Arithmetic/configuration bounds, not protection thresholds. */
    if (v->current_offset_ma < -1000000 || v->current_offset_ma > 1000000 ||
        v->current_gain_ppm < 100000u || v->current_gain_ppm > 10000000u) return 0;
    for (i=0u; i<sizeof(v->serial); ++i)
        if (v->serial[i] && ((u8)v->serial[i] < 32u || (u8)v->serial[i] > 126u)) return 0;
    return 1;
}
int bms_config_get_user(bms_user_params_t *v)
{
    if (!v || !bms_config_ensure_ready()) return 0;
    *v = g_bms_config.user;
    return bms_config_user_valid(v);
}
int bms_config_set_user(const bms_user_params_t *v)
{
    bms_config_cache_t next;
    if (!bms_config_user_valid(v) || !bms_config_ensure_ready()) return 0;
    next = g_bms_config; next.user = *v;
    if (!memcmp(&next, &g_bms_config, sizeof(next))) return 1;
    return bms_config_save_cache(&next);
}
int bms_config_reset_business(void)
{
    bms_config_cache_t next;
    if (!bms_config_ensure_ready()) return 0;
    next = g_bms_config;
    next.system.capacity_factory = CapacityFactory;
    next.user.heater_enable = 1u;
    next.user.heater_start_x10 = BMS_HEATER_START_TEMP_X10;
    next.user.heater_stop_x10 = BMS_HEATER_STOP_TEMP_X10;
    next.user.balance_enable = BMS_BALANCE_ENABLE_DEFAULT;
    next.user.balance_start_mv = BMS_BALANCE_START_VOLTAGE_MV_DEFAULT;
    next.user.balance_start_delta_mv = BMS_BALANCE_START_DELTA_MV_DEFAULT;
    next.user.balance_stop_delta_mv = BMS_BALANCE_STOP_DELTA_MV_DEFAULT;
    return bms_config_save_cache(&next);
}
/* Exact floor(magnitude * gain / 1000000), without 64-bit runtime helpers
 * absent from the pinned TC32 ABI. Each of 32 steps keeps remainder < 1e6;
 * gain <= 1e7 makes the intermediate <= 11999998. Saturate before overflow. */
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
int32_t bms_config_calibrate_current(int32_t raw_ma)
{
    int32_t offset, delta;
    u32 magnitude, scaled;
    if (!g_bms_config_ready || !bms_config_user_valid(&g_bms_config.user)) return raw_ma;
    offset=g_bms_config.user.current_offset_ma;
    if (offset>0 && raw_ma < -2147483647+offset) delta=-2147483647;
    else if (offset<0 && raw_ma > 2147483647+offset) delta=2147483647;
    else delta=raw_ma-offset;
    magnitude=delta<0 ? 0u-(u32)delta : (u32)delta;
    scaled=current_scale_ppm(magnitude,g_bms_config.user.current_gain_ppm);
    return delta<0 ? -(int32_t)scaled : (int32_t)scaled;
}
