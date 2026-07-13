#ifndef BMS_FAILSAFE_H_
#define BMS_FAILSAFE_H_

#include "bms_selftest_types.h"

void BMS_FailSafe_Init(void);
void BMS_FailSafe_Enter(bms_fault_reason_t reason, u8 fatal_mcu_fault);
void BMS_FailSafe_Process(void);
u8 BMS_FailSafe_IsActive(void);
u8 BMS_FailSafe_AllowOutputs(void);
u8 BMS_FailSafe_WatchdogCanFeed(void);
u16 BMS_FailSafe_GetReason(void);
u16 BMS_FailSafe_GetLastFatalReason(void);
u16 BMS_FailSafe_GetFlags(void);

#endif
