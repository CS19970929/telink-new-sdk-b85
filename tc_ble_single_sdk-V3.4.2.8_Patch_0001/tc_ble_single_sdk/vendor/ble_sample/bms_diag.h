#ifndef BMS_DIAG_H_
#define BMS_DIAG_H_
#include <stdint.h>

/* Schema 1. Main-loop only: no ISR producers, no I/O in the read path. */
#ifndef BMS_DIAG_BUILD_ID
#define BMS_DIAG_BUILD_ID 0u /* 0 means unavailable; official builds supply commit prefix. */
#endif
#define BMS_DIAG_BASE 0x2A00u
#define BMS_DIAG_TRACE_BASE 0x2B00u
#define BMS_DIAG_END 0x2E00u
#define BMS_DIAG_TRACE_COUNT 64u
#define BMS_DIAG_TRACE_WORDS 12u

#define BMS_DIAG_CAP_BOOT       0x0001u
#define BMS_DIAG_CAP_TRACE      0x0002u
#define BMS_DIAG_CAP_STORAGE    0x0004u
#define BMS_DIAG_CAP_MOS        0x0008u
#define BMS_DIAG_CAP_UPGRADE    0x0010u
#define BMS_DIAG_CAP_RUNTIME    0x0020u
#define BMS_DIAG_CAPABILITIES   (BMS_DIAG_CAP_BOOT | BMS_DIAG_CAP_TRACE | \
                                 BMS_DIAG_CAP_STORAGE | BMS_DIAG_CAP_MOS | \
                                 BMS_DIAG_CAP_UPGRADE | BMS_DIAG_CAP_RUNTIME)

#define BMS_DIAG_RUNTIME_VERSION 1u
#define BMS_DIAG_RUNTIME_OFFSET  192u
enum { DIAG_NOT_RUN=0, DIAG_OK=1, DIAG_PORT=2, DIAG_LAYOUT=3,
    DIAG_REGION=4, DIAG_OPEN=5, DIAG_DEFAULTS=6, DIAG_SAVE=7,
    DIAG_PROGRAM_VERIFY=8, DIAG_ERASE_VERIFY=9, DIAG_OTA=10,
    DIAG_LOCK=11, DIAG_BACKOFF=12, DIAG_INVALID=13, DIAG_STARTED=14 };
enum { DIAG_EV_BOOT=1, DIAG_EV_INIT=2, DIAG_EV_STORAGE=3,
    DIAG_EV_PARAMS=4, DIAG_EV_MOS=5, DIAG_EV_AFE=6, DIAG_EV_BOOT_DONE=7,
    DIAG_EV_DRIVER=8, DIAG_EV_UPGRADE=9, DIAG_EV_CURRENT_RECOVERY=10,
    DIAG_EV_PM_STATE=11, DIAG_EV_PROTECTION=12, DIAG_EV_SAMPLE_STATE=13 };
/* Capability bit 4: frozen startup revision outcome, separate from store open. */
enum { DIAG_UPGRADE_STARTED=1, DIAG_UPGRADE_CONFIG_LOAD=2,
    DIAG_UPGRADE_VALIDATION=3, DIAG_UPGRADE_SAVE=4, DIAG_UPGRADE_CONFIG_OK=5,
    DIAG_UPGRADE_STATE=6, DIAG_UPGRADE_EVENT=7, DIAG_UPGRADE_OK=8 };
enum { DIAG_UPGRADE_BAD_SW=1u, DIAG_UPGRADE_BAD_AFE=2u,
    DIAG_UPGRADE_BAD_SOC=4u, DIAG_UPGRADE_BAD_CAPACITY=8u };
void bms_diag_upgrade(uint16_t stage, uint16_t invalid_mask);

enum { DIAG_BLOCK_PARAMS=1u, DIAG_BLOCK_UPGRADE=2u, DIAG_BLOCK_OUTPUT=4u,
    DIAG_BLOCK_COMM=8u, DIAG_BLOCK_SW=16u, DIAG_BLOCK_HW=32u,
    DIAG_BLOCK_OPENWIRE=64u, DIAG_BLOCK_HEATER=128u,
    DIAG_BLOCK_SHUTDOWN=256u, DIAG_BLOCK_TEMP=512u, DIAG_BLOCK_BACKEND=1024u };

enum {
    DIAG_PM_BLOCK_SAMPLE_INVALID = 1u,
    DIAG_PM_BLOCK_OTA = 2u,
    DIAG_PM_BLOCK_FLASH = 4u,
    DIAG_PM_BLOCK_BUS = 8u,
    DIAG_PM_BLOCK_CURRENT = 16u,
    DIAG_PM_BLOCK_SAMPLE_PENDING = 32u,
    DIAG_PM_BLOCK_POWER_OFF = 64u,
    DIAG_PM_BLOCK_ACC_SLEEP = 128u
};
uint32_t bms_diag_tick(void);
void bms_diag_init(void);
void bms_diag_freeze_boot(void);
void bms_diag_trace(uint16_t event, uint32_t arg0, uint32_t arg1);
void bms_diag_boot_word(uint16_t offset, uint16_t value);
void bms_diag_boot_u32(uint16_t offset, uint32_t value);
void bms_diag_attempt(uint8_t domain);
void bms_diag_result(uint8_t domain, uint16_t result);
void bms_diag_storage_error(uint16_t reason, uint32_t address);
void bms_diag_params(uint8_t valid, uint8_t upgrade);
void bms_diag_mos(uint16_t requested, uint32_t charge, uint32_t discharge);
void bms_diag_command(uint8_t command, uint8_t valid);
void bms_diag_driver(uint8_t flags, uint8_t valid);
void bms_param_diag_poll(void);
void bms_afe_diag_poll(void);
void bms_diag_backend(uint16_t charge, uint16_t discharge);
uint16_t bms_diag_cached_word(uint16_t offset);
void bms_diag_counter(uint16_t index, uint32_t value);
void bms_diag_runtime_sample(uint8_t valid, int32_t raw_current_ma,
                             int32_t current_ma, uint32_t sample_tick_32k,
                             uint8_t current_recovery_pending);
void bms_diag_runtime_soc(uint8_t soc_estimate, uint8_t soc_display,
                          uint8_t ocv_state, uint8_t ocv_center,
                          uint8_t ocv_low, uint8_t ocv_high,
                          uint8_t ocv_confidence, uint16_t rest_seconds,
                          uint8_t learning_state, uint8_t capacity_learned,
                          uint16_t learned_capacity_0p1ah,
                          uint16_t current_deadband_ma);
void bms_diag_runtime_pm(uint8_t suspend_allowed, uint32_t block_mask,
                         uint8_t low_voltage_region, uint32_t low_voltage_seconds,
                         uint8_t ble_connected, uint8_t sample_pending,
                         uint16_t suspend_current_threshold_ma);
void bms_diag_runtime_faults(uint16_t level1, uint16_t level2, uint16_t level3);
void bms_diag_runtime_mode(uint8_t factory_mode);
int bms_diag_overlaps(uint16_t start, uint16_t count);
int bms_diag_read(uint16_t start, uint16_t count, uint8_t *bytes);
#endif
