/*
 * 文件功能：SOC/循环数据的状态记录；
 * 管理缓存、变化保存和恢复默认入口。
 * bms/core/bms_state_store.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_diag.h"
#include "bms_update_policy.h"
#include "bms_state_store.h"
#include "bms_product.h"

#include "bms_storage_platform.h"
#include "storage_record.h"
#include "bms_time.h"
#include "bms_error.h"
#include <string.h>

#define BMS_STATE_SAVE_INTERVAL_MS (60u * 1000u)

#define BMS_STATE_RECORD_MAGIC        0x53543500u + BMS_PRODUCT_ID /*
 * 状态标识为 ST5 加产品编号。
 */
#define BMS_STATE_SCHEMA_VERSION      5u
#define BMS_STATE_LEGACY_MAGIC (0x53543400u + BMS_PRODUCT_ID)
#define BMS_STATE_LEGACY_BYTES (12u + BMS_OTA_UPDATE_ID_WORDS * 2u)
#define BMS_STATE_PAYLOAD_BYTES (BMS_STATE_LEGACY_BYTES + 8u)

typedef struct {
    uint16_t ota_update_id[BMS_OTA_UPDATE_ID_WORDS];
    uint32_t soc;
    uint32_t dsg;
    uint32_t cycle;
    uint32_t full_confirmed;
    uint32_t full_basis;
} bms_state_persist_t;

static storage_record_store_t g_bms_state_store __attribute__((aligned(4)));
static bms_state_persist_t g_bms_state;
static uint8_t g_bms_state_ready;
static uint8_t s_state_loaded;
static uint8_t s_migration_pending;
static uint32_t g_bms_state_last_attempt_ms;
static uint8_t g_bms_state_last_failed;

/* 将 32 位值按小端写入存储缓冲区。 */
static void bms_state_put_u32le(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value & 0xFFu);
    buf[1] = (uint8_t)((value >> 8) & 0xFFu);
    buf[2] = (uint8_t)((value >> 16) & 0xFFu);
    buf[3] = (uint8_t)((value >> 24) & 0xFFu);
}

