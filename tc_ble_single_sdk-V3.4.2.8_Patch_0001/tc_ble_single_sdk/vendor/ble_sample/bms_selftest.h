#ifndef BMS_SELFTEST_H_
#define BMS_SELFTEST_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	BMS_SELFTEST_RESULT_NOT_RUN = 0,
	BMS_SELFTEST_RESULT_OK,
	BMS_SELFTEST_RESULT_FAIL,
	BMS_SELFTEST_RESULT_UNSUPPORTED,
} bms_selftest_result_t;

typedef enum {
	BMS_SELFTEST_ITEM_CPU_REG = 0,
	BMS_SELFTEST_ITEM_PC,
	BMS_SELFTEST_ITEM_CLOCK,
	BMS_SELFTEST_ITEM_FLASH,
	BMS_SELFTEST_ITEM_RAM,
	BMS_SELFTEST_ITEM_ADC,
	BMS_SELFTEST_ITEM_INTERRUPT,
	BMS_SELFTEST_ITEM_MAX,
} bms_selftest_item_t;

typedef enum {
	BMS_SELFTEST_ERROR_NONE = 0,
	BMS_SELFTEST_ERROR_CPU_REG,
	BMS_SELFTEST_ERROR_PC,
	BMS_SELFTEST_ERROR_CLOCK,
	BMS_SELFTEST_ERROR_FLASH,
	BMS_SELFTEST_ERROR_RAM,
	BMS_SELFTEST_ERROR_ADC,
	BMS_SELFTEST_ERROR_INTERRUPT,
} bms_selftest_error_t;

typedef struct {
	unsigned char startup_done;
	unsigned char periodic_done;
	bms_selftest_result_t startup_result[BMS_SELFTEST_ITEM_MAX];
	bms_selftest_result_t periodic_result[BMS_SELFTEST_ITEM_MAX];
	unsigned int startup_fail_bitmap;
	unsigned int periodic_fail_bitmap;
	unsigned int last_test_tick_ms;
	unsigned int test_counter;
	unsigned int flash_last_checksum;
	unsigned char flash_fw_crc_checked;
	unsigned char flash_fw_crc_ok;
	bms_selftest_error_t last_error;
} bms_selftest_status_t;

#define BMS_SELFTEST_PC_CHECKPOINT_LOOP_ENTRY      0x00000001u
#define BMS_SELFTEST_PC_CHECKPOINT_BMS_1S         0x00000002u
#define BMS_SELFTEST_PC_CHECKPOINT_LOOP_END       0x00000004u

extern volatile unsigned int g_bms_selftest_irq_counter;

void BMS_SelfTest_Init(void);
void BMS_SelfTest_Startup(void);
void BMS_SelfTest_PeriodicTask(void);
void BMS_SelfTest_IrqHook(void);
void BMS_SelfTest_PcCheckpoint(unsigned int checkpoint);
void BMS_SelfTest_AdcObserve(unsigned int sample0_mv, unsigned int sample1_mv, unsigned int sample2_mv);
const bms_selftest_status_t *BMS_SelfTest_GetStatus(void);
unsigned char BMS_SelfTest_IsHealthy(void);

#ifdef __cplusplus
}
#endif

#endif
