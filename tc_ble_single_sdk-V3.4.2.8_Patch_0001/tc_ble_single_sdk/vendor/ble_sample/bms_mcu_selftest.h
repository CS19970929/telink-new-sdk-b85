#ifndef BMS_MCU_SELFTEST_H_
#define BMS_MCU_SELFTEST_H_

#include "tl_common.h"

#ifndef BMS_MCU_SELFTEST_ENABLE
#define BMS_MCU_SELFTEST_ENABLE 1
#endif

#ifndef BMS_MCU_SELFTEST_FORCE_FAIL_ITEM
#define BMS_MCU_SELFTEST_FORCE_FAIL_ITEM 0
#endif

typedef enum
{
	BMS_MCU_SELFTEST_OK = 0,
	BMS_MCU_SELFTEST_FAIL_CPU = 1,
	BMS_MCU_SELFTEST_FAIL_PC = 2,
	BMS_MCU_SELFTEST_FAIL_CLOCK = 3,
	BMS_MCU_SELFTEST_FAIL_FLASH = 4,
	BMS_MCU_SELFTEST_FAIL_RAM = 5,
	BMS_MCU_SELFTEST_FAIL_ADC = 6,
	BMS_MCU_SELFTEST_FAIL_IRQ = 7,
} bms_mcu_selftest_result_t;

void bms_mcu_selftest_runtime_check(void);
void bms_mcu_selftest_fail_safe_loop(bms_mcu_selftest_result_t reason);
bms_mcu_selftest_result_t bms_mcu_selftest_last_error(void);

#endif
