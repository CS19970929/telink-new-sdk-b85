#ifndef DVC1124_CONFIG_SERVICE_H_
#define DVC1124_CONFIG_SERVICE_H_

#include "tl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transport-neutral semantic field IDs.
 *
 * The numeric values intentionally match the low byte of the current 0x2800
 * communication window, but UART/BLE code must call this service rather than
 * touching DVC registers or g_tParam directly.
 */
typedef enum
{
    DVC1124_CFG_SCHEMA                    = 0x00,
    DVC1124_CFG_MODEL                     = 0x01,
    DVC1124_CFG_CHIP_VERSION              = 0x02,
    DVC1124_CFG_WRITE_ADDR                = 0x03,
    DVC1124_CFG_CELL_COUNT                = 0x04,
    DVC1124_CFG_SHUNT_UOHM_LO             = 0x05,
    DVC1124_CFG_SHUNT_UOHM_HI             = 0x06,

    DVC1124_CFG_HS_FET_MASK               = 0x10,
    DVC1124_CFG_CADC_WORK_ENABLE          = 0x11,
    DVC1124_CFG_CURRENT_WAKE_ENABLE       = 0x12,
    DVC1124_CFG_CC1_WORK_TIME             = 0x13,
    DVC1124_CFG_CC1_SLEEP_WAKE_TIME       = 0x14,
    DVC1124_CFG_CHARGE_PUMP_VOLTAGE       = 0x15,
    DVC1124_CFG_CELL_MEAS_MASK            = 0x16,
    DVC1124_CFG_CELL_SIGNED_MODE          = 0x17,
    DVC1124_CFG_VADC_ENABLE               = 0x18,
    DVC1124_CFG_VADC_SYNC                 = 0x19,
    DVC1124_CFG_VADC_PERIOD_CYCLES        = 0x1A,
    DVC1124_CFG_VADC_TIME_US              = 0x1B,

    DVC1124_CFG_GP1_MODE                  = 0x20,
    DVC1124_CFG_GP2_MODE                  = 0x21,
    DVC1124_CFG_GP3_MODE                  = 0x22,
    DVC1124_CFG_GP4_MODE                  = 0x23,
    DVC1124_CFG_GP5_MODE                  = 0x24,
    DVC1124_CFG_GP6_MODE                  = 0x25,
    DVC1124_CFG_V3P3_SLEEP_ENABLE         = 0x28,
    DVC1124_CFG_V3P3_WORK_ENABLE          = 0x29,
    DVC1124_CFG_V3P3_TIMEOUT_RESTART      = 0x2A,
    DVC1124_CFG_I2C_WDT_SECONDS           = 0x2B,
    DVC1124_CFG_TIMED_WAKE_SECONDS        = 0x2C,
    DVC1124_CFG_INTERRUPT_MASK            = 0x2D,
    DVC1124_CFG_CURRENT_WAKE_UV           = 0x30,
    DVC1124_CFG_BODY_DIODE_UV             = 0x31,
    DVC1124_CFG_DSG_PULLDOWN              = 0x32,
    DVC1124_CFG_I2C_TIMEOUT_CLOSE_CHG     = 0x33,
    DVC1124_CFG_I2C_TIMEOUT_CLOSE_DSG     = 0x34,
    DVC1124_CFG_CORE_OT_X10C              = 0x35,

    DVC1124_CFG_REQ_COV_MV                = 0x40,
    DVC1124_CFG_REQ_COV_DELAY_MS          = 0x41,
    DVC1124_CFG_REQ_CUV_MV                = 0x42,
    DVC1124_CFG_REQ_CUV_DELAY_MS          = 0x43,
    DVC1124_CFG_REQ_OCD1_X10A             = 0x44,
    DVC1124_CFG_REQ_OCD1_DELAY_MS         = 0x45,
    DVC1124_CFG_REQ_OCC1_X10A             = 0x46,
    DVC1124_CFG_REQ_OCC1_DELAY_MS         = 0x47,
    DVC1124_CFG_REQ_OCD2_X10A             = 0x48,
    DVC1124_CFG_REQ_OCD2_DELAY_MS         = 0x49,
    DVC1124_CFG_REQ_OCC2_X10A             = 0x4A,
    DVC1124_CFG_REQ_OCC2_DELAY_MS         = 0x4B,
    DVC1124_CFG_REQ_SCD_MV                = 0x4C,
    DVC1124_CFG_REQ_SCD_DELAY_US          = 0x4D,

    DVC1124_CFG_EFF_COV_MV                = 0x50,
    DVC1124_CFG_EFF_COV_DELAY_MS          = 0x51,
    DVC1124_CFG_EFF_CUV_MV                = 0x52,
    DVC1124_CFG_EFF_CUV_DELAY_MS          = 0x53,
    DVC1124_CFG_EFF_OCD1_X10A             = 0x54,
    DVC1124_CFG_EFF_OCD1_DELAY_MS         = 0x55,
    DVC1124_CFG_EFF_OCC1_X10A             = 0x56,
    DVC1124_CFG_EFF_OCC1_DELAY_MS         = 0x57,
    DVC1124_CFG_EFF_OCD2_X10A             = 0x58,
    DVC1124_CFG_EFF_OCD2_DELAY_MS         = 0x59,
    DVC1124_CFG_EFF_OCC2_X10A             = 0x5A,
    DVC1124_CFG_EFF_OCC2_DELAY_MS         = 0x5B,
    DVC1124_CFG_EFF_SCD_MV                = 0x5C,
    DVC1124_CFG_EFF_SCD_DELAY_US          = 0x5D,
} dvc1124_config_field_t;

typedef enum
{
    DVC1124_CFG_OK = 0,
    DVC1124_CFG_ERR_ADDRESS,
    DVC1124_CFG_ERR_READ_ONLY,
    DVC1124_CFG_ERR_VALUE,
    DVC1124_CFG_ERR_AFE_IO,
    DVC1124_CFG_ERR_STORE,
    DVC1124_CFG_ERR_FORBIDDEN,
} dvc1124_config_result_t;

#define DVC1124_CONFIG_SCHEMA_VERSION 0x0001u

dvc1124_config_result_t DVC1124_ConfigServiceRead(dvc1124_config_field_t field,
                                                   u32 *value);
dvc1124_config_result_t DVC1124_ConfigServiceWrite(dvc1124_config_field_t field,
                                                    u32 value);

/* Raw register diagnostics. All offsets 0x00..0x90 are readable. */
dvc1124_config_result_t DVC1124_ConfigServiceReadRaw(u8 reg, u8 *value);

/*
 * Raw write is factory-only. It is intentionally narrower than semantic write:
 * product protections that already have semantic/BMS parameter ownership must
 * be changed through the semantic API, not by bypassing requested/effective
 * bookkeeping.
 */
dvc1124_config_result_t DVC1124_ConfigServiceWriteRaw(u8 reg, u8 value);

#ifdef __cplusplus
}
#endif

#endif /* DVC1124_CONFIG_SERVICE_H_ */
