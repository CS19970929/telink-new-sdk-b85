#include "app_config.h"
#include "bms_selftest.h"
#include "bms_selftest_port.h"

#ifndef BMS_SELFTEST_ENABLE
#define BMS_SELFTEST_ENABLE                 0
#endif

#ifndef BMS_SELFTEST_STARTUP_ENABLE
#define BMS_SELFTEST_STARTUP_ENABLE         1
#endif

#ifndef BMS_SELFTEST_PERIODIC_ENABLE
#define BMS_SELFTEST_PERIODIC_ENABLE        1
#endif

#ifndef BMS_SELFTEST_CPU_REG_ENABLE
#define BMS_SELFTEST_CPU_REG_ENABLE         1
#endif

#ifndef BMS_SELFTEST_PC_ENABLE
#define BMS_SELFTEST_PC_ENABLE              1
#endif

#ifndef BMS_SELFTEST_CLOCK_ENABLE
#define BMS_SELFTEST_CLOCK_ENABLE           1
#endif

#ifndef BMS_SELFTEST_FLASH_ENABLE
#define BMS_SELFTEST_FLASH_ENABLE           1
#endif

#ifndef BMS_SELFTEST_RAM_ENABLE
#define BMS_SELFTEST_RAM_ENABLE             1
#endif

#ifndef BMS_SELFTEST_ADC_ENABLE
#define BMS_SELFTEST_ADC_ENABLE             1
#endif

#ifndef BMS_SELFTEST_INTERRUPT_ENABLE
#define BMS_SELFTEST_INTERRUPT_ENABLE       1
#endif

#ifndef BMS_SELFTEST_FLASH_ENFORCE
#define BMS_SELFTEST_FLASH_ENFORCE          0
#endif

#ifndef BMS_SELFTEST_CPU_REG_ASM_ENABLE
#define BMS_SELFTEST_CPU_REG_ASM_ENABLE     0
#endif

#ifndef BMS_SELFTEST_PC_ASM_ENABLE
#define BMS_SELFTEST_PC_ASM_ENABLE          0
#endif

#ifndef BMS_SELFTEST_PERIOD_MS
#define BMS_SELFTEST_PERIOD_MS              1000u
#endif

#ifndef BMS_SELFTEST_RAM_WORDS
#define BMS_SELFTEST_RAM_WORDS              64u
#endif

#ifndef BMS_SELFTEST_RAM_PERIOD_WORDS
#define BMS_SELFTEST_RAM_PERIOD_WORDS       8u
#endif

#ifndef BMS_SELFTEST_FLASH_SLICE_BYTES
#define BMS_SELFTEST_FLASH_SLICE_BYTES      128u
#endif

#ifndef BMS_SELFTEST_ADC_STUCK_LIMIT
#define BMS_SELFTEST_ADC_STUCK_LIMIT        30u
#endif

#ifndef BMS_SELFTEST_INTERRUPT_STALE_LIMIT
#define BMS_SELFTEST_INTERRUPT_STALE_LIMIT  3u
#endif

#define BMS_SELFTEST_ITEM_BIT(item)         (1u << (unsigned int)(item))
#define BMS_SELFTEST_PC_STARTUP_EXPECTED    0x86be281du
#define BMS_SELFTEST_PC_REQUIRED_MASK       (BMS_SELFTEST_PC_CHECKPOINT_LOOP_ENTRY | BMS_SELFTEST_PC_CHECKPOINT_LOOP_END)
#define BMS_SELFTEST_ADC_MAX_MV             3300u

volatile unsigned int g_bms_selftest_irq_counter;

#if BMS_SELFTEST_ENABLE
static bms_selftest_status_t g_bms_selftest_status;
static volatile unsigned int g_bms_selftest_pc_mask;
static unsigned int g_bms_selftest_ram_area[BMS_SELFTEST_RAM_WORDS];
static unsigned int g_bms_selftest_ram_period_index;
static unsigned int g_bms_selftest_flash_offset;
static unsigned int g_bms_selftest_flash_checksum;
static unsigned int g_bms_selftest_last_irq_counter;
static unsigned char g_bms_selftest_irq_stale_count;
static unsigned int g_bms_selftest_adc_last0;
static unsigned int g_bms_selftest_adc_last1;
static unsigned int g_bms_selftest_adc_last2;
static unsigned char g_bms_selftest_adc_observed;
static unsigned char g_bms_selftest_adc_same_count;

