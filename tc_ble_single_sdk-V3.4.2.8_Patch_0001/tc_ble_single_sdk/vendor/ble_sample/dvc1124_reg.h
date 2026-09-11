#ifndef DVC1124_REG_H_
#define DVC1124_REG_H_

#include <stdint.h>

/*
 * DVC1124-2 register truth source.
 *
 * Source: DVC1124-2 Reference Manual V1.2.
 * This file describes chip facts only: register addresses, documented fields,
 * masks, shifts, reset values and enumerations. It intentionally contains no
 * HS-D008 board policy, BMS protection policy or transport protocol mapping.
 *
 * Do not use C bit-fields for AFE registers. tc32/ARM compiler bit-field layout
 * is implementation-defined; all register access must use masks/shifts.
 *
 * Unnamed/reserved bits are deliberately excluded from documented write masks.
 * They must be preserved with read-modify-write.
 */

#define DVC1124_REG_MAX                         0x90u

/* Measurement/status register map. */
#define DVC1124_REG_ALARM                       0x00u
#define DVC1124_REG_STATUS                      0x01u
#define DVC1124_REG_CC1_H                       0x02u
#define DVC1124_REG_CC1_L                       0x03u
#define DVC1124_REG_CC2_H                       0x04u
#define DVC1124_REG_CC2_M                       0x05u
#define DVC1124_REG_CC2_L_FLAGS                 0x06u
#define DVC1124_REG_VTOP_H                      0x07u
#define DVC1124_REG_VTOP_L                      0x08u
#define DVC1124_REG_VPACK_H                     0x09u
#define DVC1124_REG_VPACK_L                     0x0Au
#define DVC1124_REG_VLOAD_H                     0x0Bu
#define DVC1124_REG_VLOAD_L                     0x0Cu
#define DVC1124_REG_VCT_H                       0x0Du
#define DVC1124_REG_VCT_L                       0x0Eu
#define DVC1124_REG_V1P8_H                      0x0Fu
#define DVC1124_REG_V1P8_L                      0x10u
#define DVC1124_REG_GP1_H                       0x11u
#define DVC1124_REG_GP1_L                       0x12u
#define DVC1124_REG_GP2_H                       0x13u
#define DVC1124_REG_GP2_L                       0x14u
#define DVC1124_REG_GP3_H                       0x15u
#define DVC1124_REG_GP3_L                       0x16u
#define DVC1124_REG_GP4_H                       0x17u
#define DVC1124_REG_GP4_L                       0x18u
#define DVC1124_REG_GP5_H                       0x19u
#define DVC1124_REG_GP5_L                       0x1Au
#define DVC1124_REG_GP6_H                       0x1Bu
#define DVC1124_REG_GP6_L                       0x1Cu
#define DVC1124_REG_CELL1_H                     0x1Du
#define DVC1124_REG_CELL24_L                    0x4Cu
#define DVC1124_REG_VVOS_H                      0x4Du
#define DVC1124_REG_VVOS_L                      0x4Eu
#define DVC1124_REG_CVOS_H                      0x4Fu
#define DVC1124_REG_CVOS_L                      0x50u

#define DVC1124_REG_CELL_H(cell_1_to_24) \
    ((uint8_t)(DVC1124_REG_CELL1_H + (((uint8_t)(cell_1_to_24) - 1u) * 2u)))
#define DVC1124_REG_CELL_L(cell_1_to_24) \
    ((uint8_t)(DVC1124_REG_CELL_H(cell_1_to_24) + 1u))

