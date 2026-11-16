#include "ct_can_gateway.h"
#include "ct_config.h"
#include "ct_crc16.h"
#include <string.h>

#define APP_CMD_RETRY_INTERVAL_MS       100u
#define APP_CMD_WAIT_SLICE_MS           20u
#define APP_GET_STATUS_TIMEOUT_MS       1000u
#define APP_ENTER_IAP_TIMEOUT_MS        5000u
#define APP_REG_CMD_TIMEOUT_MS          1000u
#define APP_AGING_CMD_TIMEOUT_MS        2000u
#define APP_AGING_BROADCAST_SLICE_MS    20u
#define APP_BLOCK_DATA_TIMEOUT_MS       6000u
#define APP_SINGLE_READ_FALLBACK_MAX    4u
#define IAP_ACK_WAIT_SLICE_MS           20u

enum
{
    ACK_MATCH_NONE = 0,
    ACK_MATCH_OK = 1,
    ACK_MATCH_BAD = 2
};

static uint8_t s_last_gateway_error;

uint8_t CtCan_GetLastGatewayError(void)
{
    return s_last_gateway_error;
}

static uint16_t rd_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static void wr_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void wr_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t app_req_id(uint8_t can_addr)
{
    return (((uint32_t)can_addr & 0x0Fu) << 7) | CT_CAN_APP_REQ_BASE;
}

static uint32_t app_ack_id(uint8_t can_addr)
{
    return (((uint32_t)can_addr & 0x0Fu) << 7) | CT_CAN_APP_ACK_BASE;
}

static int send_app_cmd(uint8_t can_addr, uint8_t cmd, uint8_t a0, uint8_t a1, uint8_t a2)
{
    CtCanFrame frame;
    uint16_t crc;

    memset(&frame, 0, sizeof(frame));
    frame.id = app_req_id(can_addr);
    frame.ide = 0u;
    frame.dlc = 8u;
    frame.data[0] = 0xA5u;
    frame.data[1] = 0x5Au;
    frame.data[2] = cmd;
    frame.data[3] = a0;
    frame.data[4] = a1;
    frame.data[5] = a2;
    crc = CtCrc16_Calc(frame.data, 6u);
    frame.data[6] = (uint8_t)(crc >> 8);
    frame.data[7] = (uint8_t)crc;
    return CtBoard_CanSend(&frame, 200u);
}

static int timeout_expired(uint32_t start, uint32_t timeout_ms)
{
    return ((uint32_t)(CtBoard_GetTickMs() - start) >= timeout_ms) ? 1 : 0;
}

static uint32_t timeout_left(uint32_t start, uint32_t timeout_ms)
{
    uint32_t elapsed;

    elapsed = (uint32_t)(CtBoard_GetTickMs() - start);
    if (elapsed >= timeout_ms)
    {
        return 0u;
    }
    return timeout_ms - elapsed;
}

static int decode_app_ack(const CtCanFrame *frame, uint8_t can_addr, uint8_t cmd, uint8_t *v0, uint8_t *v1)
{
    uint16_t expect_crc;
    uint16_t actual_crc;

    if ((frame->ide != 0u) || (frame->id != app_ack_id(can_addr)))
    {
        return ACK_MATCH_NONE;
    }
    if ((frame->dlc != 8u) ||
        (frame->data[0] != 0x5Au) ||
        (frame->data[1] != 0xA5u) ||
        (frame->data[2] != cmd))
    {
        return ACK_MATCH_NONE;
    }

    expect_crc = rd_be16(&frame->data[6]);
    actual_crc = CtCrc16_Calc(frame->data, 6u);
    if ((expect_crc != actual_crc) || (frame->data[3] != 0u))
    {
        return ACK_MATCH_BAD;
    }

    if (v0 != 0)
    {
        *v0 = frame->data[4];
    }
    if (v1 != 0)
    {
        *v1 = frame->data[5];
    }
    return ACK_MATCH_OK;
}

