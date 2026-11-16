#include "ct_self_iap.h"
#include "ct_board_port.h"
#include "ct_boot_control.h"
#include "ct_can_gateway.h"
#include "ct_config.h"
#include "ct_crc16.h"
#include <string.h>

#define CT_LEGACY_SLAVE_ADDR           0x01u
#define CT_LEGACY_BROADCAST_ADDR       0x00u
#define CT_LEGACY_CMD_READ_REGS        0x03u
#define CT_LEGACY_CMD_WRITE_REGS       0x10u
#define CT_LEGACY_FLASH_CONNECT_ADDR   0xFFFDu
#define CT_LEGACY_RO_START             0xD000u
#define CT_LEGACY_RO_STATUS_ADDR       0xD050u
#define CT_LEGACY_READ_WORDS_MAX       64u
#define CT_LEGACY_RX_BUF_SIZE          32u
#define CT_LEGACY_FRAME_TIMEOUT_MS     500u
#define CT_LEGACY_RESPONSE_DELAY_MS    20u
#define CT_SELF_RESET_DELAY_MS         20u

static uint8_t s_legacy_buf[CT_LEGACY_RX_BUF_SIZE];
static uint8_t s_legacy_index;
static uint8_t s_legacy_expect;
static uint32_t s_legacy_last_rx_ms;
static uint8_t s_reset_pending;
static uint32_t s_reset_time_ms;

static uint16_t rd_be16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

static void wr_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static uint16_t legacy_crc16(const uint8_t *data, uint16_t length)
{
    return CtCrc16_Calc(data, length);
}

static uint32_t app_req_id(uint8_t can_addr)
{
    return (((uint32_t)can_addr & 0x0Fu) << 7) | CT_CAN_APP_REQ_BASE;
}

static uint32_t app_ack_id(uint8_t can_addr)
{
    return (((uint32_t)can_addr & 0x0Fu) << 7) | CT_CAN_APP_ACK_BASE;
}

static void schedule_reset(void)
{
    s_reset_pending = 1u;
    s_reset_time_ms = CtBoard_GetTickMs() + CT_SELF_RESET_DELAY_MS;
}

static void legacy_reset_parser(void)
{
    s_legacy_index = 0u;
    s_legacy_expect = 0u;
}

static void legacy_check_frame_timeout(void)
{
    if ((s_legacy_index != 0u) &&
        ((uint32_t)(CtBoard_GetTickMs() - s_legacy_last_rx_ms) >= CT_LEGACY_FRAME_TIMEOUT_MS))
    {
        legacy_reset_parser();
    }
}

static void legacy_delay_ms(uint32_t delay_ms)
{
    uint32_t start = CtBoard_GetTickMs();

    while ((uint32_t)(CtBoard_GetTickMs() - start) < delay_ms)
    {
    }
}

static void legacy_send_write_ack(const uint8_t *request)
{
    uint8_t ack[8];
    uint16_t crc;

    memcpy(ack, request, 6u);
    crc = CtCrc16_Calc(ack, 6u);
    ack[6] = (uint8_t)crc;
    ack[7] = (uint8_t)(crc >> 8);
    legacy_delay_ms(CT_LEGACY_RESPONSE_DELAY_MS);
    (void)CtBoard_UartWrite(ack, (uint16_t)sizeof(ack));
}

static uint16_t legacy_status_word(uint16_t addr)
{
    uint16_t offset;

    if (addr >= CT_LEGACY_RO_STATUS_ADDR)
    {
        offset = (uint16_t)(addr - CT_LEGACY_RO_STATUS_ADDR);
    }
    else if (addr >= CT_LEGACY_RO_START)
    {
        offset = (uint16_t)(addr - CT_LEGACY_RO_START);
    }
    else
    {
        return 0u;
    }

    switch (offset)
    {
    case 0u:
        return 0u;
    case 1u:
        return 0u;
    case 2u:
        return 0u;
    case 3u:
        return 0u;
    default:
        return 0u;
    }
}

static void legacy_send_read_ack(const uint8_t *request)
{
    uint8_t ack[3u + (CT_LEGACY_READ_WORDS_MAX * 2u) + 2u];
    uint16_t addr;
    uint16_t count;
    uint16_t value;
    uint16_t crc;
    uint16_t i;
    uint16_t pos;

    addr = rd_be16(&request[2]);
    count = rd_be16(&request[4]);
    if ((count == 0u) || (count > CT_LEGACY_READ_WORDS_MAX) || (addr < CT_LEGACY_RO_START))
    {
        return;
    }

    ack[0] = request[0];
    ack[1] = CT_LEGACY_CMD_READ_REGS;
    ack[2] = (uint8_t)(count * 2u);
    pos = 3u;
    for (i = 0u; i < count; ++i)
    {
        value = legacy_status_word((uint16_t)(addr + i));
        wr_be16(&ack[pos], value);
        pos = (uint16_t)(pos + 2u);
    }

    crc = legacy_crc16(ack, pos);
    ack[pos++] = (uint8_t)crc;
    ack[pos++] = (uint8_t)(crc >> 8);
    legacy_delay_ms(CT_LEGACY_RESPONSE_DELAY_MS);
    (void)CtBoard_UartWrite(ack, pos);
}

