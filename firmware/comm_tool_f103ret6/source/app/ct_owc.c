#include "ct_owc.h"
#include <string.h>

enum { OWC_WAIT_SYNC, OWC_SYNC_HIGH, OWC_DATA, OWC_STOP_HIGH };
typedef struct {
    uint8_t raw[CT_OWC_MAX_BYTES];
    uint16_t length;
    uint8_t checksum_ok, protocol;
    uint32_t tick_ms, sequence;
} OwcPacket;

/* 0是最近完整帧，1..4是A1.3.3有效缓存，5是锂神有效缓存。 */
static OwcPacket s_packets[6];
static uint8_t s_raw[CT_OWC_MAX_BYTES];
static uint16_t s_length, s_expected;
static uint8_t s_state, s_bits, s_byte, s_have_edge, s_level;
static uint8_t s_mode, s_protocol, s_frame_protocol, s_mismatch;
static uint8_t s_candidate_protocol, s_candidate_count;
static uint32_t s_low_us, s_last_edge_ms, s_candidate_ms;
static uint32_t s_good, s_checksum_errors, s_timing_errors, s_overflows, s_sequence;
static uint32_t s_last_good_ms;
static uint16_t s_session;
static uint8_t s_last_good_id, s_have_good;
#define OWC_SESSION_IDLE_MS 5000u
#define OWC_AUTO_CONFIRM_FRAMES 2u
#define OWC_LISHEN_BYTES 6u

static uint8_t packet_slot(uint8_t id)
{
    switch (id) {
    case 1u: return 1u;
    case 0x3au: return 2u;
    case 0x3bu: return 3u;
    case 0x3cu: return 4u;
    default: return 0u;
    }
}

static void put32(uint8_t *out, uint32_t value)
{
    out[0]=(uint8_t)value; out[1]=(uint8_t)(value>>8);
    out[2]=(uint8_t)(value>>16); out[3]=(uint8_t)(value>>24);
}

void CtOwc_ResetDecoder(void)
{
    s_state=OWC_WAIT_SYNC; s_have_edge=0u;
    s_length=s_expected=0u; s_bits=s_byte=0u; s_low_us=0u;
    s_frame_protocol=CT_OWC_PROTOCOL_UNKNOWN;
}

void CtOwc_Init(void)
{
    memset(s_packets,0,sizeof(s_packets));
    s_good=s_checksum_errors=s_timing_errors=s_overflows=s_sequence=0u;
    s_last_good_ms=s_candidate_ms=0u; s_session=0u;
    s_last_good_id=s_have_good=0u;
    s_mode=CT_OWC_MODE_A133; s_protocol=s_mismatch=0u;
    s_candidate_protocol=s_candidate_count=0u;
    CtOwc_ResetDecoder();
}

uint8_t CtOwc_SetMode(uint8_t mode)
{
    if (mode>CT_OWC_MODE_LISHEN) return 0u;
    CtOwc_Init();
    s_mode=mode;
    return 1u;
}
uint8_t CtOwc_GetMode(void) { return s_mode; }

void CtOwc_RecordOverflow(void)
{
    ++s_overflows;
    s_candidate_count=0u;
    CtOwc_ResetDecoder();
}

static void reject_timing(void)
{
    ++s_timing_errors;
    s_candidate_count=0u;
    s_state=OWC_WAIT_SYNC;
}

static void publish_packet(uint8_t valid, uint32_t now_ms)
{
    OwcPacket *packet=&s_packets[0];
    memcpy(packet->raw,s_raw,s_length);
    packet->length=s_length; packet->checksum_ok=valid;
    packet->protocol=s_frame_protocol; packet->tick_ms=now_ms;
    packet->sequence=++s_sequence;
    if (valid) {
        uint8_t slot=s_frame_protocol==CT_OWC_MODE_LISHEN ? 5u : packet_slot(s_raw[0]);
        s_packets[slot]=*packet;
        if (s_frame_protocol==CT_OWC_MODE_A133 && s_raw[0]==0x3au &&
            s_raw[4]==0xffu && s_raw[5]==0xffu)
            memset(&s_packets[3],0,sizeof(s_packets[3]));
    }
}

