#ifndef BMS_SELFTEST_H_
#define BMS_SELFTEST_H_

#include "bms_selftest_types.h"

void BMS_SelfTest_EarlyBoot(void);
void BMS_SelfTest_BoardInit(void);
u8 BMS_SelfTest_Startup(u8 deep_retention_wakeup);
void BMS_SelfTest_RuntimeInit(void);
void BMS_SelfTest_Process(void);
void BMS_SelfTest_TimerIsrHook(void);
void BMS_SelfTest_ReportHeartbeat(u32 module);
void BMS_SelfTest_ReportAdcSample(u8 channel, u16 millivolts);
void BMS_SelfTest_ReportAfeStatus(u8 communication_ok, u8 configuration_ok);
void BMS_SelfTest_ReportMosFeedback(u8 charge_on, u8 discharge_on, u8 precharge_on);

u8 BMS_SelfTest_IsHealthy(void);
u8 BMS_SelfTest_WatchdogCanFeed(void);
u32 BMS_SelfTest_GetFaultMask(void);
const bms_selftest_diag_t *BMS_SelfTest_GetDiag(void);

#endif