static int decode_app_word_frame(const CtCanFrame *frame, uint8_t can_addr, uint8_t *seq, uint16_t *value)
{
    uint16_t expect_crc;
    uint16_t actual_crc;

    if ((frame->ide != 0u) || (frame->id != app_ack_id(can_addr)))
    {
        return 0;
    }
    if ((frame->dlc != 8u) ||
        (frame->data[0] != 0x5Au) ||
        (frame->data[1] != 0xA5u) ||
        (frame->data[2] != CT_CAN_APP_READ_BLOCK_DATA))
    {
        return 0;
    }

    expect_crc = rd_be16(&frame->data[6]);
    actual_crc = CtCrc16_Calc(frame->data, 6u);
    if (expect_crc != actual_crc)
    {
        return 0;
    }

    if (seq != 0)
    {
        *seq = frame->data[3];
    }
    if (value != 0)
    {
        *value = rd_be16(&frame->data[4]);
    }
    return 1;
}

static void drain_can_rx(void)
{
    CtCanFrame frame;
    uint8_t limit;

    for (limit = 0u; limit < 64u; ++limit)
    {
        if (!CtBoard_CanRecv(&frame, 0u))
        {
            break;
        }
    }
}

static int send_app_cmd_wait_ack(uint8_t can_addr, uint8_t cmd, uint8_t a0, uint8_t a1, uint8_t a2,
                                 uint8_t *v0, uint8_t *v1, uint32_t timeout_ms)
{
    CtCanFrame frame;
    uint32_t start;
    uint32_t last_send;
    uint32_t wait_ms;
    int match;

    start = CtBoard_GetTickMs();
    last_send = start - APP_CMD_RETRY_INTERVAL_MS;

    while (!timeout_expired(start, timeout_ms))
    {
        if ((uint32_t)(CtBoard_GetTickMs() - last_send) >= APP_CMD_RETRY_INTERVAL_MS)
        {
            if (!send_app_cmd(can_addr, cmd, a0, a1, a2))
            {
                return 0;
            }
            last_send = CtBoard_GetTickMs();
        }

        wait_ms = timeout_left(start, timeout_ms);
        if (wait_ms > APP_CMD_WAIT_SLICE_MS)
        {
            wait_ms = APP_CMD_WAIT_SLICE_MS;
        }
        if ((wait_ms != 0u) && CtBoard_CanRecv(&frame, wait_ms))
        {
            match = decode_app_ack(&frame, can_addr, cmd, v0, v1);
            if (match == ACK_MATCH_OK)
            {
                return 1;
            }
            if (match == ACK_MATCH_BAD)
            {
                return 0;
            }
        }
    }

    return 0;
}

int CtCan_AppGetStatus(uint8_t can_addr, uint8_t *soc, uint8_t *soh)
{
    uint8_t v0;
    uint8_t v1;

    if (!send_app_cmd_wait_ack(can_addr, CT_CAN_APP_GET_STATUS, 0u, 0u, 0u,
                               &v0, &v1, APP_GET_STATUS_TIMEOUT_MS))
    {
        return 0;
    }
    if (soc != 0)
    {
        *soc = v0;
    }
    if (soh != 0)
    {
        *soh = v1;
    }
    return 1;
}

int CtCan_AppEnterIap(uint8_t can_addr)
{
    return send_app_cmd_wait_ack(can_addr, CT_CAN_APP_ENTER_IAP, 0xC3u, 0x3Cu, can_addr,
                                 0, 0, APP_ENTER_IAP_TIMEOUT_MS);
}

