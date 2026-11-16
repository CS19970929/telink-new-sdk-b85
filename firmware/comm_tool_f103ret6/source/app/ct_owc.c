#include "ct_owc.h"
#include <string.h>

enum { OWC_WAIT_SYNC, OWC_SYNC_HIGH, OWC_DATA, OWC_STOP_HIGH };
typedef struct
{
    uint8_t raw[CT_OWC_MAX_BYTES];
    uint16_t length;
    uint8_t checksum_ok;
    uint32_t tick_ms;
    uint32_t sequence;
} OwcPacket;

/* 0 保存最近完整帧（包括坏校验），1..4 只保存对应 ID 的有效帧。 */
static OwcPacket s_packets[5];
static uint8_t s_raw[CT_OWC_MAX_BYTES];
static uint16_t s_length, s_expected;
static uint8_t s_state, s_bits, s_byte, s_have_edge, s_level;
static uint32_t s_low_us;
static uint32_t s_last_edge_ms;
static uint32_t s_good, s_checksum_errors, s_timing_errors, s_overflows, s_sequence;
static uint32_t s_last_good_ms;
static uint16_t s_session;
static uint8_t s_last_good_id, s_have_good;

#define OWC_SESSION_IDLE_MS 5000u

static uint8_t packet_slot(uint8_t id)
{
    switch (id) {
    case 0x01u: return 1u;
    case 0x3au: return 2u;
    case 0x3bu: return 3u;
    case 0x3cu: return 4u;
    default: return 0u;
    }
}

static void put32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

void CtOwc_ResetDecoder(void)
{
    s_state = OWC_WAIT_SYNC;
    s_have_edge = 0u;
    s_length = 0u;
    s_expected = 0u;
    s_bits = 0u;
    s_byte = 0u;
    s_low_us = 0u;
}

void CtOwc_Init(void)
{
    memset(s_packets, 0, sizeof(s_packets));
    s_good = s_checksum_errors = s_timing_errors = s_overflows = s_sequence = 0u;
    s_last_good_ms = 0u;
    s_session = 0u;
    s_last_good_id = s_have_good = 0u;
    CtOwc_ResetDecoder();
}

void CtOwc_RecordOverflow(void)
{
    ++s_overflows;
    CtOwc_ResetDecoder();
}

static void reject_timing(void)
{
    ++s_timing_errors;
    s_state = OWC_WAIT_SYNC;
}

static void finish_packet(uint32_t now_ms)
{
    uint16_t i;
    uint8_t sum = 0u;
    uint8_t slot;
    OwcPacket *packet = &s_packets[0];
    if ((s_bits != 0u) || (s_expected == 0u) || (s_length != s_expected)) {
        reject_timing();
        return;
    }
    for (i = 0u; i < s_length - 1u; ++i) sum = (uint8_t)(sum + s_raw[i]);
    if (sum == s_raw[s_length - 1u]) {
        /* 公有帧重新出现或失联后恢复都开始新会话，不能混用另一块电池的缓存。
         * 同一接入的连续公有帧属于同一会话；坏校验不能切换身份。 */
        if (!s_have_good || (uint32_t)(now_ms - s_last_good_ms) > OWC_SESSION_IDLE_MS ||
            ((s_raw[0] == 1u) && (s_last_good_id != 1u))) {
            memset(s_packets, 0, sizeof(s_packets));
            if (++s_session == 0u) ++s_session;
        }
        s_have_good = 1u;
        s_last_good_ms = now_ms;
        s_last_good_id = s_raw[0];
    }
    memcpy(packet->raw, s_raw, s_length);
    packet->length = s_length;
    packet->checksum_ok = (sum == s_raw[s_length - 1u]) ? 1u : 0u;
    packet->tick_ms = now_ms;
    packet->sequence = ++s_sequence;
    if (packet->checksum_ok) {
        ++s_good;
        slot = packet_slot(s_raw[0]);
        s_packets[slot] = *packet;
        /* 实时电压已经失效，作废旧单体；采样恢复后也必须等新的单体帧。
         * 这里只消费协议无效值，不访问硬件或改变接入会话。 */
        if ((s_raw[0] == 0x3au) && (s_raw[4] == 0xffu) && (s_raw[5] == 0xffu))
            memset(&s_packets[3], 0, sizeof(s_packets[3]));
    } else {
        ++s_checksum_errors;
    }
    s_state = OWC_WAIT_SYNC;
}