/* Configuration/control register map. */
#define DVC1124_REG_FET_CTRL                     0x51u
#define DVC1124_REG_DSG_PULLDOWN                 0x52u
#define DVC1124_REG_DSG_MASK                     0x53u
#define DVC1124_REG_CHG_MASK                     0x54u
#define DVC1124_REG_CADC_CTRL                    0x55u
#define DVC1124_REG_CC1_TIMING                   0x56u
#define DVC1124_REG_RESERVED_57                  0x57u
#define DVC1124_REG_RESERVED_58                  0x58u
#define DVC1124_REG_OCD1_THR                     0x59u
#define DVC1124_REG_OCC1_THR                     0x5Au
#define DVC1124_REG_OCD1_DLY                     0x5Bu
#define DVC1124_REG_OCC1_DLY                     0x5Cu
#define DVC1124_REG_RESERVED_5D                  0x5Du
#define DVC1124_REG_OCD2                         0x5Eu
#define DVC1124_REG_OCC2                         0x5Fu
#define DVC1124_REG_OCD2_DLY                     0x60u
#define DVC1124_REG_OCC2_DLY                     0x61u
#define DVC1124_REG_SCD                          0x62u
#define DVC1124_REG_SCD_DLY                      0x63u
#define DVC1124_REG_RESERVED_64                  0x64u
#define DVC1124_REG_CURRENT_WAKE                 0x65u
#define DVC1124_REG_BODY_DIODE                   0x66u
#define DVC1124_REG_BAL_24_17                    0x67u
#define DVC1124_REG_BAL_16_9                     0x68u
#define DVC1124_REG_BAL_8_1                      0x69u
#define DVC1124_REG_CELL_MASK_24_17              0x6Au
#define DVC1124_REG_CELL_MASK_16_9               0x6Bu
#define DVC1124_REG_CELL_MASK_8_5_MISC           0x6Cu
#define DVC1124_REG_CP_CTRL                      0x6Du
#define DVC1124_REG_VADC_CTRL                    0x6Eu
#define DVC1124_REG_RESERVED_6F                  0x6Fu
#define DVC1124_REG_COV_H                        0x70u
#define DVC1124_REG_COV_L_DLY                    0x71u
#define DVC1124_REG_CUV_H                        0x72u
#define DVC1124_REG_CUV_L_DLY                    0x73u
#define DVC1124_REG_GP123_MODE                   0x74u
#define DVC1124_REG_GP456_MODE                   0x75u
#define DVC1124_REG_CORE_OT                      0x76u
#define DVC1124_REG_I2C_WDT                      0x77u
#define DVC1124_REG_TIMED_WAKE                   0x78u
#define DVC1124_REG_INT_MASK                     0x79u
#define DVC1124_REG_RESERVED_7A                  0x7Au
#define DVC1124_REG_RESERVED_7B                  0x7Bu
#define DVC1124_REG_RESERVED_7C                  0x7Cu
#define DVC1124_REG_RESERVED_7D                  0x7Du
#define DVC1124_REG_FRT                          0x7Eu
#define DVC1124_REG_CHIP_VERSION                 0x8Fu
#define DVC1124_REG_RESERVED_90                  0x90u

/* 0x00 ALARM: write 0 clears a flag; write 1 has no effect. */
#define DVC1124_ALARM_IWTF_MASK                  0x80u
#define DVC1124_ALARM_COV_MASK                   0x40u
#define DVC1124_ALARM_CUV_MASK                   0x20u
#define DVC1124_ALARM_OCD1_MASK                  0x10u
#define DVC1124_ALARM_OCC1_MASK                  0x08u
#define DVC1124_ALARM_OCD2_MASK                  0x04u
#define DVC1124_ALARM_OCC2_MASK                  0x02u
#define DVC1124_ALARM_SCD_MASK                   0x01u
#define DVC1124_ALARM_RESET                      0x00u

/* 0x01 STATUS. VADF/CC1F/CC2F are read-clear. */
#define DVC1124_STATUS_PD_MASK                   0x80u
#define DVC1124_STATUS_VADF_MASK                 0x40u
#define DVC1124_STATUS_CC1F_MASK                 0x20u
#define DVC1124_STATUS_CC2F_MASK                 0x10u
#define DVC1124_STATUS_CST_MASK                  0x0Fu
#define DVC1124_STATUS_CST_SHIFT                 0u
#define DVC1124_STATUS_RESET                     0x00u

typedef enum
{
    DVC1124_CST_WAKE_FROM_SHUTDOWN = 0x0,
    DVC1124_CST_WAKE_BY_I2C        = 0x1,
    DVC1124_CST_WAKE_BY_TIMER      = 0x2,
    DVC1124_CST_WAKE_BY_DSG_CUR    = 0x3,
    DVC1124_CST_WAKE_BY_CHG_CUR    = 0x4,
    DVC1124_CST_WAKE_BY_OCD2       = 0x5,
    DVC1124_CST_WAKE_BY_OCC2       = 0x6,
    DVC1124_CST_WAKE_BY_SCD        = 0x7,
    DVC1124_CST_WAKE_BY_CHARGER    = 0x8,
    DVC1124_CST_WAIT_SHUTDOWN      = 0xB,
    DVC1124_CST_WAIT_SLEEP         = 0xC,
    DVC1124_CST_RESET_REGISTERS    = 0xD,
    DVC1124_CST_ENTER_SLEEP        = 0xE,
    DVC1124_CST_ENTER_SHUTDOWN     = 0xF
} dvc1124_cst_t;

