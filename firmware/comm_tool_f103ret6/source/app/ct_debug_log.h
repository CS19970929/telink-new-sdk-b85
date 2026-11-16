#ifndef CT_DEBUG_LOG_H
#define CT_DEBUG_LOG_H

#include <stdint.h>
#include "ct_config.h"

#define CT_DEBUG_LOG_ENTRY_SIZE        12u
#define CT_DEBUG_LOG_HEADER_SIZE       6u

enum
{
    CT_LOG_MOD_APP = 1u,
    CT_LOG_MOD_UART = 2u,
    CT_LOG_MOD_CAN = 3u,
    CT_LOG_MOD_FLASH = 4u,
    CT_LOG_MOD_UPGRADE = 5u,
    CT_LOG_MOD_PROTOCOL = 6u
};

enum
{
    CT_LOG_EVT_BOOT = 1u,
    CT_LOG_EVT_CMD_RX = 2u,
    CT_LOG_EVT_CMD_TX = 3u,
    CT_LOG_EVT_BAD_FRAME = 4u,
    CT_LOG_EVT_CAN_SET = 5u,
    CT_LOG_EVT_CAN_TX_FAIL = 6u,
    CT_LOG_EVT_CAN_TX_TIMEOUT = 7u,
    CT_LOG_EVT_FW_BEGIN = 8u,
    CT_LOG_EVT_FW_END = 9u,
    CT_LOG_EVT_UPGRADE_START = 10u,
    CT_LOG_EVT_UPGRADE_PHASE = 11u,
    CT_LOG_EVT_UPGRADE_ERROR = 12u,
    CT_LOG_EVT_UPGRADE_ABORT = 13u
};

#if CT_DEBUG_LOG_ENABLE
void CtDebugLog_Record(uint8_t module, uint8_t event, uint16_t value0, uint16_t value1);
#else
#define CtDebugLog_Record(module, event, value0, value1) ((void)0)
#endif

uint8_t CtDebugLog_IsEnabled(void);
uint16_t CtDebugLog_EncodeLatest(uint8_t max_entries,
                                  uint8_t clear_after_read,
                                  uint8_t *out,
                                  uint16_t out_size);
void CtDebugLog_Clear(void);

#endif
