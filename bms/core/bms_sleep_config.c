/* 五项休眠配置及其独立持久化；计时与入睡动作由 app_power.c 持有。 */
#include "bms_sleep_config.h"
#include "bms_product.h"
#include "bms_storage_platform.h"
#include "storage_record.h"
#include "bms_error.h"
#include <string.h>

#define SLEEP_RECORD_MAGIC 0x534C5031u /* SLP1 */
#define SLEEP_PAYLOAD_BYTES (BMS_SLEEP_CONFIG_WORDS * 2u)

#if BMS_SLEEP_LOW_SECONDS < 60u || BMS_SLEEP_LOW_SECONDS % 60u != 0u || \
    BMS_SLEEP_LOW_SECONDS > BMS_SLEEP_NORMAL_SECONDS || \
    BMS_SLEEP_NORMAL_SECONDS % 60u != 0u || BMS_SLEEP_NORMAL_SECONDS / 60u > 65535u || \
    BMS_SLEEP_LOW_CELL_MV < 1000u || BMS_SLEEP_LOW_CELL_MV >= BMS_SLEEP_NORMAL_CELL_MV || \
    BMS_SLEEP_NORMAL_CELL_MV > 4500u
#error "Sleep defaults require ordered voltages and whole-minute delays in 1..65535"
#endif

/* 与其他 journal 相同：独立四字节对齐，避免 TC32 packed 布局误读端口。 */
static storage_record_store_t s_sleep_store __attribute__((aligned(4)));
static bms_sleep_config_t s_sleep_config;
static uint8_t s_sleep_writable;
static uint8_t s_sleep_record_valid;

typedef char sleep_config_is_five_words[
    sizeof(bms_sleep_config_t) == SLEEP_PAYLOAD_BYTES ? 1 : -1];

void bms_sleep_config_defaults(bms_sleep_config_t *config)
{
    if (!config) return;
    config->low_cell_mv = BMS_SLEEP_LOW_CELL_MV;
    config->low_delay_minutes = BMS_SLEEP_LOW_SECONDS / 60u;
    config->normal_cell_mv = BMS_SLEEP_NORMAL_CELL_MV;
    config->normal_delay_minutes = BMS_SLEEP_NORMAL_SECONDS / 60u;
    config->afe_delay_minutes = BMS_SLEEP_AFE_FIXED_MINUTES;
}

int bms_sleep_config_valid(const bms_sleep_config_t *config)
{
    return config && config->low_cell_mv >= 1000u &&
        config->low_cell_mv < config->normal_cell_mv && config->normal_cell_mv <= 4500u &&
        config->low_delay_minutes != 0u &&
        config->low_delay_minutes <= config->normal_delay_minutes &&
        config->afe_delay_minutes == BMS_SLEEP_AFE_FIXED_MINUTES;
}

/* 显式按 word 编码，不把 packed 结构转换为可能未对齐的 u16 指针。 */
static void sleep_config_codec(bms_sleep_config_t *config, uint8_t *payload, uint8_t encode)
{
    uint16_t i, value;
    for (i = 0u; i < BMS_SLEEP_CONFIG_WORDS; ++i) {
        if (encode) {
            memcpy(&value, (uint8_t *)config + i * 2u, sizeof(value));
            payload[i * 2u] = (uint8_t)value;
            payload[i * 2u + 1u] = (uint8_t)(value >> 8);
        } else {
            value = (uint16_t)(payload[i * 2u] | ((uint16_t)payload[i * 2u + 1u] << 8));
            memcpy((uint8_t *)config + i * 2u, &value, sizeof(value));
        }
    }
}

void bms_sleep_config_init(void)
{
    storage_region_t region;
    bms_sleep_config_t loaded;
    uint8_t payload[SLEEP_PAYLOAD_BYTES];
    const storage_port_t *port = bms_storage_platform_port();
    s_sleep_writable = 0u;
    s_sleep_record_valid = 0u;
    bms_sleep_config_defaults(&s_sleep_config);
    if (!port || !bms_storage_platform_region(BMS_STORAGE_DOMAIN_SLEEP_CONFIG, &region) ||
        !storage_record_open(&s_sleep_store, port, region, SLEEP_RECORD_MAGIC, 1u, SLEEP_PAYLOAD_BYTES)) {
        bms_error_raise(BMS_ERROR_EEPROM_COM);
        return;
    }
    if (storage_record_load(&s_sleep_store, payload)) {
        sleep_config_codec(&loaded, payload, 0u);
        /* 旧版可保存此值；升级后仅保留槽位，不再允许它影响实际休眠时间。 */
        loaded.afe_delay_minutes = BMS_SLEEP_AFE_FIXED_MINUTES;
        if (!bms_sleep_config_valid(&loaded)) {
            bms_error_raise(BMS_ERROR_EEPROM_STORE);
        } else {
            s_sleep_config = loaded;
            s_sleep_record_valid = 1u;
        }
    } else if (s_sleep_store.load_status == STORAGE_RECORD_LOAD_IO_ERROR) {
        bms_error_raise(BMS_ERROR_EEPROM_COM);
        return; /* I/O 错误不能当空白写入。 */
    } else if (s_sleep_store.load_status != STORAGE_RECORD_LOAD_EMPTY) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
    }
    /* 空白/坏记录只发布默认值，不自动写 Flash；显式保存可修复首次掉电残留。 */
    s_sleep_writable = 1u;
}

void bms_sleep_config_get(bms_sleep_config_t *config)
{
    if (config) *config = s_sleep_config;
}

int bms_sleep_config_set(const bms_sleep_config_t *config)
{
    uint8_t payload[SLEEP_PAYLOAD_BYTES];
    bms_sleep_config_t next;
    if (!s_sleep_writable || !bms_sleep_config_valid(config)) return 0;
    if (s_sleep_record_valid && !memcmp(config, &s_sleep_config, sizeof(*config))) return 1;
    next = *config;
    sleep_config_codec(&next, payload, 1u);
    if (!storage_record_save(&s_sleep_store, payload)) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0;
    }
    s_sleep_config = next;
    s_sleep_record_valid = 1u;
    return 1;
}