/* 0x06 CC2 low nibble also reports FET output states. */
#define DVC1124_CC2_PDSGF_MASK                   0x08u
#define DVC1124_CC2_PCHGF_MASK                   0x04u
#define DVC1124_CC2_DSGF_MASK                    0x02u
#define DVC1124_CC2_CHGF_MASK                    0x01u

/* 0x51 FET control. */
#define DVC1124_FET_LDPU_MASK                    0x80u
#define DVC1124_FET_PDSGC_MASK                   0x40u
#define DVC1124_FET_PCHGC_MASK                   0x20u
#define DVC1124_FET_DSGM_MASK                    0x10u
#define DVC1124_FET_DSGC_MASK                    0x0Cu
#define DVC1124_FET_DSGC_SHIFT                   2u
#define DVC1124_FET_CHGC_MASK                    0x03u
#define DVC1124_FET_CHGC_SHIFT                   0u
#define DVC1124_FET_CTRL_RESET                   0x00u

typedef enum
{
    DVC1124_FET_DRIVE_OFF       = 0u,
    DVC1124_FET_DRIVE_OFF_ALT   = 1u,
    DVC1124_FET_DRIVE_AUTO_DIODE = 2u,
    DVC1124_FET_DRIVE_ON        = 3u
} dvc1124_fet_drive_t;

/* 0x52: bits6:5 are unnamed; preserve them. */
#define DVC1124_PDWM_MASK                       0x80u
#define DVC1124_DPC_MASK                        0x1Fu
#define DVC1124_DPC_SHIFT                       0u
#define DVC1124_DSG_PULLDOWN_RESET              0x90u

/* 0x53 discharge masks. 0 = source can close output, 1 = masked/no effect. */
#define DVC1124_DSGMASK_PDDM_MASK               0x80u
#define DVC1124_DSGMASK_PCWM_MASK               0x40u
#define DVC1124_DSGMASK_PCCM_MASK               0x20u
#define DVC1124_DSGMASK_PCDM_MASK               0x10u
#define DVC1124_DSGMASK_DWM_MASK                0x08u
#define DVC1124_DSGMASK_DDM_MASK                0x04u
#define DVC1124_DSGMASK_DPDM_MASK               0x02u
#define DVC1124_DSGMASK_DBDM_MASK               0x01u
#define DVC1124_DSG_MASK_RESET                   0x59u

/* 0x54 charge masks. */
#define DVC1124_CHGMASK_CWM_MASK                0x80u
#define DVC1124_CHGMASK_CO1M_MASK               0x40u
#define DVC1124_CHGMASK_CO2M_MASK               0x20u
#define DVC1124_CHGMASK_CSM_MASK                0x10u
#define DVC1124_CHGMASK_CDM_MASK                0x08u
#define DVC1124_CHGMASK_CCM_MASK                0x04u
#define DVC1124_CHGMASK_CPCM_MASK               0x02u
#define DVC1124_CHGMASK_CBDM_MASK               0x01u
#define DVC1124_CHG_MASK_RESET                   0xF9u

/* 0x55 CADC. Unnamed bits are preserved. CAMZ is a self-clearing command. */
#define DVC1124_CADC_HSFM_MASK                  0x80u
#define DVC1124_CADC_CAEW_MASK                  0x08u
#define DVC1124_CADC_CAES_MASK                  0x04u
#define DVC1124_CADC_CAMZ_MASK                  0x01u
#define DVC1124_CADC_CTRL_RESET                 0x2Cu

/* 0x56. bits7:4 are unnamed/reserved and reset to 1. */
#define DVC1124_CC1_WORK_TIME_MASK              0x0Cu
#define DVC1124_CC1_WORK_TIME_SHIFT             2u
#define DVC1124_CC1_SLEEP_WAKE_TIME_MASK        0x03u
#define DVC1124_CC1_SLEEP_WAKE_TIME_SHIFT       0u
#define DVC1124_CC1_TIMING_RESET                0xFFu

typedef enum
{
    DVC1124_CC1_WORK_0P5MS = 0u,
    DVC1124_CC1_WORK_1MS   = 1u,
    DVC1124_CC1_WORK_2MS   = 2u,
    DVC1124_CC1_WORK_4MS   = 3u
} dvc1124_cc1_work_time_t;