int CtCan_AppReadRegs(uint8_t can_addr, uint16_t addr, uint16_t count, uint16_t *words)
{
    CtCanFrame frame;
    uint8_t received[CT_CAN_APP_READ_BLOCK_MAX_WORDS];
    uint8_t ack_count;
    uint8_t dummy;
    uint8_t seq;
    uint16_t value;
    uint16_t received_count;
    uint32_t start;
    uint32_t wait_ms;

    s_last_gateway_error = CT_CAN_GATEWAY_ERR_NONE;
    if ((words == 0) || (count == 0u) || (count > CT_CAN_APP_READ_BLOCK_MAX_WORDS))
    {
        s_last_gateway_error = CT_CAN_GATEWAY_ERR_BMS;
        return 0;
    }

    drain_can_rx();
    if (!send_app_cmd_wait_ack(can_addr,
                               CT_CAN_APP_READ_BLOCK,
                               (uint8_t)(addr >> 8),
                               (uint8_t)addr,
                               (uint8_t)count,
                               &ack_count,
                               &dummy,
                               APP_REG_CMD_TIMEOUT_MS))
    {
        uint16_t i;
        uint8_t v0;
        uint8_t v1;

        if (count > APP_SINGLE_READ_FALLBACK_MAX)
        {
            s_last_gateway_error = CT_CAN_GATEWAY_ERR_BMS;
            return 0;
        }
        for (i = 0u; i < count; ++i)
        {
            if (!send_app_cmd_wait_ack(can_addr,
                                       CT_CAN_APP_READ_REG,
                                       (uint8_t)((addr + i) >> 8),
                                       (uint8_t)(addr + i),
                                       0u,
                                       &v0,
                                       &v1,
                                       APP_REG_CMD_TIMEOUT_MS))
            {
                s_last_gateway_error = CT_CAN_GATEWAY_ERR_BMS;
                return 0;
            }
            words[i] = ((uint16_t)v0 << 8) | v1;
        }
        return 1;
    }
    if (ack_count != (uint8_t)count)
    {
        s_last_gateway_error = CT_CAN_GATEWAY_ERR_BMS;
        return 0;
    }

    memset(received, 0, sizeof(received));
    received_count = 0u;
    start = CtBoard_GetTickMs();
    while (!timeout_expired(start, APP_BLOCK_DATA_TIMEOUT_MS) && (received_count < count))
    {
        wait_ms = timeout_left(start, APP_BLOCK_DATA_TIMEOUT_MS);
        if (wait_ms > APP_CMD_WAIT_SLICE_MS)
        {
            wait_ms = APP_CMD_WAIT_SLICE_MS;
        }
        if ((wait_ms == 0u) || !CtBoard_CanRecv(&frame, wait_ms))
        {
            continue;
        }
        if (!decode_app_word_frame(&frame, can_addr, &seq, &value))
        {
            continue;
        }
        if (((uint16_t)seq < count) && (received[seq] == 0u))
        {
            words[seq] = value;
            received[seq] = 1u;
            received_count++;
        }
    }

    if (received_count != count)
    {
        s_last_gateway_error = CT_CAN_GATEWAY_ERR_TIMEOUT;
        drain_can_rx();
        return 0;
    }

    return 1;
}

int CtCan_AppWriteRegs(uint8_t can_addr, uint16_t addr, uint16_t count, const uint16_t *words)
{
    uint16_t i;
    uint16_t reg_addr;
    uint16_t value;

    if ((words == 0) || (count == 0u))
    {
        return 0;
    }

    for (i = 0u; i < count; ++i)
    {
        reg_addr = (uint16_t)(addr + i);
        value = words[i];
        if (!send_app_cmd_wait_ack(can_addr,
                                   CT_CAN_APP_WRITE_PREP,
                                   (uint8_t)(reg_addr >> 8),
                                   (uint8_t)reg_addr,
                                   (uint8_t)(value >> 8),
                                   0,
                                   0,
                                   APP_REG_CMD_TIMEOUT_MS))
        {
            return 0;
        }
        if (!send_app_cmd_wait_ack(can_addr,
                                   CT_CAN_APP_WRITE_COMMIT,
                                   (uint8_t)(reg_addr >> 8),
                                   (uint8_t)reg_addr,
                                   (uint8_t)value,
                                   0,
                                   0,
                                   APP_REG_CMD_TIMEOUT_MS))
        {
            return 0;
        }
    }

    return 1;
}