/* 从存储缓冲区按小端读取 32 位值。 */
static uint32_t bms_state_get_u32le(const uint8_t *buf)
{
    return ((uint32_t)buf[0]) | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

/* 取得 SOC 和循环状态默认值。 */
bms_state_store_data_t bms_state_store_get_default_data(void)
{
    bms_state_store_data_t data;
    memset(&data, 0, sizeof(data));
    data.soc = BMS_STATE_DEFAULT_SOC;
    data.dsg = BMS_STATE_DEFAULT_DSG;
    data.cycle = BMS_STATE_DEFAULT_CYCLE;
    return data;
}

/* 建立持久状态域的默认缓存。 */
static void bms_state_defaults(bms_state_persist_t *state)
{
    bms_state_store_data_t soc = bms_state_store_get_default_data();
    memset(state->ota_update_id, 0, sizeof(state->ota_update_id));
    if (bms_ota_update_selected(BMS_UPDATE_SOC_STATE)) bms_ota_update_copy_id(state->ota_update_id);
    state->soc = soc.soc;
    state->dsg = soc.dsg;
    state->cycle = soc.cycle;
    state->full_confirmed = 0u;
    state->full_basis = 0u;
}

/* 按固定存储格式编码 SOC 和循环状态。 */
static void bms_state_encode(const bms_state_persist_t *state, uint8_t *payload)
{
    unsigned i;
    bms_state_put_u32le(&payload[0], state->soc);
    bms_state_put_u32le(&payload[4], state->dsg);
    bms_state_put_u32le(&payload[8], state->cycle);
    bms_state_put_u32le(&payload[BMS_STATE_LEGACY_BYTES], state->full_confirmed);
    bms_state_put_u32le(&payload[BMS_STATE_LEGACY_BYTES + 4u], state->full_basis);
    for (i = 0u; i < BMS_OTA_UPDATE_ID_WORDS; ++i) {
        payload[12u + 2u * i] = (uint8_t)state->ota_update_id[i];
        payload[13u + 2u * i] = (uint8_t)(state->ota_update_id[i] >> 8);
    }
}

/* 验证版本、产品及字段范围后解码状态记录。 */
static void bms_state_decode(bms_state_persist_t *state, const uint8_t *payload, uint8_t legacy)
{
    unsigned i;
    state->full_confirmed = legacy ? 0u : bms_state_get_u32le(&payload[BMS_STATE_LEGACY_BYTES]);
    state->full_basis = legacy ? 0u : bms_state_get_u32le(&payload[BMS_STATE_LEGACY_BYTES + 4u]);
    state->soc = bms_state_get_u32le(&payload[0]);
    state->dsg = bms_state_get_u32le(&payload[4]);
    state->cycle = bms_state_get_u32le(&payload[8]);
    for (i = 0u; i < BMS_OTA_UPDATE_ID_WORDS; ++i)
        state->ota_update_id[i] = (uint16_t)((uint16_t)payload[12u + 2u * i] |
                                           ((uint16_t)payload[13u + 2u * i] << 8));
}

/* 持久状态 checkpoint；保存结果决定缓存提交，不能把 RAM 更新视为 Flash 已落盘。 */
static int bms_state_save(const bms_state_persist_t *next)
{
    uint8_t payload[BMS_STATE_PAYLOAD_BYTES];
    uint32_t now = bms_time_ms();
    /* 比较全部持久字段，避免重复编码及第二份 payload；不能比较结构体填充字节。 */
    if (!s_migration_pending && g_bms_state_store.has_latest && next->full_confirmed == g_bms_state.full_confirmed &&
        next->full_basis == g_bms_state.full_basis && next->soc == g_bms_state.soc &&
        next->dsg == g_bms_state.dsg && next->cycle == g_bms_state.cycle &&
        memcmp(next->ota_update_id, g_bms_state.ota_update_id, sizeof(next->ota_update_id)) == 0) return 1;
    /* 强制关机写入绕过普通间隔，但绝不绕过失败退避。 */
    if (g_bms_state_last_failed &&
        (uint32_t)(now - g_bms_state_last_attempt_ms) < BMS_STORAGE_RETRY_INTERVAL_MS) return 0;
    g_bms_state_last_attempt_ms = now;
    bms_state_encode(next, payload);
    if (!storage_record_save(&g_bms_state_store, payload)) {
        g_bms_state_last_failed = 1u;
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0;
    }
    g_bms_state_last_failed = 0u;
    g_bms_state = *next;
    s_state_loaded = 1u;
    s_migration_pending = 0u;
    return 1;
}

/* 加载并验证持久状态，建立当前缓存。 */
int bms_state_store_init(void)
{
    const storage_port_t *port;
    storage_region_t region;
    uint8_t payload[BMS_STATE_PAYLOAD_BYTES];
    bms_state_persist_t next;
    if (g_bms_state_ready) return 1;
    bms_diag_attempt(BMS_STORAGE_DOMAIN_STATE);
    if (g_bms_state_last_failed &&
        (uint32_t)(bms_time_ms() - g_bms_state_last_attempt_ms) < BMS_STORAGE_RETRY_INTERVAL_MS) { bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_BACKOFF); return 0; }
    port = bms_storage_platform_port();
    if (port == 0) { bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_PORT); goto invalid; }
    if (!bms_storage_platform_region(BMS_STORAGE_DOMAIN_STATE, &region)) { goto invalid; }
    if (!g_bms_state_store.ready && !storage_record_open(&g_bms_state_store, port, region, BMS_STATE_RECORD_MAGIC,
                             BMS_STATE_SCHEMA_VERSION, BMS_STATE_PAYLOAD_BYTES)) {
        bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_OPEN); goto invalid;
    }
    /* 首次加载后的写失败沿用已验证缓存与底层失败游标，不能重扫后反复擦同一扇区。 */
    if (!s_state_loaded) {
        s_migration_pending = 0u;
        if (storage_record_load(&g_bms_state_store, payload)) {
            bms_state_decode(&g_bms_state, payload, 0u);
        } else {
            storage_record_store_t legacy;
            uint8_t current_status = g_bms_state_store.load_status;
            if (current_status == STORAGE_RECORD_LOAD_IO_ERROR) { bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_INVALID); goto invalid; }
            /* 仅导入当前 ST4，绝不把任意旧格式或损坏记录当作默认值。 */
            memset(&legacy, 0, sizeof(legacy));
            if (!storage_record_open(&legacy, port, region, BMS_STATE_LEGACY_MAGIC, 4u, BMS_STATE_LEGACY_BYTES)) { bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_OPEN); goto invalid; }
            if (storage_record_load(&legacy, payload)) {
                uint32_t sector = (legacy.latest_addr - region.base) / port->erase_size;
                bms_state_decode(&g_bms_state, payload, 1u);
                /* 新槽几何不同，第一条 ST5 必须写到旧最新记录之外的扇区。
                 * has_latest/latest_addr 同时使底层失败换扇区保护旧记录。 */
                g_bms_state_store.latest_addr = legacy.latest_addr;
                g_bms_state_store.has_latest = 1u;
                g_bms_state_store.next_sequence = legacy.next_sequence;
                sector = (sector + 1u) % legacy.sector_count;
                g_bms_state_store.write_addr = region.base + sector * port->erase_size;
                g_bms_state_store.write_needs_erase = 1u;
                s_migration_pending = 1u;
            } else if (current_status == STORAGE_RECORD_LOAD_EMPTY && legacy.load_status == STORAGE_RECORD_LOAD_EMPTY) {
                bms_state_defaults(&g_bms_state);
                bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_DEFAULTS);
            } else { bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_INVALID); goto invalid; }
        }
        s_state_loaded = g_bms_state.soc <= 100u && g_bms_state.dsg <= 100u &&
            g_bms_state.cycle <= 65535u && g_bms_state.full_confirmed <= 1u &&
            (!g_bms_state.full_confirmed || g_bms_state.soc >= 99u) &&
            (g_bms_state.full_confirmed || !g_bms_state.full_basis);
    }
    next = g_bms_state;
    if (bms_ota_update_selected(BMS_UPDATE_SOC_STATE) && !bms_ota_update_id_matches(next.ota_update_id)) {
        bms_state_defaults(&next);
    }
    if (next.soc > 100u || next.dsg > 100u || next.cycle > 65535u || next.full_confirmed > 1u ||
        (next.full_confirmed && next.soc < 99u) || (!next.full_confirmed && next.full_basis)) {
        bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_INVALID); goto invalid;
    }
    if (!bms_state_save(&next)) { bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_SAVE); return 0; }
    g_bms_state_last_attempt_ms = bms_time_ms();
    g_bms_state_ready = 1u;
    bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_OK);
    return 1;