typedef enum
{
    DVC1124_CC1_SLEEP_WAKE_4MS  = 0u,
    DVC1124_CC1_SLEEP_WAKE_8MS  = 1u,
    DVC1124_CC1_SLEEP_WAKE_16MS = 2u,
    DVC1124_CC1_SLEEP_WAKE_32MS = 3u
} dvc1124_cc1_sleep_wake_time_t;

/* OC1: code 0 disables; threshold = code * 0.25mV. */
#define DVC1124_OCD1_THR_RESET                  0x00u
#define DVC1124_OCC1_THR_RESET                  0x00u
/* OC1 delay = (code + 1) * 8ms. */
#define DVC1124_OCD1_DLY_RESET                  0x00u
#define DVC1124_OCC1_DLY_RESET                  0x00u

/* OC2: bit7 is not an owned field. Enable is bit6. */
#define DVC1124_OC2_ENABLE_MASK                 0x40u
#define DVC1124_OC2_THRESHOLD_MASK              0x3Fu
#define DVC1124_OC2_THRESHOLD_SHIFT             0u
#define DVC1124_OCD2_RESET                      0x40u
#define DVC1124_OCC2_RESET                      0xC0u
/* OC2 threshold = (code + 1) * 4mV; delay = (code + 1) * 4ms. */
#define DVC1124_OCD2_DLY_RESET                  0x00u
#define DVC1124_OCC2_DLY_RESET                  0x00u

/* SCD: bit7 is read-only; enable is bit6; threshold = code * 10mV. */
#define DVC1124_SCD_ENABLE_MASK                 0x40u
#define DVC1124_SCD_THRESHOLD_MASK              0x3Fu
#define DVC1124_SCD_THRESHOLD_SHIFT             0u
#define DVC1124_SCD_RESET                       0x40u
/* SCD delay = code * 7.81us. */
#define DVC1124_SCD_DLY_RESET                   0x00u

/* Current wake: 0 disables, otherwise code * 10uV. */
#define DVC1124_CURRENT_WAKE_RESET              0x00u
/* Body diode: 0 disables, otherwise code * 40uV. */
#define DVC1124_BODY_DIODE_RESET                0x00u

/* 0x67..0x69 balance bits auto-clear after about 60s. */
#define DVC1124_BALANCE_RESET                   0x00u

/* 0x6C measurement masks. */
#define DVC1124_CELLMASK_CM8_MASK               0x80u
#define DVC1124_CELLMASK_CM7_MASK               0x40u
#define DVC1124_CELLMASK_CM6_MASK               0x20u
#define DVC1124_CELLMASK_CM5_MASK               0x10u
#define DVC1124_CELLMASK_PKM_MASK               0x08u
#define DVC1124_CELLMASK_LDM_MASK               0x04u
#define DVC1124_CELLMASK_CTM_MASK               0x02u
#define DVC1124_CELLMASK_V1P8M_MASK             0x01u
#define DVC1124_CELL_MASK_RESET                 0x00u

/* 0x6D. bits7:6 are unnamed; never overwrite them. */
#define DVC1124_CPVS_MASK                       0x38u
#define DVC1124_CPVS_SHIFT                      3u
#define DVC1124_COW_MASK                        0x04u
#define DVC1124_CMM_MASK                        0x02u
#define DVC1124_CVS_MASK                        0x01u
#define DVC1124_CP_CTRL_RESET                   0x28u

typedef enum
{
    DVC1124_CPVS_OFF = 0u,
    DVC1124_CPVS_6V  = 1u,
    DVC1124_CPVS_7V  = 2u,
    DVC1124_CPVS_8V  = 3u,
    DVC1124_CPVS_9V  = 4u,
    DVC1124_CPVS_10V = 5u,
    DVC1124_CPVS_11V = 6u,
    DVC1124_CPVS_12V = 7u
} dvc1124_cp_voltage_t;

/* 0x6E. bits3:2 are unnamed; preserve them. */
#define DVC1124_VADC_ENABLE_MASK                0x80u
#define DVC1124_VADC_SYNC_MASK                  0x40u
#define DVC1124_VADC_PERIOD_MASK                0x30u
#define DVC1124_VADC_PERIOD_SHIFT               4u
#define DVC1124_VADC_TIME_MASK                  0x03u
#define DVC1124_VADC_TIME_SHIFT                 0u
#define DVC1124_VADC_CTRL_RESET                 0xCDu

