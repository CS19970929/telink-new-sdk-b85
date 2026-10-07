/*
 * 文件功能：SOC/循环数据的状态记录；
 * 管理缓存、变化保存和恢复默认入口。
 * bms/core/bms_state_store.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_diag.h"
#include "bms_update_policy.h"
#include "bms_state_store.h"

#include "bms_storage_platform.h"
#include "storage_record.h"
#include "drivers.h"
#include "bms_error.h"
#include <string.h>

#define BMS_STATE_RECORD_MAGIC        0x53544200u + BMS_PRODUCT_ID /*
 * 状态标识为 STB 加产品编号。
 */
#define BMS_STATE_SCHEMA_VERSION      3u
#define BMS_STATE_PAYLOAD_WORDS       10u
#define BMS_STATE_PAYLOAD_BYTES       (BMS_STATE_PAYLOAD_WORDS * 4u + 4u)

typedef struct {
    u16 soc_revision;
    u32 soc;
    u32 dsg;
    u32 cycle;
} bms_state_persist_t;

static storage_record_store_t g_bms_state_store;
static bms_state_persist_t g_bms_state;
static u8 g_bms_state_ready;
/* 待 checkpoint 的状态副本；与当前持久状态分离以保留失败后的重试内容。 */
static bms_state_persist_t g_bms_state_pending;
static u32 g_bms_state_last_attempt_32k;
static u8 g_bms_state_last_failed;
static u8 g_bms_state_attempted;

/* 将 32 位值按小端写入存储缓冲区。 */
static void bms_state_put_u32le(u8 *buf, u32 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)((value >> 8) & 0xFFu);
    buf[2] = (u8)((value >> 16) & 0xFFu);
    buf[3] = (u8)((value >> 24) & 0xFFu);
}

/* 从存储缓冲区按小端读取 32 位值。 */
static u32 bms_state_get_u32le(const u8 *buf)
{
    return ((u32)buf[0]) | ((u32)buf[1] << 8) |
           ((u32)buf[2] << 16) | ((u32)buf[3] << 24);
}

/* 取得 SOC 和循环状态默认值。 */
bms_state_store_data_t bms_state_store_get_default_data(void)
{
    bms_state_store_data_t data;
    data.soc = BMS_STATE_DEFAULT_SOC;
    data.dsg = BMS_STATE_DEFAULT_DSG;
    data.cycle = BMS_STATE_DEFAULT_CYCLE;
    return data;
}

/* 建立持久状态域的默认缓存。 */
static void bms_state_defaults(bms_state_persist_t *state)
{
    bms_state_store_data_t soc = bms_state_store_get_default_data();
    state->soc_revision = BMS_UPDATE_SOC_STATE_REVISION;
    state->soc = soc.soc;
    state->dsg = soc.dsg;
    state->cycle = soc.cycle;
}

/* 按固定存储格式编码 SOC 和循环状态。 */
static void bms_state_encode(const bms_state_persist_t *state, u8 *payload)
{
    bms_state_put_u32le(&payload[0], state->soc);
    bms_state_put_u32le(&payload[4], state->dsg);
    bms_state_put_u32le(&payload[8], state->cycle);
    memset(&payload[12], 0, 28u); /* schema 3 原学习槽保留，编码恒零。 */
    bms_state_put_u32le(&payload[40], (u32)state->soc_revision);
}

/* 验证版本、产品及字段范围后解码状态记录。 */
static void bms_state_decode(bms_state_persist_t *state, const u8 *payload)
{
    state->soc = bms_state_get_u32le(&payload[0]);
    state->dsg = bms_state_get_u32le(&payload[4]);
    state->cycle = bms_state_get_u32le(&payload[8]);
    state->soc_revision = (u16)bms_state_get_u32le(&payload[40]);
}

/* 持久状态 checkpoint；保存结果决定缓存提交，不能把 RAM 更新视为 Flash 已落盘。 */
static int bms_state_save(const bms_state_persist_t *next)
{
    u8 payload[BMS_STATE_PAYLOAD_BYTES];
    u32 now = pm_get_32k_tick();
    u8 previous[BMS_STATE_PAYLOAD_BYTES];
    bms_state_encode(next, payload);
    bms_state_encode(&g_bms_state, previous);
    if (g_bms_state_store.has_latest && memcmp(previous, payload, sizeof(payload)) == 0) return 1;
    /* 强制关机写入绕过普通间隔，但绝不绕过失败退避。 */
    if (g_bms_state_attempted && g_bms_state_last_failed &&
        (u32)(now - g_bms_state_last_attempt_32k) < BMS_STORAGE_RETRY_INTERVAL_32K) return 0;
    g_bms_state_attempted = 1u;
    g_bms_state_last_attempt_32k = now;
    if (!storage_record_save(&g_bms_state_store, payload)) {
        g_bms_state_last_failed = 1u;
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0;
    }
    g_bms_state_last_failed = 0u;
    g_bms_state = *next;
    return 1;
}

