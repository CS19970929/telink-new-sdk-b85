#ifndef CT_UPGRADE_MANAGER_H
#define CT_UPGRADE_MANAGER_H

#include <stdint.h>

typedef struct
{
    uint8_t state;
    uint8_t percent;
    uint8_t last_error;
    uint8_t reserved;
    uint32_t written;
    uint32_t total;
    uint16_t expect_seq;
} CtUpgradeStatus;

#define CT_UPGRADE_STATE_IDLE       0u
#define CT_UPGRADE_STATE_RUNNING    1u
#define CT_UPGRADE_STATE_DONE       2u
#define CT_UPGRADE_STATE_ERROR      3u
#define CT_UPGRADE_STATE_ABORTED    4u

void CtUpgrade_Init(void);
const CtUpgradeStatus *CtUpgrade_GetStatus(void);
int CtUpgrade_Start(uint8_t node);
int CtUpgrade_StartWithAppAddress(uint8_t node, uint8_t app_can_addr);
void CtUpgrade_Task(void);
void CtUpgrade_Abort(void);

#endif