invalid:
    g_bms_state_last_failed = 1u;
    g_bms_state_last_attempt_ms = bms_time_ms();
    bms_error_raise(BMS_ERROR_EEPROM_STORE);
    return 0;
}

/* 取得缓存的 SOC 和循环状态。 */
bms_state_store_data_t bms_state_store_get(void)
{
    bms_state_store_data_t data = bms_state_store_get_default_data();
    if (!g_bms_state_ready && !s_state_loaded) return data;
    data.soc = g_bms_state.soc;
    data.dsg = g_bms_state.dsg;
    data.cycle = g_bms_state.cycle;
    data.full_confirmed = g_bms_state.full_confirmed;
    data.full_basis = g_bms_state.full_basis;
    return data;
}

/* 保存显式完整快照；只有持久成功才发布缓存。 */
int bms_state_store_save_snapshot(const bms_state_store_data_t *data)
{
    bms_state_persist_t next;
    if (!data || data->soc > 100u || data->dsg > 100u || data->cycle > 65535u ||
        data->full_confirmed > 1u || (data->full_confirmed && data->soc < 99u) ||
        (!data->full_confirmed && data->full_basis) || !bms_state_store_init()) return 0;
    next = g_bms_state;
    next.soc = data->soc; next.dsg = data->dsg; next.cycle = data->cycle;
    next.full_confirmed = data->full_confirmed; next.full_basis = data->full_basis;
    return bms_state_save(&next);
}

/* 旧服务入口保留 ABI，但人工写入不能授予电压满电资格。 */
int bms_state_store_save(uint32_t soc, uint32_t dsg, uint32_t cycle)
{
    bms_state_store_data_t data;
    memset(&data, 0, sizeof(data));
    data.soc = soc; data.dsg = dsg; data.cycle = cycle;
    return bms_state_store_save_snapshot(&data);
}

void bms_state_store_poll_snapshot(const bms_state_store_data_t *data)
{
    uint32_t interval = g_bms_state_last_failed ? BMS_STORAGE_RETRY_INTERVAL_MS : BMS_STATE_SAVE_INTERVAL_MS;
    if (!data) return;
    /* 资格变化也写一条完整记录；失败仍由统一退避限制，不在采样/ISR 内写。 */
    if (data->full_confirmed == g_bms_state.full_confirmed && data->full_basis == g_bms_state.full_basis &&
        (uint32_t)(bms_time_ms() - g_bms_state_last_attempt_ms) < interval) return;
    (void)bms_state_store_save_snapshot(data);
}

void bms_state_store_poll(uint32_t soc, uint32_t dsg, uint32_t cycle)
{
    bms_state_store_data_t data;
    memset(&data, 0, sizeof(data));
    data.soc = soc; data.dsg = dsg; data.cycle = cycle;
    bms_state_store_poll_snapshot(&data);
}

/* 显式出厂包：直接重置 State，既不解析也不迁移旧 payload。 */
int bms_state_store_factory_reset(void)
{
    const storage_port_t *port;
    storage_region_t region;
    uint8_t payload[BMS_STATE_PAYLOAD_BYTES];
    bms_state_persist_t next;
    if (!bms_ota_factory_reset_requested()) return 0;
    if (!g_bms_state_store.ready) {
        port = bms_storage_platform_port();
        if (!port || !bms_storage_platform_region(BMS_STORAGE_DOMAIN_STATE, &region) ||
            !storage_record_open(&g_bms_state_store, port, region, BMS_STATE_RECORD_MAGIC,
                                 BMS_STATE_SCHEMA_VERSION, BMS_STATE_PAYLOAD_BYTES)) return 0;
    }
    (void)storage_record_load(&g_bms_state_store, payload);
    if (g_bms_state_store.load_status == STORAGE_RECORD_LOAD_IO_ERROR) return 0;
    bms_state_defaults(&next);
    bms_ota_update_copy_id(next.ota_update_id);
    bms_state_encode(&next, payload);
    if (!storage_record_save(&g_bms_state_store, payload)) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0;
    }
    g_bms_state = next;
    s_state_loaded = 1u;
    s_migration_pending = 0u;
    g_bms_state_ready = 1u;
    g_bms_state_last_failed = 0u;
    g_bms_state_last_attempt_ms = bms_time_ms();
    return 1;
}