static bms_selftest_error_t BMS_SelfTest_ItemToError(bms_selftest_item_t item)
{
	switch (item) {
	case BMS_SELFTEST_ITEM_CPU_REG:
		return BMS_SELFTEST_ERROR_CPU_REG;
	case BMS_SELFTEST_ITEM_PC:
		return BMS_SELFTEST_ERROR_PC;
	case BMS_SELFTEST_ITEM_CLOCK:
		return BMS_SELFTEST_ERROR_CLOCK;
	case BMS_SELFTEST_ITEM_FLASH:
		return BMS_SELFTEST_ERROR_FLASH;
	case BMS_SELFTEST_ITEM_RAM:
		return BMS_SELFTEST_ERROR_RAM;
	case BMS_SELFTEST_ITEM_ADC:
		return BMS_SELFTEST_ERROR_ADC;
	case BMS_SELFTEST_ITEM_INTERRUPT:
		return BMS_SELFTEST_ERROR_INTERRUPT;
	default:
		return BMS_SELFTEST_ERROR_NONE;
	}
}

static void BMS_SelfTest_ResetResultArray(bms_selftest_result_t *result)
{
	unsigned int i;

	for (i = 0u; i < (unsigned int)BMS_SELFTEST_ITEM_MAX; i++) {
		result[i] = BMS_SELFTEST_RESULT_NOT_RUN;
	}
}

static void BMS_SelfTest_SetResult(bms_selftest_result_t *result,
								   unsigned int *fail_bitmap,
								   bms_selftest_item_t item,
								   bms_selftest_result_t value)
{
	unsigned int bit = BMS_SELFTEST_ITEM_BIT(item);

	result[item] = value;
	*fail_bitmap &= ~bit;

	if (value == BMS_SELFTEST_RESULT_FAIL) {
		*fail_bitmap |= bit;
		g_bms_selftest_status.last_error = BMS_SelfTest_ItemToError(item);
	}
}

static unsigned char BMS_SelfTest_CpuAluBasic(void)
{
	volatile unsigned int a = 0x55aa55aau;
	volatile unsigned int b = 0x0f0f0f0fu;
	volatile unsigned int c = 0u;

	c = a ^ b;
	if (c != 0x5aa55aa5u) {
		return 0u;
	}

	c = (a + 0x12345678u) - 0x12345678u;
	if (c != a) {
		return 0u;
	}

	c = ((a << 3) >> 3) & 0x1fffffffu;
	if (c != (a & 0x1fffffffu)) {
		return 0u;
	}

	return 1u;
}

static bms_selftest_result_t BMS_SelfTest_CpuRegTest(void)
{
#if BMS_SELFTEST_CPU_REG_ASM_ENABLE
	/* TODO(tc32): add hand-written tc32 register save/restore test after ISA/ABI review. */
	return BMS_SELFTEST_RESULT_UNSUPPORTED;
#else
	if (!BMS_SelfTest_CpuAluBasic()) {
		return BMS_SELFTEST_RESULT_FAIL;
	}

	return BMS_SELFTEST_RESULT_UNSUPPORTED;
#endif
}

static bms_selftest_result_t BMS_SelfTest_PcStartupTest(void)
{
	volatile unsigned int signature = 0x13579bdfu;

#if BMS_SELFTEST_PC_ASM_ENABLE
	/* TODO(tc32): add PC/link-register coverage after confirming tc32 branch semantics. */
	return BMS_SELFTEST_RESULT_UNSUPPORTED;
#else
	signature ^= 0x2468ace0u;
	signature += 0x10203040u;
	signature = (signature << 5) | (signature >> 27);
	signature ^= 0x6d52c7f5u;

	if (signature != BMS_SELFTEST_PC_STARTUP_EXPECTED) {
		return BMS_SELFTEST_RESULT_FAIL;
	}

	return BMS_SELFTEST_RESULT_OK;
#endif
}

