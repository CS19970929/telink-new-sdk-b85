/* 文件功能：运行调试日志 RAM 环形缓存与只读接口；编译关闭时裁剪记录点，不写 Flash、不主动发送。
 * bms_debug_log.c；源码编译归属见 bms/products/<product>/sources.txt，具体实现受产品宏约束。
 */
/* 运行日志 RAM 环形缓存和只读寄存器窗口；不启动 UART、不等待发送、不申请堆。
 * 记录与读取均由主循环执行，不与 ISR 共享；普通 suspend 保留 RAM，deep sleep/reset 丢失。 */
#include "bms_debug_log.h"
#include "bms_diag.h"
#if BMS_DEBUG_LOG_ENABLE
#include <string.h>

static uint16_t s_records[BMS_DEBUG_LOG_CAPACITY][BMS_DEBUG_LOG_RECORD_WORDS];
static uint32_t s_next_sequence; /* 下一条序号；自然回绕，槽位由低 6 位决定。 */
static uint32_t s_overwritten;   /* 缓冲满后覆盖总数，饱和计数；不是某个客户端的丢失数。 */
static uint32_t s_boot_tick;
static uint16_t s_count;

static void put32(uint16_t *words, uint32_t value)
{
    words[0] = (uint16_t)(value >> 16);
    words[1] = (uint16_t)value;
}

void bms_debug_log_init(void)
{
    memset(s_records, 0, sizeof(s_records));
    s_next_sequence = 0u;
    s_overwritten = 0u;
    s_count = 0u;
    s_boot_tick = bms_diag_tick();
}

void bms_debug_log_write(uint8_t level, uint8_t module, uint16_t event,
                         uint32_t arg0, uint32_t arg1)
{
    uint16_t *record;
    if (level < BMS_LOG_ERROR || level > BMS_DEBUG_LOG_LEVEL || module > BMS_LOG_OTA ||
        !(BMS_DEBUG_LOG_MODULE_MASK & (1u << module))) return;
    record = s_records[s_next_sequence % BMS_DEBUG_LOG_CAPACITY];
    put32(record, s_next_sequence);
    put32(record + 2, bms_diag_tick());
    record[4] = (uint16_t)(((uint16_t)level << 8) | module);
    record[5] = event;
    put32(record + 6, arg0);
    put32(record + 8, arg1);
    ++s_next_sequence;
    if (s_count < BMS_DEBUG_LOG_CAPACITY) ++s_count;
    else if (s_overwritten != UINT32_MAX) ++s_overwritten;
}
#endif

int bms_debug_log_overlaps(uint16_t start, uint16_t count)
{
    return count != 0u && start < BMS_DEBUG_LOG_END &&
           (uint32_t)start + count > BMS_DEBUG_LOG_BASE;
}

int bms_debug_log_is_read(const uint8_t *frame, uint32_t length)
{
    uint16_t start;
    if (frame == 0 || length != 8u || frame[1] != 3u) return 0;
    start = (uint16_t)(((uint16_t)frame[2] << 8) | frame[3]);
    return bms_debug_log_overlaps(start,
        (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]));
}

/* 一次请求内无主循环生产者交错；跨请求由上位机校验每条序号，拒绝被覆盖的槽位。
 * 所有 u32 都是高 word 在前，每个 word 按 Modbus 大端发送。关闭时仍可探测能力。 */
int bms_debug_log_read(uint16_t start, uint16_t count, uint8_t *bytes)
{
    uint16_t i;
    uint16_t header[BMS_DEBUG_LOG_HEADER_WORDS] = {0};
    if (bytes == 0 || count == 0u || count > 125u || start < BMS_DEBUG_LOG_BASE ||
        (uint32_t)start + count > BMS_DEBUG_LOG_END) return 0;
    header[0] = 0x4C47u;
    header[1] = 1u;
    header[2] = BMS_DEBUG_LOG_ENABLE;
    header[3] = BMS_DEBUG_LOG_CAPACITY;
    header[4] = BMS_DEBUG_LOG_RECORD_WORDS;
#if BMS_DEBUG_LOG_ENABLE
    header[5] = s_count;
    put32(header + 6, s_next_sequence);
    put32(header + 8, s_overwritten);
    put32(header + 10, s_boot_tick);
    header[14] = BMS_DEBUG_LOG_LEVEL;
    header[15] = BMS_DEBUG_LOG_MODULE_MASK;
#endif
    header[12] = (uint16_t)(BMS_DIAG_BUILD_ID >> 16);
    header[13] = (uint16_t)BMS_DIAG_BUILD_ID;
    for (i = 0u; i < count; ++i) {
        uint16_t offset = (uint16_t)(start - BMS_DEBUG_LOG_BASE + i);
        uint16_t word = 0u;
        if (offset < BMS_DEBUG_LOG_HEADER_WORDS) word = header[offset];
#if BMS_DEBUG_LOG_ENABLE
        else {
            offset = (uint16_t)(offset - BMS_DEBUG_LOG_HEADER_WORDS);
            word = s_records[offset / BMS_DEBUG_LOG_RECORD_WORDS][offset % BMS_DEBUG_LOG_RECORD_WORDS];
        }
#endif
        bytes[2u * i] = (uint8_t)(word >> 8);
        bytes[2u * i + 1u] = (uint8_t)word;
    }
    return 1;
}
