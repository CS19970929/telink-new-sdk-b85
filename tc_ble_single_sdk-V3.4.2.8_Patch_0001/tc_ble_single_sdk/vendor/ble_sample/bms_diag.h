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
enum { DIAG_NOT_RUN=0, DIAG_OK=1, DIAG_PORT=2, DIAG_LAYOUT=3,
    DIAG_REGION=4, DIAG_OPEN=5, DIAG_DEFAULTS=6, DIAG_SAVE=7,
    DIAG_PROGRAM_VERIFY=8, DIAG_ERASE_VERIFY=9, DIAG_OTA=10,
    DIAG_LOCK=11, DIAG_BACKOFF=12, DIAG_INVALID=13, DIAG_STARTED=14 };
enum { DIAG_EV_BOOT=1, DIAG_EV_INIT=2, DIAG_EV_STORAGE=3,
    DIAG_EV_PARAMS=4, DIAG_EV_MOS=5, DIAG_EV_AFE=6, DIAG_EV_BOOT_DONE=7, DIAG_EV_DRIVER=8, DIAG_EV_UPGRADE=9, DIAG_EV_CURRENT_RECOVERY=10 };
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
int bms_diag_overlaps(uint16_t start, uint16_t count);
int bms_diag_read(uint16_t start, uint16_t count, uint8_t *bytes);
#endif
