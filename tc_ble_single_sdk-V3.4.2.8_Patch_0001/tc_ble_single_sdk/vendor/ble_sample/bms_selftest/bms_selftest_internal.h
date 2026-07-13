#ifndef BMS_SELFTEST_INTERNAL_H_
#define BMS_SELFTEST_INTERNAL_H_

#include "bms_selftest.h"
#include "bms_selftest_cfg.h"

void BMS_SelfTest_RecordFailure(bms_selftest_item_t item, bms_fault_reason_t reason);
u8 BMS_SelfTest_CpuStartup(void);
u8 BMS_SelfTest_CpuRuntime(void);
u8 BMS_SelfTest_ControlFlowStartup(void);
u8 BMS_SelfTest_ControlFlowRuntime(void);
u8 BMS_SelfTest_FlashStartup(void);
u8 BMS_SelfTest_FlashRuntimeStep(void);
void BMS_SelfTest_FlashRuntimeReset(void);
u8 BMS_SelfTest_RamStartup(void);
u8 BMS_SelfTest_RamRuntimeStep(void);
u8 BMS_SelfTest_StackStartup(void);
u8 BMS_SelfTest_StackRuntime(void);
u8 BMS_SelfTest_ClockStartup(void);
u8 BMS_SelfTest_ClockRuntime(void);
u8 BMS_SelfTest_InterruptRuntime(void);
u32 BMS_SelfTest_InterruptCount(void);
void BMS_SelfTest_ApplicationProcess(void);

void BMS_SelfTest_SetFlashDiag(u32 expected, u32 actual, u32 progress, u32 total);
void BMS_SelfTest_SetIrqDiag(u32 count, u32 delta);
void BMS_SelfTest_SetStackHighWater(u32 bytes);

#endif
