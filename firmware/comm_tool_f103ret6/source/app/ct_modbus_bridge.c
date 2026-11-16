#include "ct_modbus_bridge.h"
#include "board_uart.h"
#include "ct_board_port.h"
#include "ct_config.h"
#include "ct_crc16.h"

#define CT_MODBUS_BRIDGE_MAX_FRAME          1040u
#define CT_MODBUS_BRIDGE_PC_GAP_TIMEOUT_MS  100u
#define CT_MODBUS_BRIDGE_BMS_GAP_TIMEOUT_MS 100u
#define CT_MODBUS_BRIDGE_RESPONSE_TIMEOUT_MS 3000u

#define MODBUS_FUNC_READ_HOLDING_REGS       0x03u
#define MODBUS_FUNC_WRITE_SINGLE_REG        0x06u
#define MODBUS_FUNC_WRITE_MULTI_REGS        0x10u

static uint8_t s_pc_buf[CT_MODBUS_BRIDGE_MAX_FRAME];
static uint16_t s_pc_index;
static uint16_t s_pc_expect;
static uint32_t s_pc_last_rx_ms;

static uint8_t s_bms_buf[CT_MODBUS_BRIDGE_MAX_FRAME];
static uint16_t s_bms_index;
static uint16_t s_bms_expect;
static uint32_t s_bms_first_rx_ms;
static uint32_t s_bms_last_rx_ms;
static uint8_t s_waiting_response;
static uint8_t s_request_slave;
static uint8_t s_request_func;
static uint8_t s_private_header[10];
static uint16_t s_private_index;
static uint16_t s_private_expect;
static uint8_t s_private_active;
static uint32_t s_private_last_rx_ms;

static uint16_t rd_be16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

static uint8_t modbus_func_supported(uint8_t func)
{
    return ((func == MODBUS_FUNC_READ_HOLDING_REGS) ||
            (func == MODBUS_FUNC_WRITE_SINGLE_REG) ||
            (func == MODBUS_FUNC_WRITE_MULTI_REGS)) ? 1u : 0u;
}

static uint8_t modbus_crc_ok(const uint8_t *data, uint16_t length)
{
    uint16_t expect_crc;
    uint16_t actual_crc;

    if ((data == 0) || (length < 4u))
    {
        return 0u;
    }

    expect_crc = (uint16_t)data[length - 2u] | ((uint16_t)data[length - 1u] << 8);
    actual_crc = CtCrc16_Calc(data, (size_t)(length - 2u));
    return (expect_crc == actual_crc) ? 1u : 0u;
}

static void pc_reset_parser(void)
{
    s_pc_index = 0u;
    s_pc_expect = 0u;
}

static void private_reset_parser(void)
{
    s_private_index = 0u;
    s_private_expect = 0u;
    s_private_active = 0u;
}

static void bms_reset_parser(void)
{
    s_bms_index = 0u;
    s_bms_expect = 0u;
    s_waiting_response = 0u;
}

static void pc_check_gap_timeout(void)
{
    if ((s_private_active != 0u) &&
        ((uint32_t)(CtBoard_GetTickMs() - s_private_last_rx_ms) >= CT_MODBUS_BRIDGE_PC_GAP_TIMEOUT_MS))
    {
        private_reset_parser();
    }
    if ((s_pc_index != 0u) &&
        ((uint32_t)(CtBoard_GetTickMs() - s_pc_last_rx_ms) >= CT_MODBUS_BRIDGE_PC_GAP_TIMEOUT_MS))
    {
        pc_reset_parser();
    }
}

