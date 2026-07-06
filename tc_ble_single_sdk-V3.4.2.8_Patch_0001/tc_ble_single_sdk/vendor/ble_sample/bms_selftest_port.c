#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"
#include "conf.h"
#include "bms_selftest_port.h"

extern unsigned int adc_read_gpio_mv(GPIO_PinTypeDef pin);
extern unsigned int ota_program_bootAddr;
extern unsigned int ota_program_offset;
extern bool flash_fw_check(unsigned int crc_init_value);

#ifndef BMS_SELFTEST_FW_SLOT_MAX_SIZE
#define BMS_SELFTEST_FW_SLOT_MAX_SIZE       0x20000u
#endif

unsigned int BMS_SelfTest_PortGetRawTick(void)
{
	return clock_time();
}

unsigned int BMS_SelfTest_PortGetTickMs(void)
{
	return clock_time() / (CLOCK_SYS_CLOCK_HZ / 1000u);
}

unsigned char BMS_SelfTest_PortRawTickElapsed(unsigned int start_tick, unsigned int us)
{
	return clock_time_exceed(start_tick, us) ? 1u : 0u;
}

void BMS_SelfTest_PortDelayUs(unsigned int us)
{
	unsigned int start_tick = BMS_SelfTest_PortGetRawTick();

	while (!BMS_SelfTest_PortRawTickElapsed(start_tick, us)) {
	}
}

unsigned int BMS_SelfTest_PortGetActiveFirmwareBase(void)
{
	return ota_program_offset ? 0u : ota_program_bootAddr;
}

unsigned int BMS_SelfTest_PortGetFirmwareSize(void)
{
	unsigned int fw_size = 0u;
	unsigned int fw_base = BMS_SelfTest_PortGetActiveFirmwareBase();

	flash_read_page(fw_base + 0x18u, sizeof(fw_size), (unsigned char *)&fw_size);

	if ((fw_size < 0x100u) || (fw_size > BMS_SELFTEST_FW_SLOT_MAX_SIZE)) {
		return 0u;
	}

	return fw_size;
}

void BMS_SelfTest_PortFlashRead(unsigned int addr, unsigned char *buf, unsigned int len)
{
	flash_read_page(addr, len, buf);
}

unsigned char BMS_SelfTest_PortFlashFwCheck(void)
{
	return flash_fw_check(0xffffffffu) ? 0u : 1u;
}

unsigned char BMS_SelfTest_PortAdcSample(unsigned int *sample_mv)
{
	if (sample_mv == 0) {
		return 0u;
	}

	*sample_mv = adc_read_gpio_mv(ADC_VBUS_PIN);
	return 1u;
}

unsigned char BMS_SelfTest_PortInLowPower(void)
{
	return sys_time.low_power_mode ? 1u : 0u;
}
