/* 休眠配置、独立持久化及公共保护性休眠策略；硬件动作由 app_power.c 执行。 */
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

static uint32_t sleep_elapsed_limit(uint32_t elapsed, uint32_t increment, uint32_t limit)
{
    if (elapsed >= limit || increment >= limit - elapsed) return limit;
    return elapsed + increment;
}

static void sleep_select_reason(bms_sleep_status_t *status, uint8_t reason,
                                        uint32_t elapsed_seconds, uint32_t delay_seconds)
{
    uint32_t elapsed_ms = elapsed_seconds * 1000u;
    uint32_t delay_ms = delay_seconds * 1000u;
    if (status->reason == BMS_SLEEP_REASON_NONE ||
        delay_ms - elapsed_ms <= status->delay_ms - status->elapsed_ms) {
        status->reason = reason;
        status->elapsed_ms = elapsed_ms;
        status->delay_ms = delay_ms;
    }
}

/* 纯业务策略：输入均为已验证快照，不读取时间、Flash、AFE 或 MCU 外设。 */
void bms_sleep_policy_step(bms_sleep_policy_t *state, const bms_sleep_config_t *config,
                           const bms_sleep_input_t *input, uint32_t elapsed_sec,
                           bms_sleep_status_t *status)
{
    uint8_t conditions = state->voltage_conditions;
    uint8_t afe_fault = !input->valid || input->afe_fault;
    uint32_t low_delay, normal_delay, afe_delay;
    low_delay = (uint32_t)config->low_delay_minutes * 60u;
    normal_delay = (uint32_t)config->normal_delay_minutes * 60u;
    afe_delay = BMS_SLEEP_AFE_FIXED_MINUTES * 60u;
    memset(status, 0, sizeof(*status));
    if (input->valid) {
        conditions = 0u;
        if (input->cell_min_mv < config->low_cell_mv)
            conditions |= BMS_SLEEP_VOLTAGE_LOW_ACTIVE;
        if (input->cell_min_mv < config->normal_cell_mv &&
            input->current_ma >= -BMS_SLEEP_CHARGE_CURRENT_MA)
            conditions |= BMS_SLEEP_VOLTAGE_NORMAL_ACTIVE;
    }
    /* 下档不排除充电，上档只在无可靠充电时累计；保持原有两条件资格。 */
    state->voltage_conditions = conditions;
    state->low_voltage_seconds = (conditions & BMS_SLEEP_VOLTAGE_LOW_ACTIVE) ?
        sleep_elapsed_limit(state->low_voltage_seconds, elapsed_sec, low_delay) : 0u;
    state->normal_voltage_seconds = (conditions & BMS_SLEEP_VOLTAGE_NORMAL_ACTIVE) ?
        sleep_elapsed_limit(state->normal_voltage_seconds, elapsed_sec,
                             normal_delay) : 0u;
    state->afe_error_seconds = afe_fault ?
        sleep_elapsed_limit(state->afe_error_seconds, elapsed_sec,
                             afe_delay) : 0u;
    /* fault_third 已合并软件/AFE 欠压。通信恢复重初始化不能冒充电池恢复。 */
    if (input->overdischarge_fault)
        state->overdischarge_active = 1u;
    else if (input->valid && !afe_fault &&
             input->cell_min_mv >= input->cell_uvp_recover_mv &&
             input->pack_voltage_10mv >= input->pack_uvp_recover_10mv)
        state->overdischarge_active = 0u;
    state->overdischarge_seconds = state->overdischarge_active ?
        sleep_elapsed_limit(state->overdischarge_seconds, elapsed_sec,
                             BMS_SLEEP_OVERDISCHARGE_SECONDS) : 0u;

    if ((conditions & BMS_SLEEP_VOLTAGE_LOW_ACTIVE))
        sleep_select_reason(status, BMS_SLEEP_REASON_LOW,
            state->low_voltage_seconds, low_delay);
    if ((conditions & BMS_SLEEP_VOLTAGE_NORMAL_ACTIVE))
        sleep_select_reason(status, BMS_SLEEP_REASON_NORMAL,
            state->normal_voltage_seconds, normal_delay);
    if (afe_fault)
        sleep_select_reason(status, BMS_SLEEP_REASON_AFE,
            state->afe_error_seconds, afe_delay);
    if (state->overdischarge_active)
        sleep_select_reason(status, BMS_SLEEP_REASON_OVERDISCHARGE,
            state->overdischarge_seconds, BMS_SLEEP_OVERDISCHARGE_SECONDS);
}
