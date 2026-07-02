#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"
#include "bms_mcu_selftest.h"

#include "conf.h"
#include "sh367309_datadeal.h"

#ifndef BMS_MCU_SELFTEST_ALARM_PIN
#define BMS_MCU_SELFTEST_ALARM_PIN ADC_NTC_PIN
#endif

#define BMS_FLASH_MAGIC_ADDR ((volatile const u32 *)0x00000008)
#define BMS_FLASH_SIZE_ADDR  ((volatile const u32 *)0x00000018)
#define BMS_FLASH_MAGIC_WORD 0x544c4e4bu
#define BMS_FLASH_SIZE_MIN   0x1000u
#define BMS_FLASH_SIZE_MAX   0x80000u

extern int bms_mcu_selftest_cpu_regs_asm(void);
extern int bms_mcu_selftest_pc_asm(void);

static volatile u32 g_bms_mcu_selftest_ram_probe;
static volatile bms_mcu_selftest_result_t g_bms_mcu_selftest_last_error = BMS_MCU_SELFTEST_OK;

static inline int bms_mcu_selftest_force_fail(bms_mcu_selftest_result_t item)
{
	return (BMS_MCU_SELFTEST_FORCE_FAIL_ITEM == (int)item);
}

static _attribute_ram_code_ void bms_mcu_selftest_gpio_output_low(GPIO_PinTypeDef pin)
{
	u8 bit = pin & 0xff;

	BM_SET(reg_gpio_func(pin), bit);
	BM_CLR(reg_gpio_oen(pin), bit);
	BM_CLR(reg_gpio_out(pin), bit);
}

static _attribute_ram_code_ void bms_mcu_selftest_alarm_set(u8 level)
{
	u8 bit = BMS_MCU_SELFTEST_ALARM_PIN & 0xff;

	BM_SET(reg_gpio_func(BMS_MCU_SELFTEST_ALARM_PIN), bit);
	BM_CLR(reg_gpio_oen(BMS_MCU_SELFTEST_ALARM_PIN), bit);
	if (level)
	{
		BM_SET(reg_gpio_out(BMS_MCU_SELFTEST_ALARM_PIN), bit);
	}
	else
	{
		BM_CLR(reg_gpio_out(BMS_MCU_SELFTEST_ALARM_PIN), bit);
	}
}

static _attribute_ram_code_ void bms_mcu_selftest_force_outputs_off(void)
{
	bms_mcu_selftest_gpio_output_low(MCC_C_PIN);
	bms_mcu_selftest_gpio_output_low(AFE_CTL_PIN);
	bms_mcu_selftest_gpio_output_low(ADC_BUSEN_PIN);
	bms_mcu_selftest_gpio_output_low(ADC_EN_PIN);
#ifdef _UL_RENZHENG_ENABLE_
	bms_mcu_selftest_gpio_output_low(RF_EN_PIN);
	bms_mcu_selftest_gpio_output_low(AFE1_PRO_EN_PIN);
#endif
}

static int bms_mcu_selftest_cpu_ok(void)
{
	return bms_mcu_selftest_cpu_regs_asm() != 0;
}

static int bms_mcu_selftest_pc_ok(void)
{
	return bms_mcu_selftest_pc_asm() != 0;
}

static int bms_mcu_selftest_clock_ok(void)
{
	u32 t0;
	u32 t1;
	volatile u32 i;

	if ((u8)clock_get_system_clk() != (u8)SYS_CLK_TYPE)
	{
		return 0;
	}

	t0 = clock_time();
	for (i = 0; i < 64u; ++i)
	{
		asm("tnop");
	}
	t1 = clock_time();
	return t1 != t0;
}

static int bms_mcu_selftest_flash_ok(void)
{
	u32 magic = *BMS_FLASH_MAGIC_ADDR;
	u32 image_size = *BMS_FLASH_SIZE_ADDR;

	if (magic != BMS_FLASH_MAGIC_WORD)
	{
		return 0;
	}
	if ((image_size < BMS_FLASH_SIZE_MIN) || (image_size > BMS_FLASH_SIZE_MAX))
	{
		return 0;
	}
	return 1;
}