static bms_selftest_result_t BMS_SelfTest_PcPeriodicTest(void)
{
	unsigned int mask = g_bms_selftest_pc_mask;

	g_bms_selftest_pc_mask = 0u;

	if ((mask & BMS_SELFTEST_PC_REQUIRED_MASK) != BMS_SELFTEST_PC_REQUIRED_MASK) {
		return BMS_SELFTEST_RESULT_FAIL;
	}

	return BMS_SELFTEST_RESULT_OK;
}

static bms_selftest_result_t BMS_SelfTest_ClockTest(void)
{
	unsigned int start_tick;
	unsigned int end_tick;

	if (BMS_SelfTest_PortInLowPower()) {
		return BMS_SELFTEST_RESULT_OK;
	}

	start_tick = BMS_SelfTest_PortGetRawTick();
	BMS_SelfTest_PortDelayUs(20u);
	end_tick = BMS_SelfTest_PortGetRawTick();

	if (end_tick == start_tick) {
		return BMS_SELFTEST_RESULT_FAIL;
	}

	return BMS_SELFTEST_RESULT_OK;
}

static bms_selftest_result_t BMS_SelfTest_FlashStartupTest(void)
{
	unsigned char crc_ok;

	if (BMS_SelfTest_PortGetFirmwareSize() == 0u) {
		return BMS_SELFTEST_RESULT_UNSUPPORTED;
	}

	g_bms_selftest_status.flash_fw_crc_checked = 1u;
	crc_ok = BMS_SelfTest_PortFlashFwCheck();
	g_bms_selftest_status.flash_fw_crc_ok = crc_ok;

	if (!crc_ok) {
#if BMS_SELFTEST_FLASH_ENFORCE
		return BMS_SELFTEST_RESULT_FAIL;
#else
		return BMS_SELFTEST_RESULT_OK;
#endif
	}

	return BMS_SELFTEST_RESULT_OK;
}

static unsigned int BMS_SelfTest_ChecksumStep(unsigned int checksum, const unsigned char *buf, unsigned int len)
{
	unsigned int i;

	for (i = 0u; i < len; i++) {
		checksum = (checksum << 5) | (checksum >> 27);
		checksum ^= (unsigned int)buf[i] + 0x9e3779b9u + i;
	}

	return checksum;
}

static bms_selftest_result_t BMS_SelfTest_FlashPeriodicTest(void)
{
	unsigned char buf[BMS_SELFTEST_FLASH_SLICE_BYTES];
	unsigned int fw_base = BMS_SelfTest_PortGetActiveFirmwareBase();
	unsigned int fw_size = BMS_SelfTest_PortGetFirmwareSize();
	unsigned int check_size;
	unsigned int remain;
	unsigned int len;

	if (fw_size <= 4u) {
		return BMS_SELFTEST_RESULT_UNSUPPORTED;
	}

	check_size = fw_size - 4u;
	if (g_bms_selftest_flash_offset >= check_size) {
		g_bms_selftest_flash_offset = 0u;
		g_bms_selftest_flash_checksum = 0x811c9dc5u;
	}

	remain = check_size - g_bms_selftest_flash_offset;
	len = (remain > BMS_SELFTEST_FLASH_SLICE_BYTES) ? BMS_SELFTEST_FLASH_SLICE_BYTES : remain;

	BMS_SelfTest_PortFlashRead(fw_base + g_bms_selftest_flash_offset, buf, len);
	g_bms_selftest_flash_checksum = BMS_SelfTest_ChecksumStep(g_bms_selftest_flash_checksum, buf, len);
	g_bms_selftest_flash_offset += len;

	if (g_bms_selftest_flash_offset >= check_size) {
		g_bms_selftest_status.flash_last_checksum = g_bms_selftest_flash_checksum;
		g_bms_selftest_flash_offset = 0u;
		g_bms_selftest_flash_checksum = 0x811c9dc5u;
	}

	return BMS_SELFTEST_RESULT_OK;
}

