#ifndef FLASH_LAYOUT_H_
#define FLASH_LAYOUT_H_

#include "tl_common.h"
#include "drivers.h"
#include "vendor/common/ble_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Flash layout policy:
 * 1. This file is the single source of truth for application-level flash regions.
 * 2. SDK-reserved regions (MAC / calibration / SMP / master pairing) are not reused by app storage.
 * 3. Stage-A layout assumes OTA new-fw bank still uses 0x20000, so 0x40000~0x73FFF remains usable.
 * 4. Once firmware size approaches the 0x20000 bank limit, layout must migrate to Stage-B.
 */

#define FLASH_LAYOUT_STAGE_A                    1u
#define FLASH_LAYOUT_STAGE_B                    2u

#ifndef FLASH_LAYOUT_STAGE
#define FLASH_LAYOUT_STAGE                      FLASH_LAYOUT_STAGE_A
#endif

#define FLASH_APP_SECTOR_SIZE                   0x1000u

typedef struct
{
    u32 base;
    u16 sectors;
} flash_region_t;

/* ================= Stage-A application regions ================= */
#define FLASH_REGION_STATE_A_BASE               0x40000u
#define FLASH_REGION_STATE_A_SECTORS            2u

#define FLASH_REGION_STATE_EXT_BASE             0x42000u
#define FLASH_REGION_STATE_EXT_SECTORS          2u

#define FLASH_REGION_EVENT_LOG_BASE             0x44000u
#define FLASH_REGION_EVENT_LOG_SECTORS          8u

#define FLASH_REGION_RESERVE_BASE               0x4C000u
#define FLASH_REGION_RESERVE_SECTORS            4u

#define FLASH_REGION_CFG_A_BASE                 0x50000u
#define FLASH_REGION_CFG_A_SECTORS              1u

#define FLASH_REGION_CFG_B_BASE                 0x52000u
#define FLASH_REGION_CFG_B_SECTORS              1u

#define FLASH_REGION_FUTURE_BASE                0x53000u
#define FLASH_REGION_FUTURE_END                 0x74000u

/* ================= SDK reserved regions on 512K targets ================= */
#define FLASH_REGION_SDK_SMP_BASE_512K         FLASH_ADR_SMP_PAIRING_512K_FLASH
#define FLASH_REGION_SDK_MAC_BASE_512K         CFG_ADR_MAC_512K_FLASH
#define FLASH_REGION_SDK_CAL_BASE_512K         CFG_ADR_CALIBRATION_512K_FLASH
#define FLASH_REGION_SDK_MASTER_PAIR_BASE_512K FLASH_ADR_MASTER_PAIRING_512K

/* ================= compatibility aliases for current modules ================= */
#define FLASH_ADR_CFG_SLOT_A                    FLASH_REGION_CFG_A_BASE
#define FLASH_ADR_CFG_SLOT_B                    FLASH_REGION_CFG_B_BASE

#define FLASH_ADR_STATE_SLOT_A                  FLASH_REGION_STATE_A_BASE
#define FLASH_ADR_STATE_SLOT_B                  (FLASH_REGION_STATE_A_BASE + FLASH_APP_SECTOR_SIZE)

#define FLASH_ADR_STATE_EXT_SLOT_A              FLASH_REGION_STATE_EXT_BASE
#define FLASH_ADR_STATE_EXT_SLOT_B              (FLASH_REGION_STATE_EXT_BASE + FLASH_APP_SECTOR_SIZE)

#define FLASH_ADR_EVENT_LOG_BASE                FLASH_REGION_EVENT_LOG_BASE
#define FLASH_ADR_EVENT_LOG_END                 (FLASH_REGION_EVENT_LOG_BASE + FLASH_REGION_EVENT_LOG_SECTORS * FLASH_APP_SECTOR_SIZE)

static inline flash_region_t flash_layout_get_cfg_region_a(void)
{
    flash_region_t region = { FLASH_REGION_CFG_A_BASE, FLASH_REGION_CFG_A_SECTORS };
    return region;
}

static inline flash_region_t flash_layout_get_cfg_region_b(void)
{
    flash_region_t region = { FLASH_REGION_CFG_B_BASE, FLASH_REGION_CFG_B_SECTORS };
    return region;
}

static inline flash_region_t flash_layout_get_state_region(void)
{
    flash_region_t region = { FLASH_REGION_STATE_A_BASE, FLASH_REGION_STATE_A_SECTORS + FLASH_REGION_STATE_EXT_SECTORS };
    return region;
}

static inline flash_region_t flash_layout_get_event_log_region(void)
{
    flash_region_t region = { FLASH_REGION_EVENT_LOG_BASE, FLASH_REGION_EVENT_LOG_SECTORS };
    return region;
}

#ifdef __cplusplus
}
#endif

#endif