typedef enum
{
    DVC1124_VADC_EVERY_1_CC2 = 0u,
    DVC1124_VADC_EVERY_2_CC2 = 1u,
    DVC1124_VADC_EVERY_4_CC2 = 2u,
    DVC1124_VADC_EVERY_8_CC2 = 3u
} dvc1124_vadc_period_t;

typedef enum
{
    DVC1124_VADC_TIME_0P79MS = 0u,
    DVC1124_VADC_TIME_1P54MS = 1u,
    DVC1124_VADC_TIME_3P03MS = 2u,
    DVC1124_VADC_TIME_6P02MS = 3u
} dvc1124_vadc_time_t;

/* COV/CUV 12-bit threshold + 4-bit delay. Code 0 disables protection. */
#define DVC1124_COV_RESET_H                      0x00u
#define DVC1124_COV_RESET_L                      0x00u
#define DVC1124_CUV_RESET_H                      0x00u
#define DVC1124_CUV_RESET_L                      0x00u
#define DVC1124_VPROT_THRESHOLD_LOW_MASK         0xF0u
#define DVC1124_VPROT_DELAY_MASK                 0x0Fu

/* 0x74 GP1/2/3. */
#define DVC1124_GP1_MODE_MASK                    0xC0u
#define DVC1124_GP1_MODE_SHIFT                   6u
#define DVC1124_GP2_MODE_MASK                    0x38u
#define DVC1124_GP2_MODE_SHIFT                   3u
#define DVC1124_GP3_MODE_MASK                    0x07u
#define DVC1124_GP3_MODE_SHIFT                   0u
#define DVC1124_GP123_MODE_RESET                 0x00u

/* 0x75 GP4/5/6. */
#define DVC1124_GP4_MODE_MASK                    0xC0u
#define DVC1124_GP4_MODE_SHIFT                   6u
#define DVC1124_GP5_MODE_MASK                    0x38u
#define DVC1124_GP5_MODE_SHIFT                   3u
#define DVC1124_GP6_MODE_MASK                    0x07u
#define DVC1124_GP6_MODE_SHIFT                   0u
#define DVC1124_GP456_MODE_RESET                 0x00u

typedef enum
{
    DVC1124_GP14_OFF        = 0u,
    DVC1124_GP14_NTC        = 1u,
    DVC1124_GP14_ANALOG     = 2u,
    DVC1124_GP1_CON_INPUT   = 3u,
    DVC1124_GP4_DON_INPUT   = 3u
} dvc1124_gp14_mode_t;

typedef enum
{
    DVC1124_GP236_OFF        = 0u,
    DVC1124_GP236_NTC        = 1u,
    DVC1124_GP236_ANALOG     = 2u,
    DVC1124_GP236_INTERRUPT  = 6u,
    DVC1124_GP2_LOW_PDSG     = 7u,
    DVC1124_GP3_LOW_PCHG     = 7u,
    DVC1124_GP5_LOW_CHG      = 7u,
    DVC1124_GP6_LOW_DSG      = 7u
} dvc1124_gp236_mode_t;

#define DVC1124_GP123_ENCODE(gp1, gp2, gp3) \
    ((uint8_t)((((uint8_t)(gp1) << DVC1124_GP1_MODE_SHIFT) & DVC1124_GP1_MODE_MASK) | \
               (((uint8_t)(gp2) << DVC1124_GP2_MODE_SHIFT) & DVC1124_GP2_MODE_MASK) | \
               (((uint8_t)(gp3) << DVC1124_GP3_MODE_SHIFT) & DVC1124_GP3_MODE_MASK)))

#define DVC1124_GP456_ENCODE(gp4, gp5, gp6) \
    ((uint8_t)((((uint8_t)(gp4) << DVC1124_GP4_MODE_SHIFT) & DVC1124_GP4_MODE_MASK) | \
               (((uint8_t)(gp5) << DVC1124_GP5_MODE_SHIFT) & DVC1124_GP5_MODE_MASK) | \
               (((uint8_t)(gp6) << DVC1124_GP6_MODE_SHIFT) & DVC1124_GP6_MODE_MASK)))

/* 0x76 core over-temperature. bit7 COTF is read-clear; bits6:0 are COTT. */
#define DVC1124_CORE_OT_FLAG_MASK                0x80u
#define DVC1124_CORE_OT_THRESHOLD_MASK           0x7Fu
#define DVC1124_CORE_OT_THRESHOLD_SHIFT          0u
#define DVC1124_CORE_OT_RESET                    0x00u

