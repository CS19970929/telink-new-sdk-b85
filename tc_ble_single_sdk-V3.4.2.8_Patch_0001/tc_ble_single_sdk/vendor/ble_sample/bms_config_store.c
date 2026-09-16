#include "bms_config_store.h"

#include "bms_soc_defs.h"
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
    system->battery_chemistry = BMS_SOC_CHEMISTRY_AUTO;
    system->soc_profile_id = BMS_SOC_PROFILE_AUTO;
}

static void bms_config_defaults(bms_config_cache_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    bms_config_store_get_default_protect(&cfg->protect);
    bms_config_store_get_default_system(&cfg->system);
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
}

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
    if (storage_record_load(&g_bms_config_store, payload)) bms_config_decode(&g_bms_config, payload);
    else bms_config_defaults(&g_bms_config);
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
    next = g_bms_config; next.system = *system;
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