static unsigned char BMS_SelfTest_RamWordTest(unsigned int index)
{
	volatile unsigned int *addr = &g_bms_selftest_ram_area[index];
	unsigned int backup = *addr;

	*addr = 0x00000000u;
	if (*addr != 0x00000000u) {
		*addr = backup;
		return 0u;
	}

	*addr = 0xffffffffu;
	if (*addr != 0xffffffffu) {
		*addr = backup;
		return 0u;
	}

	*addr = 0x55555555u;
	if (*addr != 0x55555555u) {
		*addr = backup;
		return 0u;
	}

	*addr = 0xaaaaaaaau;
	if (*addr != 0xaaaaaaaau) {
		*addr = backup;
		return 0u;
	}

	*addr = backup;
	return 1u;
}

static bms_selftest_result_t BMS_SelfTest_RamStartupTest(void)
{
	unsigned int i;

	for (i = 0u; i < BMS_SELFTEST_RAM_WORDS; i++) {
		if (!BMS_SelfTest_RamWordTest(i)) {
			return BMS_SELFTEST_RESULT_FAIL;
		}
	}

	return BMS_SELFTEST_RESULT_OK;
}

static bms_selftest_result_t BMS_SelfTest_RamPeriodicTest(void)
{
	unsigned int i;

	for (i = 0u; i < BMS_SELFTEST_RAM_PERIOD_WORDS; i++) {
		if (!BMS_SelfTest_RamWordTest(g_bms_selftest_ram_period_index)) {
			return BMS_SELFTEST_RESULT_FAIL;
		}

		g_bms_selftest_ram_period_index++;
		if (g_bms_selftest_ram_period_index >= BMS_SELFTEST_RAM_WORDS) {
			g_bms_selftest_ram_period_index = 0u;
		}
	}

	return BMS_SELFTEST_RESULT_OK;
}

static bms_selftest_result_t BMS_SelfTest_AdcStartupTest(void)
{
	unsigned int sample_mv = 0u;

	if (!BMS_SelfTest_PortAdcSample(&sample_mv)) {
		return BMS_SELFTEST_RESULT_UNSUPPORTED;
	}

	if (sample_mv > BMS_SELFTEST_ADC_MAX_MV) {
		return BMS_SELFTEST_RESULT_FAIL;
	}

	return BMS_SELFTEST_RESULT_OK;
}

static bms_selftest_result_t BMS_SelfTest_AdcPeriodicTest(void)
{
	if (BMS_SelfTest_PortInLowPower()) {
		return BMS_SELFTEST_RESULT_OK;
	}

	if (!g_bms_selftest_adc_observed) {
		return BMS_SELFTEST_RESULT_UNSUPPORTED;
	}

	if ((g_bms_selftest_adc_last0 > BMS_SELFTEST_ADC_MAX_MV) ||
		(g_bms_selftest_adc_last1 > BMS_SELFTEST_ADC_MAX_MV) ||
		(g_bms_selftest_adc_last2 > BMS_SELFTEST_ADC_MAX_MV)) {
		return BMS_SELFTEST_RESULT_FAIL;
	}

	if (g_bms_selftest_adc_same_count >= BMS_SELFTEST_ADC_STUCK_LIMIT) {
		return BMS_SELFTEST_RESULT_FAIL;
	}

	return BMS_SELFTEST_RESULT_OK;
}

static bms_selftest_result_t BMS_SelfTest_InterruptStartupTest(void)
{
	unsigned int irq_counter = g_bms_selftest_irq_counter;

	BMS_SelfTest_PortDelayUs(1200u);

	if (g_bms_selftest_irq_counter == irq_counter) {
		return BMS_SELFTEST_RESULT_UNSUPPORTED;
	}

	g_bms_selftest_last_irq_counter = g_bms_selftest_irq_counter;
	return BMS_SELFTEST_RESULT_OK;
}

static bms_selftest_result_t BMS_SelfTest_InterruptPeriodicTest(void)
{
	unsigned int irq_counter = g_bms_selftest_irq_counter;

	if (BMS_SelfTest_PortInLowPower()) {
		g_bms_selftest_last_irq_counter = irq_counter;
		g_bms_selftest_irq_stale_count = 0u;
		return BMS_SELFTEST_RESULT_OK;
	}

	if (irq_counter == g_bms_selftest_last_irq_counter) {
		if (g_bms_selftest_irq_stale_count < 0xffu) {
			g_bms_selftest_irq_stale_count++;
		}
	} else {
		g_bms_selftest_irq_stale_count = 0u;
		g_bms_selftest_last_irq_counter = irq_counter;
	}

	if (g_bms_selftest_irq_stale_count >= BMS_SELFTEST_INTERRUPT_STALE_LIMIT) {
		return BMS_SELFTEST_RESULT_FAIL;
	}

	return BMS_SELFTEST_RESULT_OK;
}