static uint8_t private_filter_byte(uint8_t byte)
{
    uint16_t payload_len;

    if (s_private_active == 0u)
    {
        if ((s_pc_index == 1u) && (s_pc_buf[0] == 0x55u) && (byte == 0xAAu))
        {
            pc_reset_parser();
            s_private_header[0] = 0x55u;
            s_private_header[1] = 0xAAu;
            s_private_index = 2u;
            s_private_expect = 0u;
            s_private_active = 1u;
            s_private_last_rx_ms = CtBoard_GetTickMs();
            return 1u;
        }
        return 0u;
    }

    s_private_last_rx_ms = CtBoard_GetTickMs();
    if (s_private_index < (uint16_t)sizeof(s_private_header))
    {
        s_private_header[s_private_index] = byte;
    }
    s_private_index++;

    if (s_private_index == (uint16_t)sizeof(s_private_header))
    {
        payload_len = (uint16_t)s_private_header[8] | ((uint16_t)s_private_header[9] << 8);
        if ((s_private_header[2] != CT_PROTOCOL_VERSION) || (payload_len > CT_UART_MAX_PAYLOAD))
        {
            private_reset_parser();
            return 1u;
        }
        s_private_expect = (uint16_t)(10u + payload_len + 2u);
    }

    if ((s_private_expect != 0u) && (s_private_index >= s_private_expect))
    {
        private_reset_parser();
    }
    return 1u;
}

static void bms_check_timeout(void)
{
    if (s_waiting_response == 0u)
    {
        return;
    }
    if ((s_bms_index != 0u) &&
        ((uint32_t)(CtBoard_GetTickMs() - s_bms_last_rx_ms) >= CT_MODBUS_BRIDGE_BMS_GAP_TIMEOUT_MS))
    {
        bms_reset_parser();
        return;
    }
    if ((uint32_t)(CtBoard_GetTickMs() - s_bms_first_rx_ms) >= CT_MODBUS_BRIDGE_RESPONSE_TIMEOUT_MS)
    {
        bms_reset_parser();
    }
}

static uint16_t pc_expected_length(void)
{
    uint8_t func;
    uint8_t byte_count;
    uint16_t count;
    uint16_t expect;

    func = s_pc_buf[1];
    if ((func == MODBUS_FUNC_READ_HOLDING_REGS) || (func == MODBUS_FUNC_WRITE_SINGLE_REG))
    {
        return 8u;
    }
    if (func != MODBUS_FUNC_WRITE_MULTI_REGS)
    {
        return 0u;
    }
    if (s_pc_index < 7u)
    {
        return 0u;
    }

    count = rd_be16(&s_pc_buf[4]);
    byte_count = s_pc_buf[6];
    if (count == 0u)
    {
        return 0u;
    }

    if (byte_count != 0u)
    {
        expect = (uint16_t)(9u + byte_count);
    }
    else
    {
        expect = (uint16_t)(9u + count);
    }
    if ((expect < 9u) || (expect > CT_MODBUS_BRIDGE_MAX_FRAME))
    {
        return 0u;
    }
    return expect;
}

static void start_bms_response_wait(uint8_t slave, uint8_t func)
{
    s_request_slave = slave;
    s_request_func = func;
    s_bms_index = 0u;
    s_bms_expect = 0u;
    s_waiting_response = (slave == 0u) ? 0u : 1u;
    s_bms_first_rx_ms = CtBoard_GetTickMs();
    s_bms_last_rx_ms = s_bms_first_rx_ms;
}

static void forward_pc_request(void)
{
    uint8_t slave;
    uint8_t func;

    if (modbus_crc_ok(s_pc_buf, s_pc_expect) == 0u)
    {
        return;
    }

    slave = s_pc_buf[0];
    func = s_pc_buf[1];
    if (BoardBmsUart_Write(s_pc_buf, s_pc_expect) != 0)
    {
        start_bms_response_wait(slave, func);
    }
}

void CtModbusBridge_Init(void)
{
    pc_reset_parser();
    bms_reset_parser();
    private_reset_parser();
    s_request_slave = 0u;
    s_request_func = 0u;
}