/* 0x77 I2C watchdog/V3P3. bit4 IWTS is status; bit3 is unnamed. */
#define DVC1124_V3P3_SLEEP_ENABLE_MASK           0x80u
#define DVC1124_V3P3_WORK_ENABLE_MASK            0x40u
#define DVC1124_V3P3_TIMEOUT_RESTART_MASK        0x20u
#define DVC1124_I2C_WDT_STATUS_MASK              0x10u
#define DVC1124_I2C_WDT_TIME_MASK                0x07u
#define DVC1124_I2C_WDT_TIME_SHIFT               0u
#define DVC1124_I2C_WDT_RESET                    0xC0u

typedef enum
{
    DVC1124_I2C_WDT_OFF = 0u,
    DVC1124_I2C_WDT_4S  = 4u,
    DVC1124_I2C_WDT_8S  = 5u,
    DVC1124_I2C_WDT_16S = 6u,
    DVC1124_I2C_WDT_32S = 7u
} dvc1124_i2c_wdt_code_t;

/* 0x78 timed wake. bit7 TIWK is status; bits6:4 unnamed. */
#define DVC1124_TIMED_WAKE_STATUS_MASK            0x80u
#define DVC1124_TIMED_WAKE_TIME_MASK              0x0Fu
#define DVC1124_TIMED_WAKE_TIME_SHIFT             0u
#define DVC1124_TIMED_WAKE_RESET                  0x00u

typedef enum
{
    DVC1124_TIMED_WAKE_OFF   = 0u,
    DVC1124_TIMED_WAKE_10S   = 1u,
    DVC1124_TIMED_WAKE_20S   = 2u,
    DVC1124_TIMED_WAKE_30S   = 3u,
    DVC1124_TIMED_WAKE_40S   = 4u,
    DVC1124_TIMED_WAKE_50S   = 5u,
    DVC1124_TIMED_WAKE_1MIN  = 6u,
    DVC1124_TIMED_WAKE_2MIN  = 7u,
    DVC1124_TIMED_WAKE_3MIN  = 8u,
    DVC1124_TIMED_WAKE_4MIN  = 9u,
    DVC1124_TIMED_WAKE_5MIN  = 10u,
    DVC1124_TIMED_WAKE_6MIN  = 11u,
    DVC1124_TIMED_WAKE_7MIN  = 12u,
    DVC1124_TIMED_WAKE_8MIN  = 13u,
    DVC1124_TIMED_WAKE_9MIN  = 14u,
    DVC1124_TIMED_WAKE_10MIN = 15u
} dvc1124_timed_wake_t;

/* 0x79 interrupt output masks. 0 = 1ms interrupt pulse enabled, 1 = masked. */
#define DVC1124_INTMASK_IWM_MASK                  0x80u
#define DVC1124_INTMASK_IVOM_MASK                 0x40u
#define DVC1124_INTMASK_ICCM_MASK                 0x20u
#define DVC1124_INTMASK_ICOM_MASK                 0x10u
#define DVC1124_INTMASK_ICUM_MASK                 0x08u
#define DVC1124_INTMASK_IOC1M_MASK                0x04u
#define DVC1124_INTMASK_IOC2M_MASK                0x02u
#define DVC1124_INTMASK_ISCDM_MASK                0x01u
#define DVC1124_INT_MASK_RESET                    0x00u

#define DVC1124_FIELD_PREP(mask, shift, value) \
    ((uint8_t)((((uint8_t)(value)) << (shift)) & (uint8_t)(mask)))
#define DVC1124_FIELD_GET(mask, shift, value) \
    ((uint8_t)((((uint8_t)(value)) & (uint8_t)(mask)) >> (shift)))

/*
 * All documented writable fields from V1.2.
 * A zero mask means the register is read-only or only has unnamed writable bits.
 * The caller must still use read-modify-write because some registers mix named
 * fields with read-only/unnamed bits.
 */