static bms_selftest_result_t BMS_SelfTest_RunStartupItem(bms_selftest_item_t item)
{
	switch (item) {
	case BMS_SELFTEST_ITEM_CPU_REG:
#if BMS_SELFTEST_CPU_REG_ENABLE
		return BMS_SelfTest_CpuRegTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_PC:
#if BMS_SELFTEST_PC_ENABLE
		return BMS_SelfTest_PcStartupTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_CLOCK:
#if BMS_SELFTEST_CLOCK_ENABLE
		return BMS_SelfTest_ClockTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_FLASH:
#if BMS_SELFTEST_FLASH_ENABLE
		return BMS_SelfTest_FlashStartupTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_RAM:
#if BMS_SELFTEST_RAM_ENABLE
		return BMS_SelfTest_RamStartupTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_ADC:
#if BMS_SELFTEST_ADC_ENABLE
		return BMS_SelfTest_AdcStartupTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_INTERRUPT:
#if BMS_SELFTEST_INTERRUPT_ENABLE
		return BMS_SelfTest_InterruptStartupTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	default:
		return BMS_SELFTEST_RESULT_NOT_RUN;
	}
}

static bms_selftest_result_t BMS_SelfTest_RunPeriodicItem(bms_selftest_item_t item)
{
	switch (item) {
	case BMS_SELFTEST_ITEM_CPU_REG:
#if BMS_SELFTEST_CPU_REG_ENABLE
		return BMS_SelfTest_CpuRegTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_PC:
#if BMS_SELFTEST_PC_ENABLE
		return BMS_SelfTest_PcPeriodicTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_CLOCK:
#if BMS_SELFTEST_CLOCK_ENABLE
		return BMS_SelfTest_ClockTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_FLASH:
#if BMS_SELFTEST_FLASH_ENABLE
		return BMS_SelfTest_FlashPeriodicTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_RAM:
#if BMS_SELFTEST_RAM_ENABLE
		return BMS_SelfTest_RamPeriodicTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_ADC:
#if BMS_SELFTEST_ADC_ENABLE
		return BMS_SelfTest_AdcPeriodicTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	case BMS_SELFTEST_ITEM_INTERRUPT:
#if BMS_SELFTEST_INTERRUPT_ENABLE
		return BMS_SelfTest_InterruptPeriodicTest();
#else
		return BMS_SELFTEST_RESULT_NOT_RUN;
#endif
	default:
		return BMS_SELFTEST_RESULT_NOT_RUN;
	}
}
#endif

void BMS_SelfTest_Init(void)
{
#if BMS_SELFTEST_ENABLE
	g_bms_selftest_status.startup_done = 0u;
	g_bms_selftest_status.periodic_done = 0u;
	g_bms_selftest_status.startup_fail_bitmap = 0u;
	g_bms_selftest_status.periodic_fail_bitmap = 0u;
	g_bms_selftest_status.last_test_tick_ms = BMS_SelfTest_PortGetTickMs();
	g_bms_selftest_status.test_counter = 0u;
	g_bms_selftest_status.flash_last_checksum = 0u;
	g_bms_selftest_status.flash_fw_crc_checked = 0u;
	g_bms_selftest_status.flash_fw_crc_ok = 0u;
	g_bms_selftest_status.last_error = BMS_SELFTEST_ERROR_NONE;
	g_bms_selftest_pc_mask = 0u;
	g_bms_selftest_ram_period_index = 0u;
	g_bms_selftest_flash_offset = 0u;
	g_bms_selftest_flash_checksum = 0x811c9dc5u;
	g_bms_selftest_last_irq_counter = g_bms_selftest_irq_counter;
	g_bms_selftest_irq_stale_count = 0u;
	g_bms_selftest_adc_observed = 0u;
	g_bms_selftest_adc_same_count = 0u;
	BMS_SelfTest_ResetResultArray(g_bms_selftest_status.startup_result);
	BMS_SelfTest_ResetResultArray(g_bms_selftest_status.periodic_result);
#endif
}