static void finish_packet(uint32_t now_ms)
{
    uint16_t i;
    uint8_t sum=0u;
    s_state=OWC_WAIT_SYNC;
    if (s_bits!=0u || s_expected==0u || s_length!=s_expected) {
        reject_timing(); return;
    }
    for (i=0u;i<s_length-1u;++i) sum=(uint8_t)(sum+s_raw[i]);
    if (sum!=s_raw[s_length-1u]) {
        ++s_checksum_errors; s_candidate_count=0u;
        if (s_frame_protocol==s_mode || s_frame_protocol==s_protocol)
            publish_packet(0u,now_ms);
        return;
    }
    /* 固定模式也能诊断另一种合法波形，但不能把它发布为当前测量。 */
    if ((s_mode!=CT_OWC_MODE_AUTO && s_mode!=s_frame_protocol) ||
        (s_mode==CT_OWC_MODE_AUTO && s_protocol!=0u && s_protocol!=s_frame_protocol)) {
        s_mismatch=s_frame_protocol; return;
    }
    ++s_good;
    if (s_mode==CT_OWC_MODE_AUTO && s_protocol==0u) {
        if (s_candidate_protocol!=s_frame_protocol ||
            (uint32_t)(now_ms-s_candidate_ms)>OWC_SESSION_IDLE_MS)
            s_candidate_count=0u;
        s_candidate_protocol=s_frame_protocol; s_candidate_ms=now_ms;
        if (++s_candidate_count<OWC_AUTO_CONFIRM_FRAMES) return;
    }
    if (!s_have_good || s_protocol!=s_frame_protocol ||
        (uint32_t)(now_ms-s_last_good_ms)>OWC_SESSION_IDLE_MS ||
        (s_frame_protocol==CT_OWC_MODE_A133 && s_raw[0]==1u && s_last_good_id!=1u)) {
        memset(s_packets,0,sizeof(s_packets));
        if (++s_session==0u) ++s_session;
    }
    s_protocol=s_frame_protocol; s_mismatch=0u;
    s_have_good=1u; s_last_good_ms=now_ms; s_last_good_id=s_raw[0];
    publish_packet(1u,now_ms);
}

static void append_bit(uint8_t bit)
{
    if (s_frame_protocol==CT_OWC_MODE_LISHEN) s_byte=(uint8_t)((s_byte<<1)|bit);
    else s_byte|=(uint8_t)(bit<<s_bits);
    if (++s_bits!=8u) return;
    s_bits=0u;
    if (s_length>=CT_OWC_MAX_BYTES || (s_expected!=0u && s_length>=s_expected)) {
        reject_timing(); return;
    }
    s_raw[s_length++]=s_byte; s_byte=0u;
    if (s_frame_protocol==CT_OWC_MODE_LISHEN) {
        if ((s_length==2u && s_raw[1]>100u) ||
            (s_length==5u && !((s_raw[0]==0u && s_raw[4]==0u) ||
              ((s_raw[4]&0xc0u)==0xc0u && (s_raw[4]&0x3fu)<=0x29u)))) reject_timing();
        return;
    }
    if (s_length==1u && packet_slot(s_raw[0])==0u) reject_timing();
    else if (s_length==2u) {
        if ((s_raw[1]&15u)==15u || (s_raw[1]>>4)==15u) reject_timing();
        else if (s_raw[0]==1u) s_expected=20u;
    } else if (s_length==3u && s_raw[0]!=1u) {
        s_expected=(uint16_t)(s_raw[2]+4u);
        if ((s_raw[0]==0x3au && (s_raw[2]<27u || s_raw[2]>29u)) ||
            (s_raw[0]==0x3bu && (s_raw[2]==0u || (s_raw[2]&1u))) ||
            (s_raw[0]==0x3cu && s_raw[2]==0u)) reject_timing();
    }
}

void CtOwc_Poll(uint32_t now_ms)
{
    uint32_t elapsed=(uint32_t)(now_ms-s_last_edge_ms);
    /* 自动失联后解除锁定，必须重新确认两帧；不复用上一个协议的缓存。 */
    if (s_mode==CT_OWC_MODE_AUTO && s_protocol!=0u && s_have_good &&
        (uint32_t)(now_ms-s_last_good_ms)>OWC_SESSION_IDLE_MS) {
        memset(s_packets,0,sizeof(s_packets));
        s_protocol=s_mismatch=s_have_good=s_candidate_count=0u;
        /* 捕获仍连续，保留上一边沿的电平证据，避免丢掉恢复后的首个同步。 */
        s_state=OWC_WAIT_SYNC; s_length=s_expected=0u;
        s_bits=s_byte=0u; s_low_us=0u; s_frame_protocol=0u;
        return;
    }
    if (s_state==OWC_STOP_HIGH &&
        elapsed>=(s_frame_protocol==CT_OWC_MODE_LISHEN ? 3u : 51u))
        finish_packet(s_last_edge_ms);
    else if ((s_state==OWC_DATA || s_state==OWC_SYNC_HIGH) &&
             elapsed>=(s_frame_protocol==CT_OWC_MODE_LISHEN ? 35u : 25u))
        reject_timing();
}