/* 加载并验证持久状态，建立当前缓存。 */
int bms_state_store_init(void)
{
    const storage_port_t *port;
    storage_region_t region;
    u8 payload[BMS_STATE_PAYLOAD_BYTES];
    bms_state_persist_t next;
    if (g_bms_state_ready) return 1;
    bms_diag_attempt(BMS_STORAGE_DOMAIN_STATE);
    if (g_bms_state_attempted && g_bms_state_last_failed &&
        (u32)(pm_get_32k_tick() - g_bms_state_last_attempt_32k) < BMS_STORAGE_RETRY_INTERVAL_32K) { bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_BACKOFF); return 0; }
    port = bms_storage_platform_port();
    if (port == 0) { bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_PORT); goto invalid; }
    if (!bms_storage_platform_region(BMS_STORAGE_DOMAIN_STATE, &region)) { goto invalid; }
    if (!g_bms_state_store.ready && !storage_record_open(&g_bms_state_store, port, region, BMS_STATE_RECORD_MAGIC,
                             BMS_STATE_SCHEMA_VERSION, BMS_STATE_PAYLOAD_BYTES)) {
        bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_OPEN); goto invalid;
    }
    if (storage_record_load(&g_bms_state_store, payload)) bms_state_decode(&g_bms_state, payload);
    else if (g_bms_state_store.load_status == STORAGE_RECORD_LOAD_IO_ERROR) {
        bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_INVALID); goto invalid;
    }
    else { bms_state_defaults(&g_bms_state); bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_DEFAULTS); }
    next = g_bms_state;
    if (next.soc_revision != BMS_UPDATE_SOC_STATE_REVISION) {
        bms_state_defaults(&next);
    }
    if (next.soc > 100u || next.dsg > 100u || next.cycle > 65535u) {
        bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_INVALID); goto invalid;
    }
    if (!bms_state_save(&next)) { bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_SAVE); return 0; }
    g_bms_state_pending = g_bms_state;
    g_bms_state_last_attempt_32k = pm_get_32k_tick();
    g_bms_state_attempted = 1u;
    g_bms_state_ready = 1u;
    bms_diag_result(BMS_STORAGE_DOMAIN_STATE, DIAG_OK);
    return 1;
invalid:
    g_bms_state_attempted = 1u;
    g_bms_state_last_failed = 1u;
    g_bms_state_last_attempt_32k = pm_get_32k_tick();
    bms_error_raise(BMS_ERROR_EEPROM_STORE);
    return 0;
}

/* 取得缓存的 SOC 和循环状态。 */
bms_state_store_data_t bms_state_store_get(void)
{
    bms_state_store_data_t data = bms_state_store_get_default_data();
    if (!g_bms_state_ready) return data;
    data.soc = g_bms_state.soc;
    data.dsg = g_bms_state.dsg;
    data.cycle = g_bms_state.cycle;
    return data;
}

/* 保存完整状态并按结果发布缓存。 */
int bms_state_store_write_all(u32 soc, u32 dsg, u32 cycle)
{
    bms_state_persist_t next;
    if (soc > 100u || dsg > 100u || cycle > 65535u || !bms_state_store_init()) return 0;
    next = g_bms_state_pending;
    next.soc = soc; next.dsg = dsg; next.cycle = cycle;
    g_bms_state_pending = next;
    return bms_state_save(&next);
}

/* 仅在状态变化且策略允许时提交检查点。 */
void bms_state_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle)
{
    u32 interval;
    if (soc > 100u || dsg > 100u || cycle > 65535u || !bms_state_store_init()) return;
    g_bms_state_pending.soc = soc;
    g_bms_state_pending.dsg = dsg;
    g_bms_state_pending.cycle = cycle;
    interval = g_bms_state_last_failed ? BMS_STORAGE_RETRY_INTERVAL_32K : BMS_STATE_SAVE_INTERVAL_32K;
    if ((u32)(pm_get_32k_tick() - g_bms_state_last_attempt_32k) < interval) return;
    (void)bms_state_save(&g_bms_state_pending);
}

/* 更新 SOC 与循环次数状态并保存。 */
int bms_state_store_set_soc_cycle(u32 soc, u32 dsg, u32 cycle)
{
    bms_state_persist_t next;
    if (soc > 100u || dsg > 100u || cycle > 65535u || !bms_state_store_init()) return 0;
    next = g_bms_state_pending;
    next.soc = soc; next.dsg = dsg; next.cycle = cycle;
    if (!bms_state_save(&next)) return 0;
    g_bms_state_pending = next;
    return 1;
}