int CtCan_AppAgingControl(uint8_t can_addr, uint8_t action, uint8_t *state, uint8_t *remaining_hours)
{
    uint8_t cmd;

    switch (action)
    {
    case CT_CAN_APP_AGING_ACTION_START:
        cmd = CT_CAN_APP_AGING_START;
        break;
    case CT_CAN_APP_AGING_ACTION_STOP:
        cmd = CT_CAN_APP_AGING_STOP;
        break;
    case CT_CAN_APP_AGING_ACTION_RESET:
        cmd = CT_CAN_APP_AGING_RESET_TIME;
        break;
    default:
        return 0;
    }

    return send_app_cmd_wait_ack(can_addr,
                                 cmd,
                                 CT_CAN_APP_AGING_GUARD,
                                 action,
                                 can_addr,
                                 state,
                                 remaining_hours,
                                 APP_AGING_CMD_TIMEOUT_MS);
}

int CtCan_AppSetAgingHours(uint8_t can_addr, uint16_t hours, uint8_t *state, uint8_t *remaining_hours)
{
    if ((hours == 0u) || (hours > 168u))
    {
        return 0;
    }

    return send_app_cmd_wait_ack(can_addr,
                                 CT_CAN_APP_AGING_SET_HOURS,
                                 CT_CAN_APP_AGING_GUARD,
                                 (uint8_t)hours,
                                 can_addr,
                                 state,
                                 remaining_hours,
                                 APP_AGING_CMD_TIMEOUT_MS);
}

int CtCan_ReadFactoryAgingBroadcast(uint8_t *state, uint16_t *remaining_minutes, uint32_t timeout_ms)
{
    CtCanFrame frame;
    uint32_t start;
    uint32_t wait_ms;

    start = CtBoard_GetTickMs();
    while (!timeout_expired(start, timeout_ms))
    {
        wait_ms = timeout_left(start, timeout_ms);
        if (wait_ms > APP_AGING_BROADCAST_SLICE_MS)
        {
            wait_ms = APP_AGING_BROADCAST_SLICE_MS;
        }
        if ((wait_ms != 0u) && CtBoard_CanRecv(&frame, wait_ms))
        {
            if ((frame.ide != 0u) &&
                (frame.id == CT_CAN_FEIDAO_FACTORY_TIME_ID) &&
                (frame.dlc >= 5u))
            {
                if (state != 0)
                {
                    *state = frame.data[2];
                }
                if (remaining_minutes != 0)
                {
                    *remaining_minutes = rd_be16(&frame.data[3]);
                }
                return 1;
            }
        }
    }

    return 0;
}

static uint32_t iap_ctrl_id(uint8_t node)
{
    return CT_CAN_IAP_CTRL_BASE | node;
}

static uint32_t iap_ack_id(uint8_t node)
{
    return CT_CAN_IAP_ACK_BASE | node;
}

static uint32_t iap_data_id(uint8_t node, uint16_t seq)
{
    return CT_CAN_IAP_DATA_BASE | ((uint32_t)seq << 8) | node;
}

static int send_iap_ctrl(uint8_t node, const uint8_t data[8])
{
    CtCanFrame frame;

    memset(&frame, 0, sizeof(frame));
    frame.id = iap_ctrl_id(node);
    frame.ide = 1u;
    frame.dlc = 8u;
    memcpy(frame.data, data, 8u);
    return CtBoard_CanSend(&frame, 200u);
}

