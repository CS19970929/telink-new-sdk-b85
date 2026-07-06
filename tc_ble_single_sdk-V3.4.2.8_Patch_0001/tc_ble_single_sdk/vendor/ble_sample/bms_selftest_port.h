#ifndef BMS_SELFTEST_PORT_H_
#define BMS_SELFTEST_PORT_H_

#ifdef __cplusplus
extern "C" {
#endif

unsigned int BMS_SelfTest_PortGetRawTick(void);
unsigned int BMS_SelfTest_PortGetTickMs(void);
unsigned char BMS_SelfTest_PortRawTickElapsed(unsigned int start_tick, unsigned int us);
void BMS_SelfTest_PortDelayUs(unsigned int us);
unsigned int BMS_SelfTest_PortGetActiveFirmwareBase(void);
unsigned int BMS_SelfTest_PortGetFirmwareSize(void);
void BMS_SelfTest_PortFlashRead(unsigned int addr, unsigned char *buf, unsigned int len);
unsigned char BMS_SelfTest_PortFlashFwCheck(void);
unsigned char BMS_SelfTest_PortAdcSample(unsigned int *sample_mv);
unsigned char BMS_SelfTest_PortInLowPower(void);

#ifdef __cplusplus
}
#endif

#endif