static void legacy_handle_frame(void)
{
    uint16_t addr;
    uint16_t count;
    uint16_t expect_crc;
    uint16_t actual_crc;

    expect_crc = (uint16_t)s_legacy_buf[s_legacy_expect - 2u] |
                 ((uint16_t)s_legacy_buf[s_legacy_expect - 1u] << 8);
    actual_crc = legacy_crc16(s_legacy_buf, (uint16_t)(s_legacy_expect - 2u));
    if (expect_crc != actual_crc)
    {
        return;
    }

    if (s_legacy_buf[1] == CT_LEGACY_CMD_READ_REGS)
    {
        legacy_send_read_ack(s_legacy_buf);
        return;
    }
    if (s_legacy_expect < 9u)
    {
        return;
    }

    addr = rd_be16(&s_legacy_buf[2]);
    count = rd_be16(&s_legacy_buf[4]);
    if ((s_legacy_buf[1] == CT_LEGACY_CMD_WRITE_REGS) &&
        (addr == CT_LEGACY_FLASH_CONNECT_ADDR) &&
        (count == 1u) &&
        (s_legacy_buf[6] == 2u) &&
        (CtBoot_RequestIap() != 0))
    {
        legacy_send_write_ack(s_legacy_buf);
        schedule_reset();
    }
}

void CtSelfIap_Init(void)
{
    legacy_reset_parser();
    s_reset_pending = 0u;
    CtBoot_ClearRequest();
}

void CtSelfIap_FeedUartByte(uint8_t byte)
{
    legacy_check_frame_timeout();
    if (s_legacy_index == 0u)
    {
        if ((byte != CT_LEGACY_SLAVE_ADDR) && (byte != CT_LEGACY_BROADCAST_ADDR))
        {
            return;
        }
    }
    else if (s_legacy_index == 1u)
    {
        if ((byte != CT_LEGACY_CMD_WRITE_REGS) && (byte != CT_LEGACY_CMD_READ_REGS))
        {
            legacy_reset_parser();
            return;
        }
    }

    if (s_legacy_index >= CT_LEGACY_RX_BUF_SIZE)
    {
        legacy_reset_parser();
        return;
    }

    s_legacy_buf[s_legacy_index++] = byte;
    s_legacy_last_rx_ms = CtBoard_GetTickMs();
    if ((s_legacy_index == 6u) && (s_legacy_buf[1] == CT_LEGACY_CMD_READ_REGS))
    {
        s_legacy_expect = 8u;
    }
    else if ((s_legacy_index == 7u) && (s_legacy_buf[1] == CT_LEGACY_CMD_WRITE_REGS))
    {
        s_legacy_expect = (uint8_t)(9u + s_legacy_buf[6]);
        if ((s_legacy_expect > CT_LEGACY_RX_BUF_SIZE) || (s_legacy_expect < 9u))
        {
            legacy_reset_parser();
            return;
        }
    }

    if ((s_legacy_expect != 0u) && (s_legacy_index >= s_legacy_expect))
    {
        legacy_handle_frame();
        legacy_reset_parser();
    }
}

static int can_decode_request(const CtCanFrame *frame, uint8_t *cmd, uint8_t args[3])
{
    uint16_t expect_crc;
    uint16_t actual_crc;

    if ((frame->ide != 0u) || (frame->id != app_req_id(CT_SELF_CAN_APP_ADDR)) || (frame->dlc != 8u))
    {
        return 0;
    }
    if ((frame->data[0] != 0xA5u) || (frame->data[1] != 0x5Au))
    {
        return 0;
    }

    expect_crc = rd_be16(&frame->data[6]);
    actual_crc = CtCrc16_Calc(frame->data, 6u);
    if (expect_crc != actual_crc)
    {
        return 0;
    }

    *cmd = frame->data[2];
    args[0] = frame->data[3];
    args[1] = frame->data[4];
    args[2] = frame->data[5];
    return 1;
}

static void can_send_ack(uint8_t cmd, uint8_t status, uint8_t v0, uint8_t v1)
{
    CtCanFrame ack;
    uint16_t crc;

    memset(&ack, 0, sizeof(ack));
    ack.id = app_ack_id(CT_SELF_CAN_APP_ADDR);
    ack.ide = 0u;
    ack.dlc = 8u;
    ack.data[0] = 0x5Au;
    ack.data[1] = 0xA5u;
    ack.data[2] = cmd;
    ack.data[3] = status;
    ack.data[4] = v0;
    ack.data[5] = v1;
    crc = CtCrc16_Calc(ack.data, 6u);
    wr_be16(&ack.data[6], crc);
    (void)CtBoard_CanSend(&ack, 100u);
}

void CtSelfIap_PollCan(void)
{
    CtCanFrame frame;
    uint8_t cmd;
    uint8_t args[3];
    uint8_t limit;

    for (limit = 0u; limit < 8u; ++limit)
    {
        if (!CtBoard_CanRecv(&frame, 0u))
        {
            break;
        }

        if (!can_decode_request(&frame, &cmd, args))
        {
            continue;
        }

        if (cmd == CT_CAN_APP_GET_STATUS)
        {
            can_send_ack(cmd, 0u, 0u, 100u);
        }
        else if ((cmd == CT_CAN_APP_ENTER_IAP) &&
                 (args[0] == 0xC3u) &&
                 (args[1] == 0x3Cu) &&
                 ((args[2] & 0x0Fu) == CT_SELF_CAN_APP_ADDR) &&
                 (CtBoot_RequestIap() != 0))
        {
            can_send_ack(cmd, 0u, 0u, 0u);
            schedule_reset();
        }
        else
        {
            can_send_ack(cmd, 1u, 0u, 0u);
        }
    }
}

void CtSelfIap_Task(void)
{
    legacy_check_frame_timeout();

    if ((s_reset_pending != 0u) &&
        ((int32_t)(CtBoard_GetTickMs() - s_reset_time_ms) >= 0))
    {
        s_reset_pending = 0u;
        CtBoard_Reset();
    }
}
