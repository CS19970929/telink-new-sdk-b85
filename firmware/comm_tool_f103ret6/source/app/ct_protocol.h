#ifndef CT_PROTOCOL_H
#define CT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "ct_config.h"

#define CT_UART_MAGIC                  0xAA55u
#define CT_UART_FLAG_ACK               0x01u

#define CT_CMD_GET_INFO                0x01u
#define CT_CMD_SET_CAN                 0x02u
#define CT_CMD_BMS_READ                0x10u
#define CT_CMD_BMS_WRITE               0x11u
#define CT_CMD_BMS_AGING_CTRL          0x12u
#define CT_CMD_BMS_AGING_STATUS        0x13u
#define CT_CMD_BMS_AGING_SET_HOURS     0x14u
#define CT_CMD_SET_BMS_UART            0x15u
#define CT_CMD_FW_BEGIN                0x20u
#define CT_CMD_FW_DATA                 0x21u
#define CT_CMD_FW_END                  0x22u
#define CT_CMD_FW_INFO                 0x23u
#define CT_CMD_ENTER_IAP               0x30u
#define CT_CMD_UPGRADE                 0x31u
#define CT_CMD_UPGRADE_STATUS          0x32u
#define CT_CMD_UPGRADE_ABORT           0x33u
#define CT_CMD_RAW_CAN_TX              0x40u
#define CT_CMD_CAN_DIAG                0x41u
#define CT_CMD_DEBUG_LOG               0x42u
#define CT_CMD_OWC_CONTROL             0x43u
#define CT_CMD_OWC_SNAPSHOT            0x44u

typedef struct
{
    uint8_t version;
    uint8_t flags;
    uint16_t seq;
    uint8_t cmd;
    uint8_t status;
    uint16_t length;
    uint8_t payload[CT_UART_MAX_PAYLOAD];
} CtFrame;

typedef struct
{
    uint8_t state;
    uint16_t index;
    uint16_t payload_length;
    uint8_t raw[10u + CT_UART_MAX_PAYLOAD + 2u];
    CtFrame frame;
} CtProtocolParser;

void CtProtocol_Init(CtProtocolParser *parser);
uint8_t CtProtocol_Feed(CtProtocolParser *parser, uint8_t byte, CtFrame *out_frame);
uint16_t CtProtocol_Encode(uint16_t seq,
                           uint8_t cmd,
                           uint8_t status,
                           const uint8_t *payload,
                           uint16_t payload_len,
                           uint8_t flags,
                           uint8_t *out,
                           uint16_t out_size);

#endif
