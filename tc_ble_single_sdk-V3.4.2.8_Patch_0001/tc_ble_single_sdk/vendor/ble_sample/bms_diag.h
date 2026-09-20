#ifndef BMS_DIAG_H_
#define BMS_DIAG_H_

#include <stdint.h>

/* Shared host diagnostics schema. All producers and Modbus reads run in the
 * cooperative main loop; the window is read-only. */
#ifndef BMS_DIAG_BUILD_ID
#define BMS_DIAG_BUILD_ID 0u
#endif
#ifndef BMS_DIAG_BUILD_DIRTY
#define BMS_DIAG_BUILD_DIRTY 0
#endif
#if (BMS_DIAG_BUILD_DIRTY != 0) && (BMS_DIAG_BUILD_DIRTY != 1)
#error "BMS_DIAG_BUILD_DIRTY must be 0 or 1"
#endif

#define BMS_DIAG_BASE        0x2A00u
#define BMS_DIAG_TRACE_BASE  0x2B00u
#define BMS_DIAG_END         0x2E00u
#define BMS_DIAG_TRACE_COUNT 64u
#define BMS_DIAG_TRACE_WORDS 12u

#define BMS_DIAG_CAP_BOOT       0x0001u
#define BMS_DIAG_CAP_TRACE      0x0002u
#define BMS_DIAG_CAP_STORAGE    0x0004u
#define BMS_DIAG_CAP_MOS        0x0008u
#define BMS_DIAG_CAP_RUNTIME    0x0020u
#define BMS_DIAG_CAPABILITIES   (BMS_DIAG_CAP_BOOT | BMS_DIAG_CAP_TRACE | \
                                 BMS_DIAG_CAP_STORAGE | BMS_DIAG_CAP_MOS | \
                                 BMS_DIAG_CAP_RUNTIME)

/* word 29 describes optional semantics without changing schema 1. */
#define BMS_DIAG_INFO_GENERIC_FET_BITS 0x0001u

#define BMS_DIAG_RUNTIME_VERSION 2u

enum {
    DIAG_NOT_RUN=0, DIAG_OK=1, DIAG_PORT=2, DIAG_LAYOUT=3,
    DIAG_REGION=4, DIAG_OPEN=5, DIAG_DEFAULTS=6, DIAG_SAVE=7,
    DIAG_PROGRAM_VERIFY=8, DIAG_ERASE_VERIFY=9, DIAG_OTA=10,
    DIAG_LOCK=11, DIAG_BACKOFF=12, DIAG_INVALID=13, DIAG_STARTED=14
};
enum {
    DIAG_EV_BOOT=1, DIAG_EV_INIT=2, DIAG_EV_STORAGE=3,
    DIAG_EV_PARAMS=4, DIAG_EV_MOS=5, DIAG_EV_AFE=6,
    DIAG_EV_BOOT_DONE=7, DIAG_EV_DRIVER=8,
    DIAG_EV_PROTECTION=12, DIAG_EV_SAMPLE_STATE=13
};
enum {
    DIAG_BLOCK_PARAMS=1u, DIAG_BLOCK_UPGRADE=2u,
    DIAG_BLOCK_OUTPUT=4u, DIAG_BLOCK_COMM=8u,
    DIAG_BLOCK_SW=16u, DIAG_BLOCK_HW=32u,
    DIAG_BLOCK_OPENWIRE=64u, DIAG_BLOCK_HEATER=128u,
    DIAG_BLOCK_SHUTDOWN=256u, DIAG_BLOCK_TEMP=512u,
    DIAG_BLOCK_BACKEND=1024u
};

void bms_diag_init(void);
void bms_diag_set_build_flags(uint16_t flags);
void bms_diag_set_boot_result(uint16_t afe_result, uint16_t parameter_result);
void bms_diag_freeze_boot(void);
void bms_diag_attempt(uint8_t domain);
void bms_diag_result(uint8_t domain, uint16_t result);
void bms_diag_storage_error(uint16_t reason, uint32_t address);
void bms_diag_poll_runtime(uint8_t sample_valid, int32_t current_ma,
                           uint32_t sample_tick_32k, uint8_t factory_mode);
int bms_diag_overlaps(uint16_t start, uint16_t count);
int bms_diag_read(uint16_t start, uint16_t count, uint8_t *bytes);

#endif