int CtCan_IapSendHello(uint8_t node)
{
    uint8_t data[8] = {CT_CAN_IAP_HELLO, CT_PROTOCOL_VERSION, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    data[2] = node;
    return send_iap_ctrl(node, data);
}

int CtCan_IapSendStart(uint8_t node, uint32_t size, uint16_t crc16)
{
    uint8_t data[8];
    data[0] = CT_CAN_IAP_START;
    data[1] = CT_PROTOCOL_VERSION;
    wr_be32(&data[2], size);
    wr_be16(&data[6], crc16);
    return send_iap_ctrl(node, data);
}

int CtCan_IapSendData(uint8_t node, uint16_t seq, const uint8_t data[8])
{
    CtCanFrame frame;

    memset(&frame, 0, sizeof(frame));
    frame.id = iap_data_id(node, seq);
    frame.ide = 1u;
    frame.dlc = 8u;
    memcpy(frame.data, data, 8u);
    return CtBoard_CanSend(&frame, 200u);
}

int CtCan_IapSendCommit(uint8_t node, uint16_t block_seq, uint16_t block_len, uint16_t block_crc)
{
    uint8_t data[8];
    data[0] = CT_CAN_IAP_COMMIT;
    wr_be16(&data[1], block_seq);
    wr_be16(&data[3], block_len);
    wr_be16(&data[5], block_crc);
    data[7] = 0xFFu;
    return send_iap_ctrl(node, data);
}

int CtCan_IapSendEnd(uint8_t node, uint16_t frame_count, uint16_t crc16)
{
    uint8_t data[8];
    data[0] = CT_CAN_IAP_END;
    wr_be16(&data[1], frame_count);
    wr_be16(&data[3], crc16);
    data[5] = 0xFFu;
    data[6] = 0xFFu;
    data[7] = 0xFFu;
    return send_iap_ctrl(node, data);
}

int CtCan_IapWaitAck(uint8_t node, uint8_t cmd, uint16_t *expect_seq, uint32_t timeout_ms)
{
    CtCanFrame frame;
    uint32_t start;
    uint32_t wait_ms;

    start = CtBoard_GetTickMs();
    while (!timeout_expired(start, timeout_ms))
    {
        wait_ms = timeout_left(start, timeout_ms);
        if (wait_ms > IAP_ACK_WAIT_SLICE_MS)
        {
            wait_ms = IAP_ACK_WAIT_SLICE_MS;
        }
        if ((wait_ms == 0u) || !CtBoard_CanRecv(&frame, wait_ms))
        {
            continue;
        }

        if ((frame.ide == 0u) || (frame.id != iap_ack_id(node)))
        {
            continue;
        }
        if (frame.dlc < 6u)
        {
            return 0;
        }
        if ((frame.data[0] == CT_CAN_IAP_NACK) && (frame.data[1] == cmd))
        {
            return 0;
        }
        if ((frame.data[0] != CT_CAN_IAP_ACK) || (frame.data[1] != cmd))
        {
            continue;
        }
        if (frame.data[5] != 0u)
        {
            return 0;
        }
        if (expect_seq != 0)
        {
            *expect_seq = rd_be16(&frame.data[3]);
        }
        return 1;
    }

    return 0;
}

uint8_t CtCan_IapPollAck(uint8_t node, uint8_t cmd, uint16_t *expect_seq, uint8_t *code)
{
    CtCanFrame frame;
    uint8_t scanned = 0u;

    if (code != 0)
    {
        *code = 0u;
    }

    while (scanned < 16u)
    {
        if (!CtBoard_CanRecv(&frame, 0u))
        {
            break;
        }
        scanned++;

        if ((frame.ide == 0u) || (frame.id != iap_ack_id(node)))
        {
            continue;
        }
        if (frame.dlc < 6u)
        {
            if (code != 0)
            {
                *code = 0xFEu;
            }
            return CT_CAN_IAP_ACK_MATCH_BAD;
        }
        if ((frame.data[0] == CT_CAN_IAP_NACK) && (frame.data[1] == cmd))
        {
            if (expect_seq != 0)
            {
                *expect_seq = rd_be16(&frame.data[3]);
            }
            if (code != 0)
            {
                *code = frame.data[5];
            }
            return CT_CAN_IAP_ACK_MATCH_BAD;
        }
        if ((frame.data[0] != CT_CAN_IAP_ACK) || (frame.data[1] != cmd))
        {
            continue;
        }
        if (frame.data[5] != 0u)
        {
            if (expect_seq != 0)
            {
                *expect_seq = rd_be16(&frame.data[3]);
            }
            if (code != 0)
            {
                *code = frame.data[5];
            }
            return CT_CAN_IAP_ACK_MATCH_BAD;
        }
        if (expect_seq != 0)
        {
            *expect_seq = rd_be16(&frame.data[3]);
        }
        return CT_CAN_IAP_ACK_MATCH_OK;
    }

    return CT_CAN_IAP_ACK_MATCH_NONE;
}
