#include "ct_protocol.h"
#include "ct_crc16.h"
#include "ct_debug_log.h"

enum
{
    CT_PROTO_HEADER_SIZE = 10u,
    CT_PROTO_CRC_SIZE = 2u
};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void wr16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

void CtProtocol_Init(CtProtocolParser *parser)
{
    if (parser == 0)
    {
        return;
    }
    parser->state = 0u;
    parser->index = 0u;
    parser->payload_length = 0u;
}

static void parser_restart_with_byte(CtProtocolParser *parser, uint8_t byte)
{
    parser->index = 0u;
    parser->payload_length = 0u;
    parser->state = 0u;
    if (byte == 0x55u)
    {
        parser->raw[0] = byte;
        parser->index = 1u;
        parser->state = 1u;
    }
}

uint8_t CtProtocol_Feed(CtProtocolParser *parser, uint8_t byte, CtFrame *out_frame)
{
    uint16_t total_length;
    uint16_t expect_crc;
    uint16_t actual_crc;
    uint16_t payload_len;
    uint16_t i;
    uint8_t *raw;

    if ((parser == 0) || (out_frame == 0))
    {
        return 0u;
    }

    raw = parser->raw;

    if (parser->state == 0u)
    {
        parser_restart_with_byte(parser, byte);
        return 0u;
    }

    raw[parser->index++] = byte;
    if ((parser->index == 2u) && ((raw[0] != 0x55u) || (raw[1] != 0xAAu)))
    {
        parser_restart_with_byte(parser, byte);
        return 0u;
    }

    if (parser->index == CT_PROTO_HEADER_SIZE)
    {
        payload_len = rd16(&raw[8]);
        if ((raw[2] != CT_PROTOCOL_VERSION) || (payload_len > CT_UART_MAX_PAYLOAD))
        {
            CtDebugLog_Record(CT_LOG_MOD_PROTOCOL,
                              CT_LOG_EVT_BAD_FRAME,
                              raw[2],
                              payload_len);
            CtProtocol_Init(parser);
            return 0u;
        }
        parser->payload_length = payload_len;
        parser->state = 2u;
    }

    if (parser->state != 2u)
    {
        return 0u;
    }

    total_length = (uint16_t)(CT_PROTO_HEADER_SIZE + parser->payload_length + CT_PROTO_CRC_SIZE);
    if (parser->index < total_length)
    {
        return 0u;
    }

    expect_crc = rd16(&raw[CT_PROTO_HEADER_SIZE + parser->payload_length]);
    actual_crc = CtCrc16_Calc(raw, (size_t)(CT_PROTO_HEADER_SIZE + parser->payload_length));
    if (expect_crc != actual_crc)
    {
        CtDebugLog_Record(CT_LOG_MOD_PROTOCOL,
                          CT_LOG_EVT_BAD_FRAME,
                          expect_crc,
                          actual_crc);
        CtProtocol_Init(parser);
        return 0u;
    }

    out_frame->version = raw[2];
    out_frame->flags = raw[3];
    out_frame->seq = rd16(&raw[4]);
    out_frame->cmd = raw[6];
    out_frame->status = raw[7];
    out_frame->length = parser->payload_length;
    for (i = 0u; i < parser->payload_length; ++i)
    {
        out_frame->payload[i] = raw[CT_PROTO_HEADER_SIZE + i];
    }

    CtProtocol_Init(parser);
    return 1u;
}

uint16_t CtProtocol_Encode(uint16_t seq,
                           uint8_t cmd,
                           uint8_t status,
                           const uint8_t *payload,
                           uint16_t payload_len,
                           uint8_t flags,
                           uint8_t *out,
                           uint16_t out_size)
{
    uint16_t crc;
    uint16_t i;
    uint16_t total;

    total = (uint16_t)(CT_PROTO_HEADER_SIZE + payload_len + CT_PROTO_CRC_SIZE);
    if ((out == 0) || (payload_len > CT_UART_MAX_PAYLOAD) || (out_size < total))
    {
        return 0u;
    }

    wr16(&out[0], CT_UART_MAGIC);
    out[2] = CT_PROTOCOL_VERSION;
    out[3] = flags;
    wr16(&out[4], seq);
    out[6] = cmd;
    out[7] = status;
    wr16(&out[8], payload_len);
    for (i = 0u; i < payload_len; ++i)
    {
        out[CT_PROTO_HEADER_SIZE + i] = payload[i];
    }

    crc = CtCrc16_Calc(out, (size_t)(CT_PROTO_HEADER_SIZE + payload_len));
    wr16(&out[CT_PROTO_HEADER_SIZE + payload_len], crc);
    return total;
}

