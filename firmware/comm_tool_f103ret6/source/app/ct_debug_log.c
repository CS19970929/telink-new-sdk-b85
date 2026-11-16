#include "ct_debug_log.h"
#include "ct_board_port.h"
#include <string.h>

typedef struct
{
    uint16_t seq;
    uint32_t tick_ms;
    uint8_t module;
    uint8_t event;
    uint16_t value0;
    uint16_t value1;
} CtDebugLogEntry;

#if CT_DEBUG_LOG_ENABLE
static CtDebugLogEntry s_entries[CT_DEBUG_LOG_CAPACITY];
static uint16_t s_next_seq;
static uint16_t s_dropped;
static uint8_t s_head;
static uint8_t s_count;
#endif

static void wr16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void wr32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

#if CT_DEBUG_LOG_ENABLE
void CtDebugLog_Record(uint8_t module, uint8_t event, uint16_t value0, uint16_t value1)
{
    CtDebugLogEntry *entry;

    entry = &s_entries[s_head];
    entry->seq = s_next_seq++;
    entry->tick_ms = CtBoard_GetTickMs();
    entry->module = module;
    entry->event = event;
    entry->value0 = value0;
    entry->value1 = value1;

    s_head++;
    if (s_head >= CT_DEBUG_LOG_CAPACITY)
    {
        s_head = 0u;
    }

    if (s_count < CT_DEBUG_LOG_CAPACITY)
    {
        s_count++;
    }
    else if (s_dropped < 0xFFFFu)
    {
        s_dropped++;
    }
}
#endif

uint8_t CtDebugLog_IsEnabled(void)
{
    return (uint8_t)CT_DEBUG_LOG_ENABLE;
}

void CtDebugLog_Clear(void)
{
#if CT_DEBUG_LOG_ENABLE
    memset(s_entries, 0, sizeof(s_entries));
    s_next_seq = 0u;
    s_dropped = 0u;
    s_head = 0u;
    s_count = 0u;
#endif
}

uint16_t CtDebugLog_EncodeLatest(uint8_t max_entries,
                                  uint8_t clear_after_read,
                                  uint8_t *out,
                                  uint16_t out_size)
{
    uint8_t count = 0u;
    uint8_t start;
    uint8_t skip = 0u;
    uint8_t i;
    uint8_t index;
    uint8_t *dst;
    uint16_t length = CT_DEBUG_LOG_HEADER_SIZE;

    if ((out == 0) || (out_size < CT_DEBUG_LOG_HEADER_SIZE))
    {
        return 0u;
    }

    out[0] = (uint8_t)CT_DEBUG_LOG_ENABLE;
    out[1] = 0u;
    out[2] = CT_DEBUG_LOG_CAPACITY;
    out[3] = CT_DEBUG_LOG_ENTRY_SIZE;
    out[4] = 0u;
    out[5] = 0u;

#if CT_DEBUG_LOG_ENABLE
    count = s_count;
    if ((max_entries != 0u) && (count > max_entries))
    {
        count = max_entries;
    }
    while ((uint16_t)(CT_DEBUG_LOG_HEADER_SIZE + ((uint16_t)count * CT_DEBUG_LOG_ENTRY_SIZE)) > out_size)
    {
        count--;
    }
    skip = (uint8_t)(s_count - count);
    start = (uint8_t)((s_head + CT_DEBUG_LOG_CAPACITY - s_count) % CT_DEBUG_LOG_CAPACITY);

    out[1] = count;
    wr16(&out[4], s_dropped);

    for (i = 0u; i < count; ++i)
    {
        index = (uint8_t)((start + skip + i) % CT_DEBUG_LOG_CAPACITY);
        dst = &out[CT_DEBUG_LOG_HEADER_SIZE + ((uint16_t)i * CT_DEBUG_LOG_ENTRY_SIZE)];
        wr16(&dst[0], s_entries[index].seq);
        wr32(&dst[2], s_entries[index].tick_ms);
        dst[6] = s_entries[index].module;
        dst[7] = s_entries[index].event;
        wr16(&dst[8], s_entries[index].value0);
        wr16(&dst[10], s_entries[index].value1);
    }

    length = (uint16_t)(CT_DEBUG_LOG_HEADER_SIZE + ((uint16_t)count * CT_DEBUG_LOG_ENTRY_SIZE));
    if (clear_after_read != 0u)
    {
        CtDebugLog_Clear();
    }
#else
    (void)max_entries;
    (void)clear_after_read;
#endif

    return length;
}