static int bms_mcu_selftest_ram_ok(void)
{
	g_bms_mcu_selftest_ram_probe = 0xaaaaaaaau;
	if (g_bms_mcu_selftest_ram_probe != 0xaaaaaaaau)
	{
		return 0;
	}
	g_bms_mcu_selftest_ram_probe = 0x55555555u;
	if (g_bms_mcu_selftest_ram_probe != 0x55555555u)
	{
		return 0;
	}
	g_bms_mcu_selftest_ram_probe = 0u;
	return 1;
}

static int bms_mcu_selftest_adc_ok(void)
{
	u8 pga_ctrl;
	u8 adc_clk;
	u8 busen_bit = ADC_BUSEN_PIN & 0xff;
	u8 adc_en_bit = ADC_EN_PIN & 0xff;

	if ((reg_gpio_out(ADC_BUSEN_PIN) & busen_bit) == 0)
	{
		return 0;
	}
	if ((reg_gpio_out(ADC_EN_PIN) & adc_en_bit) == 0)
	{
		return 0;
	}

	pga_ctrl = analog_read(areg_adc_pga_ctrl);
	if (pga_ctrl & FLD_SAR_ADC_POWER_DOWN)
	{
		return 0;
	}

	adc_clk = analog_read(areg_clk_setting);
	return (adc_clk & FLD_CLK_24M_TO_SAR_EN) != 0;
}

static int bms_mcu_selftest_irq_ok(void)
{
	if (reg_irq_en == 0)
	{
		return 0;
	}
	if ((reg_irq_mask & FLD_IRQ_TMR0_EN) == 0)
	{
		return 0;
	}
	if ((reg_tmr_ctrl & FLD_TMR0_EN) == 0)
	{
		return 0;
	}
	return 1;
}

static void bms_mcu_selftest_fail_if(int ok, bms_mcu_selftest_result_t reason)
{
	if (!ok || bms_mcu_selftest_force_fail(reason))
	{
		bms_mcu_selftest_fail_safe_loop(reason);
	}
}

void bms_mcu_selftest_runtime_check(void)
{
#if BMS_MCU_SELFTEST_ENABLE
	if (sys_time.low_power_mode)
	{
		return;
	}

	bms_mcu_selftest_fail_if(bms_mcu_selftest_cpu_ok(), BMS_MCU_SELFTEST_FAIL_CPU);
	bms_mcu_selftest_fail_if(bms_mcu_selftest_pc_ok(), BMS_MCU_SELFTEST_FAIL_PC);
	bms_mcu_selftest_fail_if(bms_mcu_selftest_clock_ok(), BMS_MCU_SELFTEST_FAIL_CLOCK);
	bms_mcu_selftest_fail_if(bms_mcu_selftest_flash_ok(), BMS_MCU_SELFTEST_FAIL_FLASH);
	bms_mcu_selftest_fail_if(bms_mcu_selftest_ram_ok(), BMS_MCU_SELFTEST_FAIL_RAM);
	bms_mcu_selftest_fail_if(bms_mcu_selftest_adc_ok(), BMS_MCU_SELFTEST_FAIL_ADC);
	bms_mcu_selftest_fail_if(bms_mcu_selftest_irq_ok(), BMS_MCU_SELFTEST_FAIL_IRQ);
#endif
}

_attribute_ram_code_ void bms_mcu_selftest_fail_safe_loop(bms_mcu_selftest_result_t reason)
{
	volatile u32 delay;

	g_bms_mcu_selftest_last_error = reason;
	irq_disable();
	bms_mcu_selftest_force_outputs_off();

	while (1)
	{
#if (MODULE_WATCHDOG_ENABLE)
		wd_clear();
#endif
		bms_mcu_selftest_alarm_set(1);
		for (delay = 0; delay < 0x2000u; ++delay)
		{
			asm("tnop");
		}
		bms_mcu_selftest_alarm_set(0);
		for (delay = 0; delay < 0x2000u; ++delay)
		{
			asm("tnop");
		}
	}
}

bms_mcu_selftest_result_t bms_mcu_selftest_last_error(void)
{
	return g_bms_mcu_selftest_last_error;
}