static void append_bit(uint8_t bit)
{
    s_byte |= (uint8_t)(bit << s_bits);
    if (++s_bits != 8u) return;
    s_bits = 0u;
    if ((s_length >= CT_OWC_MAX_BYTES) ||
        ((s_expected != 0u) && (s_length >= s_expected))) {
        reject_timing();
        return;
    }
    s_raw[s_length++] = s_byte;
    s_byte = 0u;
    if ((s_length == 1u) && (packet_slot(s_raw[0]) == 0u)) {
        reject_timing();
    } else if (s_length == 2u) {
        if (((s_raw[1] & 0x0fu) == 0x0fu) || ((s_raw[1] >> 4) == 0x0fu)) {
            reject_timing();
        } else if (s_raw[0] == 1u) {
            s_expected = 20u;
        }
    } else if ((s_length == 3u) && (s_raw[0] != 1u)) {
        s_expected = (uint16_t)(s_raw[2] + 4u);
        /* 0x3A 至少包含 16 bit 加密结果；0x3B 必须是完整的电压对。 */
        if (((s_raw[0] == 0x3au) && ((s_raw[2] < 27u) || (s_raw[2] > 29u))) ||
            ((s_raw[0] == 0x3bu) && ((s_raw[2] == 0u) || (s_raw[2] & 1u))) ||
            ((s_raw[0] == 0x3cu) && (s_raw[2] == 0u))) reject_timing();
    }
}

void CtOwc_Poll(uint32_t now_ms)
{
    uint32_t elapsed = (uint32_t)(now_ms - s_last_edge_ms);
    /* 毫秒时间戳向下取整，多留 1ms，确保真实高电平不少于 50ms。 */
    if ((s_state == OWC_STOP_HIGH) && (elapsed >= 51u)) {
        finish_packet(s_last_edge_ms);
    } else if (((s_state == OWC_DATA) || (s_state == OWC_SYNC_HIGH)) && (elapsed >= 25u)) {
        reject_timing();
    }
}

void CtOwc_FeedEdge(uint32_t duration_us, uint8_t level, uint32_t now_ms)
{
    uint32_t period;
    CtOwc_Poll(now_ms);
    s_last_edge_ms = now_ms;
    if (!s_have_edge) {
        s_have_edge = 1u;
        s_level = level;
        return;
    }
    if (level == s_level) {
        reject_timing();
        return;
    }
    s_level = level;
    if (s_state == OWC_STOP_HIGH) {
        reject_timing(); /* 结束高电平尚未达到 50ms 就发生边沿。 */
    }
    /* 每个合法同步低脉冲都能重新定界，包括上一帧损坏后的恢复。 */
    if ((level != 0u) && (duration_us >= 9000u)) {
        s_state = OWC_SYNC_HIGH;
        s_length = s_expected = 0u;
        s_bits = s_byte = 0u;
        s_low_us = 0u;
        return;
    }
    if (s_state == OWC_WAIT_SYNC) return;
    if (s_state == OWC_SYNC_HIGH) {
        if ((level == 0u) && (duration_us >= 750u) && (duration_us <= 1250u)) {
            s_state = OWC_DATA;
        } else reject_timing();
        return;
    }
    if (level != 0u) {
        if ((duration_us >= 4500u) && (duration_us <= 6500u)) {
            s_state = OWC_STOP_HIGH;
        } else if (((duration_us >= 300u) && (duration_us <= 750u)) ||
                   ((duration_us >= 1250u) && (duration_us <= 1750u))) {
            s_low_us = duration_us;
        } else reject_timing();
    } else {
        period = s_low_us + duration_us;
        if ((s_low_us == 0u) || (period < 1700u) || (period > 2300u) ||
            ((s_low_us <= 750u) && ((duration_us < 1250u) || (duration_us > 1750u))) ||
            ((s_low_us >= 1250u) && ((duration_us < 300u) || (duration_us > 750u)))) {
            reject_timing();
        } else append_bit((s_low_us <= 750u) ? 1u : 0u);
        s_low_us = 0u;
    }
}

uint16_t CtOwc_EncodeSnapshot(uint8_t id, uint8_t enabled, uint32_t now_ms,
                             uint8_t *out, uint16_t capacity)
{
    uint8_t slot = packet_slot(id);
    const OwcPacket *packet = &s_packets[slot];
    uint16_t total = (uint16_t)(CT_OWC_HEADER_BYTES + packet->length);
    if ((id != 0u && slot == 0u) || (out == 0) || (capacity < total)) return 0u;
    memset(out, 0, CT_OWC_HEADER_BYTES);
    out[0] = CT_OWC_SNAPSHOT_VERSION;
    out[1] = enabled;
    out[2] = packet->length ? 1u : 0u;
    out[3] = packet->checksum_ok;
    put32(out + 4, packet->length ? (uint32_t)(now_ms - packet->tick_ms) : 0xffffffffu);
    put32(out + 8, s_good);
    put32(out + 12, s_checksum_errors);
    put32(out + 16, s_timing_errors);
    put32(out + 20, s_overflows);
    put32(out + 24, packet->sequence);
    out[28] = (uint8_t)packet->length;
    out[29] = (uint8_t)(packet->length >> 8);
    out[30] = (uint8_t)s_session;
    out[31] = (uint8_t)(s_session >> 8);
    memcpy(out + CT_OWC_HEADER_BYTES, packet->raw, packet->length);
    return total;
}
