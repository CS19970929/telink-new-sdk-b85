#ifndef CT_BOARD_PORT_H
#define CT_BOARD_PORT_H

#include <stdint.h>

typedef struct
{
    uint32_t id;
    uint8_t ide;
    uint8_t dlc;
    uint8_t data[8];
} CtCanFrame;

typedef struct
{
    uint32_t tx_count;
    uint32_t tx_ok;
    uint32_t tx_fail;
    uint32_t tx_timeout;
    uint32_t rx_count;
    uint32_t rx_drop;
    uint32_t last_esr;
    uint32_t last_tsr;
    uint32_t last_msr;
    uint32_t last_rf0r;
    uint32_t last_tx_id;
    uint32_t last_rx_id;
    uint8_t last_tx_ide;
    uint8_t last_tx_dlc;
    uint8_t last_tx_status;
    uint8_t last_rx_ide;
    uint8_t last_rx_dlc;
    uint8_t last_rx_data[8];
} CtCanDiag;

uint32_t CtBoard_GetTickMs(void);
void CtBoard_Reset(void);
int CtBoard_OfflineUpgradeButtonActive(void);
int CtBoard_UartWrite(const uint8_t *data, uint16_t length);
int CtBoard_CanSend(const CtCanFrame *frame, uint32_t timeout_ms);
int CtBoard_CanRecv(CtCanFrame *frame, uint32_t timeout_ms);
int CtBoard_SetCanBitrate(uint32_t bitrate);
int CtBoard_SetBmsUartBaud(uint32_t baudrate);
void CtBoard_CanGetDiag(CtCanDiag *diag);
void CtBoard_CanClearDiag(void);

#endif