void CtModbusBridge_FeedPcByte(uint8_t byte)
{
    pc_check_gap_timeout();

    if (private_filter_byte(byte) != 0u)
    {
        return;
    }

    if (s_waiting_response != 0u)
    {
        return;
    }

    if (s_pc_index == 0u)
    {
        if (byte > 247u)
        {
            return;
        }
    }
    else if (s_pc_index == 1u)
    {
        if (modbus_func_supported(byte) == 0u)
        {
            pc_reset_parser();
            return;
        }
    }

    if (s_pc_index >= CT_MODBUS_BRIDGE_MAX_FRAME)
    {
        pc_reset_parser();
        return;
    }

    s_pc_buf[s_pc_index++] = byte;
    s_pc_last_rx_ms = CtBoard_GetTickMs();

    if ((s_pc_index == 2u) &&
        ((s_pc_buf[1] == MODBUS_FUNC_READ_HOLDING_REGS) ||
         (s_pc_buf[1] == MODBUS_FUNC_WRITE_SINGLE_REG)))
    {
        s_pc_expect = 8u;
    }
    else if ((s_pc_index == 7u) && (s_pc_buf[1] == MODBUS_FUNC_WRITE_MULTI_REGS))
    {
        s_pc_expect = pc_expected_length();
        if (s_pc_expect == 0u)
        {
            pc_reset_parser();
            return;
        }
    }

    if ((s_pc_expect != 0u) && (s_pc_index >= s_pc_expect))
    {
        forward_pc_request();
        pc_reset_parser();
    }
}

static uint16_t bms_expected_length(void)
{
    uint8_t func;
    uint8_t byte_count;
    uint16_t expect;

    func = s_bms_buf[1];
    if (func == (uint8_t)(s_request_func | 0x80u))
    {
        return 5u;
    }
    if (func != s_request_func)
    {
        return 0u;
    }

    if (func == MODBUS_FUNC_READ_HOLDING_REGS)
    {
        if (s_bms_index < 3u)
        {
            return 0u;
        }
        byte_count = s_bms_buf[2];
        expect = (uint16_t)(5u + byte_count);
        return (expect <= CT_MODBUS_BRIDGE_MAX_FRAME) ? expect : 0u;
    }
    if ((func == MODBUS_FUNC_WRITE_SINGLE_REG) || (func == MODBUS_FUNC_WRITE_MULTI_REGS))
    {
        return 8u;
    }
    return 0u;
}

static void forward_bms_response(void)
{
    if (modbus_crc_ok(s_bms_buf, s_bms_expect) != 0u)
    {
        (void)CtBoard_UartWrite(s_bms_buf, s_bms_expect);
    }
    bms_reset_parser();
}

static void feed_bms_byte(uint8_t byte)
{
    bms_check_timeout();
    if (s_waiting_response == 0u)
    {
        return;
    }

    if (s_bms_index == 0u)
    {
        if (byte != s_request_slave)
        {
            return;
        }
        s_bms_first_rx_ms = CtBoard_GetTickMs();
    }
    else if (s_bms_index == 1u)
    {
        if ((byte != s_request_func) && (byte != (uint8_t)(s_request_func | 0x80u)))
        {
            bms_reset_parser();
            return;
        }
    }

    if (s_bms_index >= CT_MODBUS_BRIDGE_MAX_FRAME)
    {
        bms_reset_parser();
        return;
    }

    s_bms_buf[s_bms_index++] = byte;
    s_bms_last_rx_ms = CtBoard_GetTickMs();

    if (s_bms_index == 2u)
    {
        if ((s_bms_buf[1] == (uint8_t)(s_request_func | 0x80u)) ||
            (s_request_func == MODBUS_FUNC_WRITE_SINGLE_REG) ||
            (s_request_func == MODBUS_FUNC_WRITE_MULTI_REGS))
        {
            s_bms_expect = bms_expected_length();
            if (s_bms_expect == 0u)
            {
                bms_reset_parser();
                return;
            }
        }
    }
    else if ((s_bms_index == 3u) && (s_request_func == MODBUS_FUNC_READ_HOLDING_REGS))
    {
        s_bms_expect = bms_expected_length();
        if (s_bms_expect == 0u)
        {
            bms_reset_parser();
            return;
        }
    }

    if ((s_bms_expect != 0u) && (s_bms_index >= s_bms_expect))
    {
        forward_bms_response();
    }
}

void CtModbusBridge_Task(void)
{
    uint8_t byte;
    uint8_t limit;

    pc_check_gap_timeout();
    bms_check_timeout();

    for (limit = 0u; limit < 64u; ++limit)
    {
        if (!BoardBmsUart_ReadByte(&byte))
        {
            break;
        }
        feed_bms_byte(byte);
    }
}
