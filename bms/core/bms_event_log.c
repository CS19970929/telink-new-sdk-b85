/*
 * 文件功能：持久历史事件和 Flash checkpoint；与详细运行调试日志独立，
 * OTA 保留已保存的历史事件，不再依据旧编号清空。
 * bms/core/bms_event_log.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_diag.h"
#include "bms_event_log.h"
#include "bms_update_policy.h"

#include "bms_error.h"
#include "bms_storage_platform.h"
#include "storage_record.h"
#include "bms_time.h"
#include "bms_product.h"
#include "bms_afe_backend.h"
#include <string.h>

#define BMS_EVENT_SAVE_INTERVAL_32K (60u * 32000u)

#define BMS_EVENT_RECORD_MAGIC          0x45563400u + BMS_PRODUCT_ID /*
 * 事件标识为 EV4 加产品编号。
 */
#define BMS_EVENT_SCHEMA_VERSION        4u
#define BMS_EVENT_PAYLOAD_BYTES (2u + BMS_EVENT_LOG_ENTRY_COUNT * 2u)

typedef struct {
    u8 records[BMS_EVENT_LOG_ENTRY_COUNT][2];
    u32 last_attempt_32k;
    u8 last_failed;
    u8 dirty;
    u16 write_pos;
    u32 interval_s;
    u8 ready;
    u8 event_latched[EVENT_NUM];
    u32 clock_tick_32k;
    u16 clock_fraction_32k;
    u8 merge_ready;
} bms_event_log_ctx_t;

static bms_event_log_ctx_t g_bms_event_log;
/* SDK 的 -fpack-struct 会压紧 ctx；TC32 整字读取 store->port，不能内嵌到未对齐偏移。 */
static storage_record_store_t g_bms_event_store __attribute__((aligned(4)));

/* 将 16 位值按小端写入存储缓冲区。 */
static void bms_event_log_put_u16le(u8 *buf, u16 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)(value >> 8);
}

/* 从存储缓冲区按小端读取 16 位值。 */
static u16 bms_event_log_get_u16le(const u8 *buf)
{
    return (u16)((u16)buf[0] | ((u16)buf[1] << 8));
}

/* 清除本次运行的历史事件去重标志。 */
static void bms_event_log_clear_runtime_flags(void)
{
    memset(g_bms_event_log.event_latched, 0, sizeof(g_bms_event_log.event_latched));
    g_bms_event_log.clock_tick_32k = bms_time_now_32k();
    g_bms_event_log.clock_fraction_32k = 0u;
    g_bms_event_log.merge_ready = 0u;
    g_bms_event_log.interval_s = 0u;
}

/* 按实际 32K 差值累计；每次调用间隔须短于约 37 小时的 tick 回绕周期。 */
static void bms_event_log_advance_time(void)
{
    u32 now = bms_time_now_32k();
    u32 delta = now - g_bms_event_log.clock_tick_32k;
    u32 seconds = delta / 32000u;
    u32 fraction = delta % 32000u + g_bms_event_log.clock_fraction_32k;
    const u32 limit = 168u * 3600u + 1u;
    g_bms_event_log.clock_tick_32k = now;
    seconds += fraction / 32000u;
    g_bms_event_log.clock_fraction_32k = (u16)(fraction % 32000u);
    if (seconds >= limit - g_bms_event_log.interval_s) g_bms_event_log.interval_s = limit;
    else g_bms_event_log.interval_s += seconds;
}

/* 仅复位 RAM 历史事件状态，不擦除持久记录。 */
static void bms_event_log_reset_ram_only(void)
{
    memset(g_bms_event_log.records, 0, sizeof(g_bms_event_log.records));
    g_bms_event_log.write_pos = 0u;
    bms_event_log_clear_runtime_flags();
}

/* 按固定存储格式编码历史事件快照。 */
static void bms_event_log_encode(u8 payload[BMS_EVENT_PAYLOAD_BYTES])
{
    bms_event_log_put_u16le(&payload[0], g_bms_event_log.write_pos);
    memcpy(&payload[2], g_bms_event_log.records, sizeof(g_bms_event_log.records));
}

/* 验证并解码历史事件持久记录。 */
static int bms_event_log_decode(const u8 payload[BMS_EVENT_PAYLOAD_BYTES])
{
    u16 i;
    u16 write_pos = bms_event_log_get_u16le(&payload[0]);
    /* 旧事件编号不再触发清空；OTA 窗口始终保留历史事件。 */
    if (write_pos >= BMS_EVENT_LOG_ENTRY_COUNT) return 0;
    for (i = 0u; i < BMS_EVENT_LOG_ENTRY_COUNT; ++i) {
        u8 event = payload[2u + 2u * i];
        u8 time = payload[3u + 2u * i];
        if (event >= EVENT_NUM || (event == 0u && time != 0u) ||
            (event != 0u && ((time == 0u && event != BMS_START_UP) ||
             (time > 168u && time != 170u && time != 171u)))) return 0;
    }
    g_bms_event_log.write_pos = write_pos;
    memcpy(g_bms_event_log.records, &payload[2], sizeof(g_bms_event_log.records));
    return 1;
}