void CtOwc_FeedEdge(uint32_t duration_us,uint8_t level,uint32_t now_ms)
{
    uint32_t period;
    uint32_t previous_edge_ms=s_last_edge_ms;
    CtOwc_Poll(now_ms);
    s_last_edge_ms=now_ms;
    if (!s_have_edge) { s_have_edge=1u; s_level=level; return; }
    if (level==s_level) { reject_timing(); return; }
    s_level=level;
    if (s_state==OWC_STOP_HIGH) {
        /* 锂神高电平仅2ms：边沿本身可证明最小宽度，不等待下一个主循环tick。 */
        uint32_t minimum=s_frame_protocol==CT_OWC_MODE_LISHEN ? 1500u : 50000u;
        if (level==0u && duration_us>=minimum) finish_packet(previous_edge_ms);
        else reject_timing();
    }
    /* 25ms结束低脉冲也长于A1.3.3同步门限，完整锂神帧必须先识别结束。 */
    if (level!=0u && s_state==OWC_DATA && s_frame_protocol==CT_OWC_MODE_LISHEN &&
        s_length==OWC_LISHEN_BYTES && s_bits==0u && duration_us>=22000u && duration_us<=28000u) {
        s_state=OWC_STOP_HIGH; return;
    }
    /* 同步低电平可能与帧间低电平相连；由后继高电平区分两种协议。 */
    if (level!=0u && duration_us>=9000u) {
        s_state=OWC_SYNC_HIGH; s_length=s_bits=s_byte=0u; s_expected=0u;
        s_low_us=duration_us; s_frame_protocol=0u; return;
    }
    if (s_state==OWC_WAIT_SYNC) return;
    if (s_state==OWC_SYNC_HIGH) {
        if (level==0u && duration_us>=750u && duration_us<=1250u)
            s_frame_protocol=CT_OWC_MODE_A133;
        else if (level==0u && s_low_us>=54000u && duration_us>=1500u && duration_us<=2600u) {
            s_frame_protocol=CT_OWC_MODE_LISHEN; s_expected=OWC_LISHEN_BYTES;
        } else { reject_timing(); return; }
        s_low_us=0u; s_state=OWC_DATA; return;
    }
    if (level!=0u) {
        if (s_frame_protocol==CT_OWC_MODE_LISHEN) {
            if (duration_us>=22000u && duration_us<=28000u) s_state=OWC_STOP_HIGH;
            else if ((duration_us>=1500u && duration_us<=2500u) ||
                     (duration_us>=3500u && duration_us<=4500u)) s_low_us=duration_us;
            else reject_timing();
        } else {
            if (duration_us>=4500u && duration_us<=6500u) s_state=OWC_STOP_HIGH;
            else if ((duration_us>=300u && duration_us<=750u) ||
                     (duration_us>=1250u && duration_us<=1750u)) s_low_us=duration_us;
            else reject_timing();
        }
    } else {
        period=s_low_us+duration_us;
        if (s_frame_protocol==CT_OWC_MODE_LISHEN) {
            if (s_low_us==0u || period<5200u || period>6800u ||
                (s_low_us<=2500u && (duration_us<3500u || duration_us>4500u)) ||
                (s_low_us>=3500u && (duration_us<1500u || duration_us>2500u))) reject_timing();
            else append_bit(s_low_us<=2500u ? 1u : 0u);
        } else {
            if (s_low_us==0u || period<1700u || period>2300u ||
                (s_low_us<=750u && (duration_us<1250u || duration_us>1750u)) ||
                (s_low_us>=1250u && (duration_us<300u || duration_us>750u))) reject_timing();
            else append_bit(s_low_us<=750u ? 1u : 0u);
        }
        s_low_us=0u;
    }
}

static uint16_t encode_snapshot(uint8_t selector,uint8_t enabled,uint32_t now_ms,
                               uint8_t *out,uint16_t capacity,uint8_t version)
{
    uint8_t slot=selector==CT_OWC_LISHEN_SELECTOR ? 5u : packet_slot(selector);
    const OwcPacket *packet=&s_packets[slot];
    uint16_t header=version==3u ? CT_OWC_V3_HEADER_BYTES : CT_OWC_HEADER_BYTES;
    uint16_t total=(uint16_t)(header+packet->length);
    if ((selector!=0u && slot==0u) || out==0 || capacity<total) return 0u;
    memset(out,0,header);
    out[0]=version; out[1]=enabled; out[2]=packet->length ? 1u : 0u;
    out[3]=packet->checksum_ok;
    put32(out+4,packet->length ? (uint32_t)(now_ms-packet->tick_ms) : 0xffffffffu);
    put32(out+8,s_good); put32(out+12,s_checksum_errors); put32(out+16,s_timing_errors);
    put32(out+20,s_overflows); put32(out+24,packet->sequence);
    out[28]=(uint8_t)packet->length; out[29]=(uint8_t)(packet->length>>8);
    out[30]=(uint8_t)s_session; out[31]=(uint8_t)(s_session>>8);
    if (version==3u) {
        out[32]=s_mode; out[33]=s_protocol;
        out[34]=packet->length ? packet->protocol : 0u; out[35]=s_mismatch;
    }
    memcpy(out+header,packet->raw,packet->length);
    return total;
}

uint16_t CtOwc_EncodeSnapshot(uint8_t id,uint8_t enabled,uint32_t now_ms,uint8_t *out,uint16_t capacity)
{
    if (s_mode!=CT_OWC_MODE_A133 || id==CT_OWC_LISHEN_SELECTOR) return 0u;
    return encode_snapshot(id,enabled,now_ms,out,capacity,CT_OWC_SNAPSHOT_VERSION);
}
uint16_t CtOwc_EncodeSnapshotV3(uint8_t selector,uint8_t enabled,uint32_t now_ms,uint8_t *out,uint16_t capacity)
{
    CtOwc_Poll(now_ms);
    return encode_snapshot(selector,enabled,now_ms,out,capacity,3u);
}
