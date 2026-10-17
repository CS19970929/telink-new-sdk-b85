/* SOC profile 独立 journal；完整候选校验、保存成功后才发布 RAM。 */
#include "bms_soc_profile_store.h"
#include "bms_soc_profile.h"
#include "bms_config_store.h"
#include "bms_storage_platform.h"
#include "storage_record.h"
#include "bms_error.h"
#include <string.h>

#define SOC_PROFILE_RECORD_MAGIC 0x53435031u /* SCP1 */
#define SOC_PROFILE_RECORD_SCHEMA 1u
#define SOC_PROFILE_PAYLOAD_BYTES (BMS_SOC_PROFILE_WORDS * 2u)

static storage_record_store_t s_store __attribute__((aligned(4)));
static bms_soc_profile_config_t s_config;
static uint16_t s_generation;
static uint8_t s_writable;
static uint8_t s_persisted;

static const bms_soc_profile_config_t *profile_for_chemistry(uint8_t chemistry)
{
    return chemistry == BMS_SOC_CHEMISTRY_LFP ? &g_soc_profile_lfp :
        chemistry == BMS_SOC_CHEMISTRY_NMC ? &g_soc_profile_nmc : 0;
}

void bms_soc_profile_store_defaults(uint8_t chemistry, bms_soc_profile_config_t *config)
{
    const bms_soc_profile_config_t *profile = profile_for_chemistry(chemistry);
    if (config && profile) *config = *profile;
}

int bms_soc_profile_store_valid(const bms_soc_profile_config_t *c)
{
    uint16_t i;
    if (!c || !profile_for_chemistry((uint8_t)c->chemistry) ||
        c->profile_id != c->chemistry || c->profile_version == 0u ||
        c->point_count < 2u || c->point_count > BMS_SOC_PROFILE_MAX_POINTS ||
        c->current_deadband_ma > 2000u ||
        c->ocv_rest_prepare_s < 60u || c->ocv_rest_prepare_s > 3600u ||
        c->ocv_error_band_percent == 0u || c->ocv_error_band_percent > 20u ||
        c->valid_min_mv < 1000u || c->valid_min_mv >= c->valid_max_mv ||
        c->valid_max_mv > 5000u || c->empty_sync_mv < c->valid_min_mv ||
        c->empty_sync_mv >= c->full_sync_mv || c->full_sync_mv > c->valid_max_mv ||
        c->full_min_margin_mv > 1000u || c->empty_max_margin_mv > 1000u ||
        c->ocv_min_error_band_percent == 0u || c->ocv_min_error_band_percent > 20u ||
        c->full_cell_delta_max_mv > 2000u || c->terminal_cell_delta_max_mv > 2000u ||
        c->terminal_start_offset_mv < c->terminal_l1_offset_mv ||
        c->terminal_l1_offset_mv < c->terminal_l2_offset_mv ||
        c->terminal_l2_offset_mv < c->terminal_l3_offset_mv) return 0;
    if (c->points[0].soc_percent != 0u ||
        c->points[c->point_count - 1u].soc_percent != 100u) return 0;
    for (i = 0u; i < c->point_count; ++i) {
        if (c->points[i].mv < c->valid_min_mv || c->points[i].mv > c->valid_max_mv ||
            c->points[i].soc_percent > 100u) return 0;
        if (i != 0u && (c->points[i].mv <= c->points[i - 1u].mv ||
                        c->points[i].soc_percent < c->points[i - 1u].soc_percent)) return 0;
    }
    return 1;
}

uint16_t bms_soc_profile_store_word(const bms_soc_profile_config_t *c, uint16_t offset)
{
    uint16_t value = 0u;
    if (!c || offset >= BMS_SOC_PROFILE_WORDS) return 0u;
    if (offset < BMS_SOC_PROFILE_FIXED_WORDS) {
        memcpy(&value, (const uint8_t *)c + offset * 2u, sizeof(value));
        return value;
    }
    offset -= BMS_SOC_PROFILE_FIXED_WORDS;
    return (offset & 1u) ? c->points[offset / 2u].soc_percent : c->points[offset / 2u].mv;
}

int bms_soc_profile_store_set_word(bms_soc_profile_config_t *c, uint16_t offset, uint16_t value)
{
    if (!c || offset >= BMS_SOC_PROFILE_WORDS) return 0;
    if (offset < BMS_SOC_PROFILE_FIXED_WORDS) {
        memcpy((uint8_t *)c + offset * 2u, &value, sizeof(value));
        return 1;
    }
    offset -= BMS_SOC_PROFILE_FIXED_WORDS;
    if (offset & 1u) c->points[offset / 2u].soc_percent = value;
    else c->points[offset / 2u].mv = value;
    return 1;
}

