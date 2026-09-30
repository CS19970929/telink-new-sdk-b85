#include "bms_afe_hw_access.h"
#include "bms_afe_hw_profile.h"

static u16 s_token;
static u32 s_last_activity_tick;
static u8 s_active;
static u16 s_generation;
#define ACCESS_FRAME_BYTES 79u
#define ACCESS_CHUNK_BYTES 11u
static u8 s_frame[ACCESS_FRAME_BYTES];
static u8 s_received;
static u32 s_fragment_tick;

static u16 access_crc16(const u8 *data, u32 len)
{
    u16 crc = 0xFFFFu;
    u32 i;
    u8 bit;

    for (i = 0u; i < len; ++i)
    {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc & 1u) ? (u16)((crc >> 1) ^ 0xA001u) : (u16)(crc >> 1);
    }
    return crc;
}

static u16 access_u16be(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

static u32 access_u32be(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static void access_put_u16be(u8 *p, u16 value)
{
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}

static u8 access_expired(void)
{
    if (!s_active) return 1u;
    return clock_time_exceed(s_last_activity_tick,
                             (u32)BMS_AFE_HW_ACCESS_TIMEOUT_SECONDS * 1000000u) ? 1u : 0u;
}

void bms_afe_hw_access_close(void)
{
    s_received = 0u;
    s_active = 0u;
    s_token = 0u;
    s_last_activity_tick = 0u;
}

void bms_afe_hw_access_poll(void)
{
    if (s_received && clock_time_exceed(s_fragment_tick, 5000000u)) s_received = 0u;
    if (s_active && access_expired()) bms_afe_hw_access_close();
}

u8 bms_afe_hw_access_is_active(void)
{
    bms_afe_hw_access_poll();
    return s_active ? 1u : 0u;
}

u16 bms_afe_hw_access_remaining_seconds(void)
{
    u32 elapsed_ticks;
    u32 elapsed_seconds;

    if (!bms_afe_hw_access_is_active()) return 0u;
    elapsed_ticks = (u32)(clock_time() - s_last_activity_tick);
    elapsed_seconds = (CLOCK_SYS_CLOCK_HZ == 0u) ? 0u : elapsed_ticks / CLOCK_SYS_CLOCK_HZ;
    if (elapsed_seconds >= BMS_AFE_HW_ACCESS_TIMEOUT_SECONDS) return 0u;
    return (u16)(BMS_AFE_HW_ACCESS_TIMEOUT_SECONDS - elapsed_seconds);
}

static u16 access_new_token(void)
{
    ++s_generation;
    if (!s_generation) ++s_generation;
    return s_generation;
}

static u8 access_session_valid(u16 token)
{
    if (!bms_afe_hw_access_is_active() || token == 0u || token != s_token) return 0u;
    s_last_activity_tick = clock_time();
    return 1u;
}

static int access_response(u8 addr,
                           u8 command,
                           u8 status,
                           const u8 *payload,
                           u8 payload_len,
                           u8 *rsp,
                           u32 *rsp_len)
{
    u32 length = 0u;
    u16 crc;

    rsp[length++] = addr;
    rsp[length++] = BMS_AFE_HW_ACCESS_MODBUS_FUNC;
    rsp[length++] = command;
    rsp[length++] = status;
    if (payload != 0 && payload_len != 0u)
    {
        u8 i;
        for (i = 0u; i < payload_len; ++i) rsp[length++] = payload[i];
    }
    crc = access_crc16(rsp, length);
    rsp[length++] = (u8)crc;
    rsp[length++] = (u8)(crc >> 8);
    *rsp_len = length;
    return 1;
}

int bms_afe_hw_access_modbus_on_frame(const u8 *req,
                                      u32 req_len,
                                      u8 *rsp,
                                      u32 *rsp_len)
{
    u16 crc_rx;
    u16 crc_calc;
    u8 command;
    u8 payload[7];
    u16 token;

    if (req == 0 || rsp == 0 || rsp_len == 0) return 0;
    *rsp_len = 0u;
    if (req_len < 5u || req[1] != BMS_AFE_HW_ACCESS_MODBUS_FUNC) return 0;

    crc_rx = (u16)(((u16)req[req_len - 1u] << 8) | req[req_len - 2u]);
    crc_calc = access_crc16(req, req_len - 2u);
    if (crc_rx != crc_calc) { s_received = 0u; return 0; }

    command = req[2];
    if (command == BMS_AFE_HW_ACCESS_CMD_OPEN)
    {
        if (req_len != 9u || access_u32be(&req[3]) != BMS_AFE_HW_ACCESS_UNLOCK_MAGIC)
            return access_response(req[0], command, BMS_AFE_HW_ACCESS_STATUS_AUTH_REQUIRED,
                                   0, 0u, rsp, rsp_len);

        s_received = 0u;
        s_token = access_new_token();
        s_last_activity_tick = clock_time();
        s_active = 1u;
        access_put_u16be(&payload[0], s_token);
        access_put_u16be(&payload[2], BMS_AFE_HW_ACCESS_TIMEOUT_SECONDS);
        payload[4] = BMS_AFE_HW_ACCESS_PROTOCOL_VERSION;
        access_put_u16be(&payload[5], bms_afe_hw_profile_expected_model());
        return access_response(req[0], command, BMS_AFE_HW_ACCESS_STATUS_OK,
                               payload, 7u, rsp, rsp_len);
    }

    if (req_len < 7u || (command != BMS_AFE_HW_ACCESS_CMD_STAGE && req_len != 7u)) {
        s_received = 0u;
        return access_response(req[0], command, BMS_AFE_HW_ACCESS_STATUS_BAD_REQUEST, 0, 0u, rsp, rsp_len);
    }
    token = access_u16be(&req[3]);
    if (!access_session_valid(token)) {
        s_received = 0u;
        return access_response(req[0], command, BMS_AFE_HW_ACCESS_STATUS_AUTH_REQUIRED, 0, 0u, rsp, rsp_len);
    }
    if (command == BMS_AFE_HW_ACCESS_CMD_STAGE) {
        u8 count;
        u8 i;
        if (req_len < 10u) goto bad_fragment;
        count = req[6];
        if (!count || count > ACCESS_CHUNK_BYTES || req_len != (u32)(9u + count) ||
            req[5] != s_received || (u16)s_received + count > ACCESS_FRAME_BYTES) goto bad_fragment;
        for (i = 0u; i < count; ++i) s_frame[s_received + i] = req[7u + i];
        s_received = (u8)(s_received + count);
        s_fragment_tick = clock_time();
        access_put_u16be(payload, s_token); payload[2] = s_received;
        return access_response(req[0], command, BMS_AFE_HW_ACCESS_STATUS_OK, payload, 3u, rsp, rsp_len);
    }
    if (command == BMS_AFE_HW_ACCESS_CMD_COMMIT) {
        u8 error;
        if (s_received != ACCESS_FRAME_BYTES || s_frame[0] != req[0]) goto bad_fragment;
        s_received = 0u; /* Consume once, even if persistence/apply fails. */
        error = bms_afe_hw_write_complete_frame(s_frame, ACCESS_FRAME_BYTES);
        return access_response(req[0], command, error ? BMS_AFE_HW_ACCESS_STATUS_APPLY_FAILED :
                               BMS_AFE_HW_ACCESS_STATUS_OK, 0, 0u, rsp, rsp_len);
    }
    switch (command)
    {
    case BMS_AFE_HW_ACCESS_CMD_HEARTBEAT:
    case BMS_AFE_HW_ACCESS_CMD_STATUS:
        access_put_u16be(&payload[0], s_token);
        access_put_u16be(&payload[2], bms_afe_hw_access_remaining_seconds());
        return access_response(req[0], command, BMS_AFE_HW_ACCESS_STATUS_OK,
                               payload, 4u, rsp, rsp_len);

    case BMS_AFE_HW_ACCESS_CMD_CLOSE:
        bms_afe_hw_access_close();
        return access_response(req[0], command, BMS_AFE_HW_ACCESS_STATUS_OK,
                               0, 0u, rsp, rsp_len);

    default:
        return access_response(req[0], command, BMS_AFE_HW_ACCESS_STATUS_UNSUPPORTED,
                               0, 0u, rsp, rsp_len);
    }
bad_fragment:
    s_received = 0u;
    return access_response(req[0], command, BMS_AFE_HW_ACCESS_STATUS_BAD_REQUEST, 0, 0u, rsp, rsp_len);

}