/* 将待写历史事件合并为 checkpoint，失败按 32K 时间退避；此路径不用于运行调试日志。 */
static int bms_event_log_write_snapshot(void)
{
    u8 payload[BMS_EVENT_PAYLOAD_BYTES];
    u32 now = bms_time_now_32k();
    if (!g_bms_event_log.ready) return 0;
    if (g_bms_event_log.last_failed &&
        (u32)(now - g_bms_event_log.last_attempt_32k) < BMS_STORAGE_RETRY_INTERVAL_32K) return 0;
    g_bms_event_log.last_attempt_32k = now;
    bms_event_log_encode(payload);
    if (!storage_record_save(&g_bms_event_store, payload)) {
        g_bms_event_log.last_failed = 1u;
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0;
    }
    g_bms_event_log.last_failed = 0u;
    g_bms_event_log.dirty = 0u;
    return 1;
}

/* 把采样间隔编码为历史事件时间字段。 */
static u8 bms_event_log_map_interval(u32 seconds)
{
    if (seconds <= 60u) return 171u;
    if (seconds <= (3600u * 168u)) return (u8)((seconds + 3599u) / 3600u);
    return 170u;
}

/*
 * RAM 待保存环形缓冲在保存失败后保留事件；协议仍为 event ID 和时间码。
 */
static void bms_event_log_append(bms_event_log_id_t event)
{
    u16 pos, previous;
    /* 三个调用入口均已完成 init；这里只更新 RAM，不存在存储失败返回。 */
    pos = g_bms_event_log.write_pos;
    previous = pos ? (u16)(pos - 1u) : (BMS_EVENT_LOG_ENTRY_COUNT - 1u);
    if (g_bms_event_log.merge_ready && event != BMS_START_UP && event != BMS_SLEEP &&
        g_bms_event_log.records[previous][0] == (u8)event &&
        g_bms_event_log.interval_s <= 60u) {
        /* 相同事件在首条后的 60 秒内合并；没有消费者的隐藏计数不再触发擦写。 */
        return;
    }
    g_bms_event_log.records[pos][0] = (u8)event;
    g_bms_event_log.records[pos][1] = event == BMS_START_UP ? 0u :
        bms_event_log_map_interval(g_bms_event_log.interval_s);
    /* 直接访问 packed 字段，不能把未对齐成员地址作为普通 u32 * 传递。 */
    if (event != BMS_START_UP) g_bms_event_log.interval_s = 0u;
    g_bms_event_log.clock_fraction_32k = 0u;
    g_bms_event_log.merge_ready = (event != BMS_START_UP && event != BMS_SLEEP) ? 1u : 0u;
    g_bms_event_log.write_pos = (u16)((pos + 1u) % BMS_EVENT_LOG_ENTRY_COUNT);
    g_bms_event_log.dirty = 1u;
}

/* 检测故障上升沿并追加历史事件。 */
static void bms_event_log_track_edge(u8 active, bms_event_log_id_t event)
{
    if ((u32)event >= EVENT_NUM) return;
    if (active) {
        if (!g_bms_event_log.event_latched[event]) {
            bms_event_log_append(event);
            g_bms_event_log.event_latched[event] = 1u;
        }
    } else {
        g_bms_event_log.event_latched[event] = 0u;
    }
}

/* 加载历史记录并初始化事件跟踪状态。 */
int bms_event_log_init(void)
{
    const storage_port_t *port;
    storage_region_t region;
    u8 payload[BMS_EVENT_PAYLOAD_BYTES];
    int loaded;
    if (g_bms_event_log.ready) return 1;
    bms_diag_attempt(BMS_STORAGE_DOMAIN_EVENT);
    if (g_bms_event_log.last_failed &&
        (u32)(bms_time_now_32k() - g_bms_event_log.last_attempt_32k) < BMS_STORAGE_RETRY_INTERVAL_32K) { bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_BACKOFF); return 0; }
    port = bms_storage_platform_port();
    if (port == 0) { bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_PORT); return 0; }
    if (!bms_storage_platform_region(BMS_STORAGE_DOMAIN_EVENT, &region)) { return 0; }
    if (!g_bms_event_store.ready && !storage_record_open(&g_bms_event_store, port, region, BMS_EVENT_RECORD_MAGIC,
                             BMS_EVENT_SCHEMA_VERSION, BMS_EVENT_PAYLOAD_BYTES)) {
        bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_OPEN); return 0;
    }
    loaded = storage_record_load(&g_bms_event_store, payload) && bms_event_log_decode(payload);
    if (g_bms_event_store.load_status == STORAGE_RECORD_LOAD_IO_ERROR) {
        bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_INVALID); return 0;
    }
    if (!loaded) { bms_event_log_reset_ram_only(); bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_DEFAULTS); }
    g_bms_event_log.ready = 1u;
    if (!loaded) {
        if (!bms_event_log_write_snapshot()) { g_bms_event_log.ready = 0u; bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_SAVE); return 0; }
    }
    g_bms_event_log.last_attempt_32k = bms_time_now_32k();
    bms_event_log_clear_runtime_flags();
    bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_OK);
    return 1;
}