static void codec(bms_soc_profile_config_t *config, uint8_t *payload, uint8_t encode)
{
    uint16_t i, value;
    for (i = 0u; i < BMS_SOC_PROFILE_WORDS; ++i) {
        if (encode) {
            value = bms_soc_profile_store_word(config, i);
            payload[i * 2u] = (uint8_t)value;
            payload[i * 2u + 1u] = (uint8_t)(value >> 8);
        } else {
            value = (uint16_t)(payload[i * 2u] | ((uint16_t)payload[i * 2u + 1u] << 8));
            (void)bms_soc_profile_store_set_word(config, i, value);
        }
    }
}

void bms_soc_profile_store_init(void)
{
    storage_region_t region;
    bms_soc_profile_config_t loaded;
    bms_soc_config_t identity;
    uint8_t payload[SOC_PROFILE_PAYLOAD_BYTES];
    const storage_port_t *port = bms_storage_platform_port();
    s_writable = 0u;
    s_persisted = 0u;
    s_generation = 1u;
    memset(&identity, 0, sizeof(identity));
    if (!bms_config_store_get_soc(&identity)) {
        identity.chemistry = BMS_SOC_CHEMISTRY_NMC;
        identity.profile_id = BMS_SOC_PROFILE_GENERIC_NMC;
    }
    bms_soc_profile_store_defaults(identity.chemistry, &s_config);
    if (!port || !bms_storage_platform_region(BMS_STORAGE_DOMAIN_SOC_PROFILE, &region) ||
        !storage_record_open(&s_store, port, region, SOC_PROFILE_RECORD_MAGIC,
                             SOC_PROFILE_RECORD_SCHEMA, SOC_PROFILE_PAYLOAD_BYTES)) {
        bms_error_raise(BMS_ERROR_EEPROM_COM);
        return;
    }
    memset(&loaded, 0, sizeof(loaded));
    if (storage_record_load(&s_store, payload)) {
        codec(&loaded, payload, 0u);
        if (bms_soc_profile_store_valid(&loaded) &&
            loaded.chemistry == identity.chemistry && loaded.profile_id == identity.profile_id) {
            s_config = loaded;
            s_persisted = 1u;
        } else {
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
        }
    } else if (s_store.load_status == STORAGE_RECORD_LOAD_IO_ERROR) {
        bms_error_raise(BMS_ERROR_EEPROM_COM);
        return;
    } else if (s_store.load_status != STORAGE_RECORD_LOAD_EMPTY) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
    }
    s_writable = 1u;
}

/* 仅存储模块可发布此对象；stage 和失败候选均不影响它。 */
const bms_soc_profile_config_t *bms_soc_profile_store_current(void)
{
    /* AUTO 没有独立默认曲线，保留算法按产品/OVP 选择的回退。 */
    return s_generation && s_config.point_count >= 2u ? &s_config : 0;
}

void bms_soc_profile_store_get(bms_soc_profile_config_t *config)
{
    if (config) *config = s_config;
}

/* 调用者拥有稳定候选；journal 成功后才发布，失败不改变曲线或代次。 */
static int save_profile(bms_soc_profile_config_t *candidate)
{
    uint8_t payload[SOC_PROFILE_PAYLOAD_BYTES];
    codec(candidate, payload, 1u);
    if (!storage_record_save(&s_store, payload)) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0;
    }
    s_config = *candidate;
    s_persisted = 1u;
    if (++s_generation == 0u) ++s_generation;
    return 1;
}

int bms_soc_profile_store_set(const bms_soc_profile_config_t *config)
{
    bms_soc_config_t identity;
    bms_soc_profile_config_t next;
    if (!s_writable || !bms_soc_profile_store_valid(config) ||
        !bms_config_store_get_soc(&identity) || config->chemistry != identity.chemistry ||
        config->profile_id != identity.profile_id) return 0;
    if (s_persisted && !memcmp(config, &s_config, sizeof(*config))) return 1;
    next = *config;
    return save_profile(&next);
}

int bms_soc_profile_store_reset(uint8_t chemistry)
{
    bms_soc_profile_config_t defaults;
    bms_soc_profile_store_defaults(chemistry, &defaults);
    if (!bms_soc_profile_store_valid(&defaults)) return 0;
    /* 类型切换在 Config 提交前调用，允许候选类型暂时与当前 Config 不同。 */
    if (!s_writable) return 0;
    return save_profile(&defaults);
}

uint16_t bms_soc_profile_store_generation(void) { return s_generation; }
uint8_t bms_soc_profile_store_is_persisted(void) { return s_persisted; }
