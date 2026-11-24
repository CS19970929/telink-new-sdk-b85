#include "bms_sleep_status.h"
#include "bms_time.h"

/* PM 是唯一写入者，协议在同一主循环读取；不在 ISR 修改，无 Flash 持久化。 */
static uint16_t s_words[18];

static void put32(uint16_t *words, uint32_t value)
{
    words[0] = (uint16_t)value;
    words[1] = (uint16_t)(value >> 16);
}

void bms_sleep_status_publish(uint8_t reason, uint32_t block_mask, uint32_t elapsed_ms,
    uint32_t delay_ms, uint32_t retry_ms, uint8_t suspend_allowed)
{
    uint32_t remaining_ms = elapsed_ms < delay_ms ? delay_ms - elapsed_ms : 0u;
    s_words[2] = reason == BMS_SLEEP_REASON_NONE ? BMS_SLEEP_STATE_NONE :
        (block_mask ? BMS_SLEEP_STATE_BLOCKED : (retry_ms ? BMS_SLEEP_STATE_RETRY :
        (remaining_ms ? BMS_SLEEP_STATE_COUNTING : BMS_SLEEP_STATE_READY)));
    s_words[3] = reason;
    s_words[4] = suspend_allowed ? 1u : 0u;
    put32(&s_words[6], block_mask);
    put32(&s_words[8], elapsed_ms);
    put32(&s_words[10], delay_ms);
    put32(&s_words[12], remaining_ms);
    put32(&s_words[14], retry_ms);
    put32(&s_words[16], bms_time_now_32k());
}

void bms_sleep_status_committed(void)
{
    s_words[2] = BMS_SLEEP_STATE_COMMITTED;
    put32(&s_words[6], 0u);
    s_words[8] = s_words[10];
    s_words[9] = s_words[11];
    put32(&s_words[12], 0u);
    put32(&s_words[14], 0u);
    put32(&s_words[16], bms_time_now_32k());
}

int bms_sleep_status_overlaps(uint16_t start, uint16_t count)
{
    return count != 0u && start < BMS_SLEEP_STATUS_END &&
        (uint32_t)start + count > BMS_SLEEP_STATUS_BASE;
}

int bms_sleep_status_read(uint16_t start, uint16_t count, uint8_t *bytes)
{
    uint16_t i;
    uint32_t now;
    if (!bytes || count == 0u || start < BMS_SLEEP_STATUS_BASE ||
        (uint32_t)start + count > BMS_SLEEP_STATUS_END) return 0;
    now = bms_time_now_32k();
    for (i = 0u; i < count; ++i) {
        uint16_t offset = (uint16_t)(start - BMS_SLEEP_STATUS_BASE + i);
        uint16_t word;
        if (offset == 0u) word = 0x534Cu;
        else if (offset == 1u) word = 1u;
        else if (offset == 18u) word = (uint16_t)now;
        else if (offset == 19u) word = (uint16_t)(now >> 16);
        else word = s_words[offset];
        bytes[2u * i] = (uint8_t)(word >> 8);
        bytes[2u * i + 1u] = (uint8_t)word;
    }
    return 1;
}
