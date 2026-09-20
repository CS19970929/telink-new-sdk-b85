#ifndef BMS_AFE_HW_MODBUS_H_
#define BMS_AFE_HW_MODBUS_H_

#include "tl_common.h"
#include "bms_afe_hw_profile.h"

/* 0x2500..0x2522: requested/persisted 35-word profile. */
#define BMS_AFE_HW_REQUESTED_REG_BASE           0x2500u

/* 0x2523..0x252B: read-only metadata. */
#define BMS_AFE_HW_META_CAPABILITIES             0x2523u
#define BMS_AFE_HW_META_VALID                    0x2524u
#define BMS_AFE_HW_META_SHUNT_UOHM               0x2525u
#define BMS_AFE_HW_META_CELL_COUNT               0x2526u
#define BMS_AFE_HW_META_WDT_SECONDS              0x2527u
#define BMS_AFE_HW_META_ACCESS_ACTIVE            0x2528u
#define BMS_AFE_HW_META_APPLY_STATE              0x2529u
#define BMS_AFE_HW_META_LAST_ERROR               0x252Au
#define BMS_AFE_HW_META_INTERFACE_VERSION        0x252Bu
#define BMS_AFE_HW_INTERFACE_VERSION             0x0002u
#define BMS_AFE_HW_REQUESTED_REG_COUNT           44u

/* 0x2540..0x2562: read-only values actually represented by the AFE. */
#define BMS_AFE_HW_EFFECTIVE_REG_BASE            0x2540u
#define BMS_AFE_HW_EFFECTIVE_REG_COUNT           BMS_AFE_HW_PROFILE_WORD_COUNT

typedef enum
{
    BMS_AFE_HW_APPLY_IDLE = 0u,
    BMS_AFE_HW_APPLY_OK = 1u,
    BMS_AFE_HW_APPLY_ROLLBACK_OK = 2u,
    BMS_AFE_HW_APPLY_INCONSISTENT = 3u,
} bms_afe_hw_apply_state_t;

typedef enum
{
    BMS_AFE_HW_ERROR_NONE = 0u,
    BMS_AFE_HW_ERROR_AUTH = 1u,
    BMS_AFE_HW_ERROR_VALIDATION = 2u,
    BMS_AFE_HW_ERROR_STORE = 3u,
    BMS_AFE_HW_ERROR_APPLY_VERIFY = 4u,
    BMS_AFE_HW_ERROR_ROLLBACK = 5u,
} bms_afe_hw_error_t;

#endif /* BMS_AFE_HW_MODBUS_H_ */
