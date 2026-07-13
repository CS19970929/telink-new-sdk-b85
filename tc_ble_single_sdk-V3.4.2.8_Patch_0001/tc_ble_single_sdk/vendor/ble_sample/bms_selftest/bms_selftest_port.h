#ifndef BMS_SELFTEST_PORT_H_
#define BMS_SELFTEST_PORT_H_

#include "tl_common.h"

void BMS_Port_ForceDangerousOutputsOffEarly(void);
void BMS_Port_ForceDangerousOutputsOff(void);
void BMS_Port_SetApplicationReady(u8 ready);
u8 BMS_Port_IsApplicationReady(void);
u8 BMS_Port_IsOtaWorking(void);
u8 BMS_Port_IsLowPower(void);
u8 BMS_Port_VerifyAfeConfiguration(void);
u32 BMS_Port_GetRunningImageBase(void);
u8 BMS_Port_ReadResetMarker(void);
void BMS_Port_WriteResetMarker(u8 marker);

#endif