/* 记录本次启动及启动原因。 */
void bms_event_log_note_startup(void)
{
    if (!bms_event_log_init()) return;
    bms_event_log_advance_time();
    g_bms_event_log.interval_s = 0u;
    g_bms_event_log.clock_fraction_32k = 0u;
    bms_event_log_append(BMS_START_UP);
}

/* 记录进入休眠的原因与当前状态。 */
int bms_event_log_note_sleep(void)
{
    if (!bms_event_log_init()) return 0;
    bms_event_log_advance_time();
    /* 保存失败的同一次尝试不重复追加；已提交的保护性休眠不再取消。 */
    if (!g_bms_event_log.event_latched[BMS_SLEEP]) {
        bms_event_log_append(BMS_SLEEP);
        g_bms_event_log.event_latched[BMS_SLEEP] = 1u;
    }
    if (!g_bms_event_log.dirty) return 1;
    return bms_event_log_write_snapshot();
}

/* 按秒检测事件变化并按策略保存检查点。 */
void bms_event_log_poll_1s(const bms_event_log_sample_t *sample)
{
    if (sample == 0) return;
    if (!bms_event_log_init()) return;
    bms_event_log_advance_time();
    bms_event_log_track_edge(sample->vcell_ovp, VCELL_OVP);
    bms_event_log_track_edge(sample->vbus_ovp, VBUS_OVP);
    bms_event_log_track_edge(sample->chg_ocp, CHG_OCP);
    bms_event_log_track_edge(sample->vcell_uvp, VCELL_UVP);
    bms_event_log_track_edge(sample->vbus_uvp, VBUS_UVP);
    bms_event_log_track_edge(sample->dsg_ocp, DSG_OCP);
    bms_event_log_track_edge(sample->chg_utp, CHG_UTP);
    bms_event_log_track_edge(sample->dsg_utp, DSG_UTP);
    bms_event_log_track_edge(sample->chg_otp, CHG_OTP);
    bms_event_log_track_edge(sample->dsg_otp, DSG_OTP);
    bms_event_log_track_edge(sample->vdelta_op, VDELTA_OP);
    bms_event_log_track_edge(sample->afe1_err, AFE1_ERR);
    bms_event_log_track_edge(sample->cbc_err, CBC_ERR);
    if (g_bms_event_log.dirty &&
        (u32)(bms_time_now_32k() - g_bms_event_log.last_attempt_32k) >=
        (g_bms_event_log.last_failed ? BMS_STORAGE_RETRY_INTERVAL_32K : BMS_EVENT_SAVE_INTERVAL_32K))
        (void)bms_event_log_write_snapshot();
}

/* 读取历史事件窗口中的一个协议寄存器。 */
u16 bms_event_log_read_reg(u16 reg)
{
    u16 idx;
    if (reg >= BMS_EVENT_LOG_REG_COUNT) return 0u;
    if (!g_bms_event_log.ready) return 0u;
    idx = (u16)(g_bms_event_log.write_pos + BMS_EVENT_LOG_ENTRY_COUNT - 1u - reg);
    if (idx >= BMS_EVENT_LOG_ENTRY_COUNT) idx = (u16)(idx - BMS_EVENT_LOG_ENTRY_COUNT);
    return (u16)(((u16)g_bms_event_log.records[idx][0] << 8) |
                 g_bms_event_log.records[idx][1]);
}

/* 出厂恢复：提交空历史快照，旧记录不再属于可见事件历史。 */
int bms_event_log_factory_reset(void)
{
    const storage_port_t *port;
    storage_region_t region;
    u8 payload[BMS_EVENT_PAYLOAD_BYTES];
    if (!bms_ota_factory_reset_requested()) return 0;
    if (!g_bms_event_store.ready) {
        port = bms_storage_platform_port();
        if (!port || !bms_storage_platform_region(BMS_STORAGE_DOMAIN_EVENT, &region) ||
            !storage_record_open(&g_bms_event_store, port, region, BMS_EVENT_RECORD_MAGIC,
                                 BMS_EVENT_SCHEMA_VERSION, BMS_EVENT_PAYLOAD_BYTES)) return 0;
    }
    (void)storage_record_load(&g_bms_event_store, payload);
    if (g_bms_event_store.load_status == STORAGE_RECORD_LOAD_IO_ERROR) return 0;
    bms_event_log_reset_ram_only();
    g_bms_event_log.ready = 1u;
    g_bms_event_log.dirty = 1u;
    return bms_event_log_write_snapshot();
}