void BMS_SelfTest_Startup(void)
{
#if BMS_SELFTEST_ENABLE && BMS_SELFTEST_STARTUP_ENABLE
	unsigned int i;

	g_bms_selftest_status.startup_fail_bitmap = 0u;
	BMS_SelfTest_ResetResultArray(g_bms_selftest_status.startup_result);

	for (i = 0u; i < (unsigned int)BMS_SELFTEST_ITEM_MAX; i++) {
		BMS_SelfTest_SetResult(g_bms_selftest_status.startup_result,
							   &g_bms_selftest_status.startup_fail_bitmap,
							   (bms_selftest_item_t)i,
							   BMS_SelfTest_RunStartupItem((bms_selftest_item_t)i));
	}

	g_bms_selftest_status.startup_done = 1u;
#endif
}

void BMS_SelfTest_PeriodicTask(void)
{
#if BMS_SELFTEST_ENABLE && BMS_SELFTEST_PERIODIC_ENABLE
	unsigned int now_ms = BMS_SelfTest_PortGetTickMs();
	unsigned int i;

	if ((now_ms - g_bms_selftest_status.last_test_tick_ms) < BMS_SELFTEST_PERIOD_MS) {
		return;
	}

	g_bms_selftest_status.last_test_tick_ms = now_ms;
	g_bms_selftest_status.periodic_fail_bitmap = 0u;
	BMS_SelfTest_ResetResultArray(g_bms_selftest_status.periodic_result);

	for (i = 0u; i < (unsigned int)BMS_SELFTEST_ITEM_MAX; i++) {
		BMS_SelfTest_SetResult(g_bms_selftest_status.periodic_result,
							   &g_bms_selftest_status.periodic_fail_bitmap,
							   (bms_selftest_item_t)i,
							   BMS_SelfTest_RunPeriodicItem((bms_selftest_item_t)i));
	}

	g_bms_selftest_status.periodic_done = 1u;
	g_bms_selftest_status.test_counter++;
#endif
}

void BMS_SelfTest_IrqHook(void)
{
#if BMS_SELFTEST_ENABLE && BMS_SELFTEST_INTERRUPT_ENABLE
	g_bms_selftest_irq_counter++;
#endif
}

void BMS_SelfTest_PcCheckpoint(unsigned int checkpoint)
{
#if BMS_SELFTEST_ENABLE && BMS_SELFTEST_PC_ENABLE
	g_bms_selftest_pc_mask |= checkpoint;
#else
	(void)checkpoint;
#endif
}

void BMS_SelfTest_AdcObserve(unsigned int sample0_mv, unsigned int sample1_mv, unsigned int sample2_mv)
{
#if BMS_SELFTEST_ENABLE && BMS_SELFTEST_ADC_ENABLE
	if (g_bms_selftest_adc_observed &&
		(sample0_mv == g_bms_selftest_adc_last0) &&
		(sample1_mv == g_bms_selftest_adc_last1) &&
		(sample2_mv == g_bms_selftest_adc_last2)) {
		if (g_bms_selftest_adc_same_count < 0xffu) {
			g_bms_selftest_adc_same_count++;
		}
	} else {
		g_bms_selftest_adc_same_count = 0u;
	}

	g_bms_selftest_adc_last0 = sample0_mv;
	g_bms_selftest_adc_last1 = sample1_mv;
	g_bms_selftest_adc_last2 = sample2_mv;
	g_bms_selftest_adc_observed = 1u;
#else
	(void)sample0_mv;
	(void)sample1_mv;
	(void)sample2_mv;
#endif
}

const bms_selftest_status_t *BMS_SelfTest_GetStatus(void)
{
#if BMS_SELFTEST_ENABLE
	return &g_bms_selftest_status;
#else
	return 0;
#endif
}

unsigned char BMS_SelfTest_IsHealthy(void)
{
#if BMS_SELFTEST_ENABLE
	return ((g_bms_selftest_status.startup_fail_bitmap == 0u) &&
			(g_bms_selftest_status.periodic_fail_bitmap == 0u)) ? 1u : 0u;
#else
	return 1u;
#endif
}