static inline uint8_t DVC1124_RegDocumentedWriteMask(uint8_t reg)
{
    switch (reg)
    {
    case DVC1124_REG_ALARM:              return 0xFFu;
    case DVC1124_REG_STATUS:             return DVC1124_STATUS_CST_MASK;
    case DVC1124_REG_FET_CTRL:           return 0xFFu;
    case DVC1124_REG_DSG_PULLDOWN:       return (uint8_t)(DVC1124_PDWM_MASK | DVC1124_DPC_MASK);
    case DVC1124_REG_DSG_MASK:           return 0xFFu;
    case DVC1124_REG_CHG_MASK:           return 0xFFu;
    case DVC1124_REG_CADC_CTRL:          return (uint8_t)(DVC1124_CADC_HSFM_MASK | DVC1124_CADC_CAEW_MASK | DVC1124_CADC_CAES_MASK | DVC1124_CADC_CAMZ_MASK);
    case DVC1124_REG_CC1_TIMING:         return (uint8_t)(DVC1124_CC1_WORK_TIME_MASK | DVC1124_CC1_SLEEP_WAKE_TIME_MASK);
    case DVC1124_REG_OCD1_THR:
    case DVC1124_REG_OCC1_THR:
    case DVC1124_REG_OCD1_DLY:
    case DVC1124_REG_OCC1_DLY:
    case DVC1124_REG_OCD2_DLY:
    case DVC1124_REG_OCC2_DLY:
    case DVC1124_REG_SCD_DLY:
    case DVC1124_REG_CURRENT_WAKE:
    case DVC1124_REG_BODY_DIODE:
    case DVC1124_REG_BAL_24_17:
    case DVC1124_REG_BAL_16_9:
    case DVC1124_REG_BAL_8_1:
    case DVC1124_REG_CELL_MASK_24_17:
    case DVC1124_REG_CELL_MASK_16_9:
    case DVC1124_REG_CELL_MASK_8_5_MISC:
    case DVC1124_REG_COV_H:
    case DVC1124_REG_COV_L_DLY:
    case DVC1124_REG_CUV_H:
    case DVC1124_REG_CUV_L_DLY:
    case DVC1124_REG_GP123_MODE:
    case DVC1124_REG_GP456_MODE:
    case DVC1124_REG_INT_MASK:           return 0xFFu;
    case DVC1124_REG_OCD2:
    case DVC1124_REG_OCC2:               return (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK);
    case DVC1124_REG_SCD:                return (uint8_t)(DVC1124_SCD_ENABLE_MASK | DVC1124_SCD_THRESHOLD_MASK);
    case DVC1124_REG_CP_CTRL:            return (uint8_t)(DVC1124_CPVS_MASK | DVC1124_COW_MASK | DVC1124_CMM_MASK | DVC1124_CVS_MASK);
    case DVC1124_REG_VADC_CTRL:          return (uint8_t)(DVC1124_VADC_ENABLE_MASK | DVC1124_VADC_SYNC_MASK | DVC1124_VADC_PERIOD_MASK | DVC1124_VADC_TIME_MASK);
    case DVC1124_REG_CORE_OT:            return DVC1124_CORE_OT_THRESHOLD_MASK;
    case DVC1124_REG_I2C_WDT:            return (uint8_t)(DVC1124_V3P3_SLEEP_ENABLE_MASK | DVC1124_V3P3_WORK_ENABLE_MASK | DVC1124_V3P3_TIMEOUT_RESTART_MASK | DVC1124_I2C_WDT_TIME_MASK);
    case DVC1124_REG_TIMED_WAKE:         return DVC1124_TIMED_WAKE_TIME_MASK;
    default:                             return 0u;
    }
}

/*
 * Stable configuration fields only. Commands/status/runtime controls are excluded:
 * Alarm/CST, FET outputs, CADC manual calibration, balance and open-wire trigger.
 */
static inline uint8_t DVC1124_RegPersistentConfigMask(uint8_t reg)
{
    uint8_t mask = DVC1124_RegDocumentedWriteMask(reg);

    switch (reg)
    {
    case DVC1124_REG_ALARM:
    case DVC1124_REG_STATUS:
    case DVC1124_REG_FET_CTRL:
    case DVC1124_REG_BAL_24_17:
    case DVC1124_REG_BAL_16_9:
    case DVC1124_REG_BAL_8_1:
        return 0u;
    case DVC1124_REG_CADC_CTRL:
        return (uint8_t)(mask & (uint8_t)~DVC1124_CADC_CAMZ_MASK);
    case DVC1124_REG_CP_CTRL:
        return (uint8_t)(mask & (uint8_t)~DVC1124_COW_MASK);
    default:
        return mask;
    }
}

#endif /* DVC1124_REG_H_ */
