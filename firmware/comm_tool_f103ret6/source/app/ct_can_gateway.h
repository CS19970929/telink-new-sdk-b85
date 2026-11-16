#ifndef CT_CAN_GATEWAY_H
#define CT_CAN_GATEWAY_H

#include <stdint.h>
#include "ct_board_port.h"

#define CT_CAN_APP_REQ_BASE             0x060u
#define CT_CAN_APP_ACK_BASE             0x061u
#define CT_CAN_APP_GET_STATUS           0x01u
#define CT_CAN_APP_ENTER_IAP            0x02u
#define CT_CAN_APP_READ_REG             0x03u
#define CT_CAN_APP_WRITE_PREP           0x04u
#define CT_CAN_APP_WRITE_COMMIT         0x05u
#define CT_CAN_APP_READ_BLOCK           0x06u
#define CT_CAN_APP_AGING_START          0x07u
#define CT_CAN_APP_AGING_STOP           0x08u
#define CT_CAN_APP_AGING_RESET_TIME     0x09u
#define CT_CAN_APP_AGING_SET_HOURS      0x0Au
#define CT_CAN_APP_READ_BLOCK_DATA      0x86u
#define CT_CAN_APP_READ_BLOCK_MAX_WORDS 120u

#define CT_CAN_GATEWAY_ERR_NONE         0u
#define CT_CAN_GATEWAY_ERR_BMS          1u
#define CT_CAN_GATEWAY_ERR_TIMEOUT      2u
#define CT_CAN_APP_AGING_GUARD          0xA9u
#define CT_CAN_APP_AGING_ACTION_START   0x51u
#define CT_CAN_APP_AGING_ACTION_STOP    0x50u
#define CT_CAN_APP_AGING_ACTION_RESET   0x5Au
#define CT_CAN_FEIDAO_BROADCAST_BASE    0x14F80200u
#define CT_CAN_FEIDAO_FACTORY_TIME_ID   0x14F80208u

#define CT_CAN_IAP_CTRL_BASE            0x14F8F000u
#define CT_CAN_IAP_ACK_BASE             0x14F8F100u
#define CT_CAN_IAP_DATA_BASE            0x14000000u

#define CT_CAN_IAP_HELLO                0x01u
#define CT_CAN_IAP_START                0x02u
#define CT_CAN_IAP_COMMIT               0x03u
#define CT_CAN_IAP_END                  0x04u
#define CT_CAN_IAP_ABORT                0x05u
#define CT_CAN_IAP_ACK                  0x79u
#define CT_CAN_IAP_NACK                 0x1Fu

#define CT_CAN_IAP_ACK_MATCH_NONE       0u
#define CT_CAN_IAP_ACK_MATCH_OK         1u
#define CT_CAN_IAP_ACK_MATCH_BAD        2u

int CtCan_AppGetStatus(uint8_t can_addr, uint8_t *soc, uint8_t *soh);
int CtCan_AppEnterIap(uint8_t can_addr);
int CtCan_AppReadRegs(uint8_t can_addr, uint16_t addr, uint16_t count, uint16_t *words);
int CtCan_AppWriteRegs(uint8_t can_addr, uint16_t addr, uint16_t count, const uint16_t *words);
int CtCan_AppAgingControl(uint8_t can_addr, uint8_t action, uint8_t *state, uint8_t *remaining_hours);
int CtCan_AppSetAgingHours(uint8_t can_addr, uint16_t hours, uint8_t *state, uint8_t *remaining_hours);
int CtCan_ReadFactoryAgingBroadcast(uint8_t *state, uint16_t *remaining_minutes, uint32_t timeout_ms);
uint8_t CtCan_GetLastGatewayError(void);
int CtCan_IapSendHello(uint8_t node);
int CtCan_IapSendStart(uint8_t node, uint32_t size, uint16_t crc16);
int CtCan_IapSendData(uint8_t node, uint16_t seq, const uint8_t data[8]);
int CtCan_IapSendCommit(uint8_t node, uint16_t block_seq, uint16_t block_len, uint16_t block_crc);
int CtCan_IapSendEnd(uint8_t node, uint16_t frame_count, uint16_t crc16);
int CtCan_IapWaitAck(uint8_t node, uint8_t cmd, uint16_t *expect_seq, uint32_t timeout_ms);
uint8_t CtCan_IapPollAck(uint8_t node, uint8_t cmd, uint16_t *expect_seq, uint8_t *code);

#endif
