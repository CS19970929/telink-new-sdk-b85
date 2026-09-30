#ifndef SH3673510_PROJECT_CONFIG_H_
#define SH3673510_PROJECT_CONFIG_H_

#include "sh3673520_reg.h"
#include "sh3673520_port.h"

/* Shared SH3673510 register formulas. Physical values come from the selected product. */
#include "bms_sh3673510_config.h"
#define SH3673510_BOARD_NTC_NOMINAL_OHM 10000UL
#define BMS_BOARD_HEATER_CHG_PIN                     GPIO_PB4
#define BMS_BOARD_HEATER_FUSE_TRIGGER_PIN              GPIO_PB5  /* irreversible heater-fuse trigger; keep LOW until a separately validated fuse state machine authorizes firing. */
#define BMS_BOARD_HEATER_FUSE_SAFE_LEVEL               0u

/* Capabilities must be backed by each product schematic/BOM. */
#define SH3673510_BOARD_SPI_GROUP               SH3673520_SPI_GROUP_B6_B7_D2_D7

/*
 * Protection-path isolation switches.
 * 1/1: production behavior (software + AFE hardware protection).
 * 1/0: software-protection-only bench test.
 * 0/1: AFE-hardware-protection-only bench test.
 * 0/0: measurement/communication debug only; no threshold protection.
 *
 * Each path keeps its own trip + recovery logic together. Do not ship a
 * production build with either path disabled.
 */
#ifndef SH3673510_SW_PROTECT_ENABLE
#define SH3673510_SW_PROTECT_ENABLE             1u
#endif
#ifndef SH3673510_HW_PROTECT_ENABLE
#define SH3673510_HW_PROTECT_ENABLE             1u
#endif
#if ((SH3673510_SW_PROTECT_ENABLE > 1u) || (SH3673510_HW_PROTECT_ENABLE > 1u))
#error "SH3673510 protection enable macros must be 0 or 1"
#endif

#define SH3673510_BOARD_BAT_NTC1_INDEX           0u  /* TS1, 10K-3435 */
#define SH3673510_BOARD_BAT_NTC2_INDEX           1u  /* TS2, 10K-3435 */
#define SH3673510_BOARD_HEATER_NTC_INDEX         2u  /* TS3-NC on D014; never enables heater policy */
#define SH3673510_BOARD_MOS_NTC_INDEX            3u  /* TS4 MOS 10K-3435; schematic RN4 text differs from fitted BOM */

/* -------------------------------------------------------------------------
 * Static SH3673510 register profile, SH36735XX CV1.0A sections 10.2.1-10.2.9.
 * 0/1 macros correspond to the named single bit; *_CODE macros are raw field
 * codes exactly as documented by the AFE datasheet.
 * ------------------------------------------------------------------------- */

/* SCONF1 0x40: normal operating command after initialization. */
#define SH3673510_BOARD_SCONF1_BOOT_VALUE         SH3673520_SCONF1_NORMAL

/* SCONF2 0x41, b7..b0: LTCLR PD_EN PD_CTL PUMP_EN PDSG_CTL PDSGMOS DSGMOS CHGMOS. */
#define SH3673510_BOARD_LTCLR                      0u /* runtime-only flag-clear gate */
#define SH3673510_BOARD_PD_EN                      SH3673510_HW_PROTECT_ENABLE /* autonomous low-cell Powerdown belongs to the HW protection path */
#define SH3673510_BOARD_PD_CTL                     0u /* no immediate MCU Powerdown command */
#define SH3673510_BOARD_PUMP_EN                    1u
#define SH3673510_BOARD_PDSG_CTL                   0u
#define SH3673510_BOARD_PDSGMOS                    0u /* pre-discharge is MCU-forced/off in this product */
#define SH3673510_BOARD_DSGMOS_BOOT                0u /* runtime-controlled after valid samples */
#define SH3673510_BOARD_CHGMOS_BOOT                0u /* runtime-controlled after valid samples */
#define SH3673510_BOARD_SCONF2_VALUE \
    ((SH3673510_BOARD_LTCLR ? SH3673520_SCONF2_LTCLR_MASK : 0u) | \
     (SH3673510_BOARD_PD_EN ? SH3673520_SCONF2_PD_EN_MASK : 0u) | \
     (SH3673510_BOARD_PD_CTL ? SH3673520_SCONF2_PD_CTL_MASK : 0u) | \
     (SH3673510_BOARD_PUMP_EN ? SH3673520_SCONF2_PUMP_EN_MASK : 0u) | \
     (SH3673510_BOARD_PDSG_CTL ? SH3673520_SCONF2_PDSG_CTL_MASK : 0u) | \
     (SH3673510_BOARD_PDSGMOS ? SH3673520_SCONF2_PDSGMOS_MASK : 0u) | \
     (SH3673510_BOARD_DSGMOS_BOOT ? SH3673520_SCONF2_DSGMOS_MASK : 0u) | \
     (SH3673510_BOARD_CHGMOS_BOOT ? SH3673520_SCONF2_CHGMOS_MASK : 0u))

/* SCONF3 0x42: b7 reserved, b6 CGR_WK, b5:4 LD_WK, b3:2 CRLD_EN, b1 OWD_EN, b0 OWD_TRG. */
#define SH3673510_BOARD_CGR_WK                     1u
#define SH3673510_BOARD_LD_WK_CODE                 SH3673520_SCONF3_LD_WK_OFF
#define SH3673510_BOARD_CRLD_EN_CODE               SH3673520_SCONF3_CRLD_CPLUS_CODE
#define SH3673510_BOARD_OWD_EN                     0u
#define SH3673510_BOARD_OWD_TRG                    0u /* trigger bit is command-like; keep zero in static profile */
#define SH3673510_BOARD_SCONF3_VALUE \
    ((SH3673510_BOARD_CGR_WK ? SH3673520_SCONF3_CGR_WK_MASK : 0u) | \
     ((SH3673510_BOARD_LD_WK_CODE << SH3673520_SCONF3_LD_WK_SHIFT) & SH3673520_SCONF3_LD_WK_MASK) | \
     ((SH3673510_BOARD_CRLD_EN_CODE << SH3673520_SCONF3_CRLD_EN_SHIFT) & SH3673520_SCONF3_CRLD_EN_MASK) | \
     (SH3673510_BOARD_OWD_EN ? SH3673520_SCONF3_OWD_EN_MASK : 0u) | \
     (SH3673510_BOARD_OWD_TRG ? SH3673520_SCONF3_OWD_TRG_MASK : 0u))

/* SCONF4 0x43: b7:5 PDSGT, b4:0 CN. */
#define SH3673510_BOARD_PDSGT_CODE                 SH3673520_SCONF4_PDSGT_490MS
#define SH3673510_BOARD_SCONF4_VALUE \
    (((SH3673510_BOARD_PDSGT_CODE << SH3673520_SCONF4_PDSGT_SHIFT) & SH3673520_SCONF4_PDSGT_MASK) | \
     (SH3673510_BOARD_CELL_COUNT & SH3673520_SCONF4_CELL_COUNT_MASK))

/* SCONF5 0x44: b7:6 reserved, b5 MOS_EN, b4 OCC_EN, b3 CADC_EN, b2 WDT_EN, b1:0 WDT. */
#define SH3673510_BOARD_MOS_EN                     SH3673510_HW_PROTECT_ENABLE /* isolate AFE autonomous FET recovery with HW tests */
#define SH3673510_BOARD_OCC_EN                     SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_CADC_EN                    1u /* current acquisition stays on in every test mode */
#define SH3673510_BOARD_WDT_EN                     SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_WDT_CODE                   SH3673520_SCONF5_WDT_32S_CODE
#define SH3673510_BOARD_SCONF5_VALUE \
    ((SH3673510_BOARD_MOS_EN ? SH3673520_SCONF5_MOS_EN_MASK : 0u) | \
     (SH3673510_BOARD_OCC_EN ? SH3673520_SCONF5_OCC_EN_MASK : 0u) | \
     (SH3673510_BOARD_CADC_EN ? SH3673520_SCONF5_CADC_EN_MASK : 0u) | \
     (SH3673510_BOARD_WDT_EN ? SH3673520_SCONF5_WDT_EN_MASK : 0u) | \
     ((SH3673510_BOARD_WDT_CODE << SH3673520_SCONF5_WDT_SHIFT) & SH3673520_SCONF5_WDT_MASK))

/* SCONF6 0x45: b7..b0 TS4 TS3 TS2 TS1 SC OCD UV OV protection enables. */
#define SH3673510_BOARD_TS4_HW_PROTECT_EN           0u /* AFE TS4 shares battery OTC/UTC thresholds; MOS-only OTP uses software policy */
#define SH3673510_BOARD_TS3_HW_PROTECT_EN           0u /* D014 TS3 is NC */
#define SH3673510_BOARD_TS2_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_TS1_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_SC_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_OCD_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_UV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_OV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_SCONF6_VALUE \
    ((SH3673510_BOARD_TS4_HW_PROTECT_EN ? SH3673520_SCONF6_TS4_EN_MASK : 0u) | \
     (SH3673510_BOARD_TS3_HW_PROTECT_EN ? SH3673520_SCONF6_TS3_EN_MASK : 0u) | \
     (SH3673510_BOARD_TS2_HW_PROTECT_EN ? SH3673520_SCONF6_TS2_EN_MASK : 0u) | \
     (SH3673510_BOARD_TS1_HW_PROTECT_EN ? SH3673520_SCONF6_TS1_EN_MASK : 0u) | \
     (SH3673510_BOARD_SC_HW_PROTECT_EN ? SH3673520_SCONF6_SC_EN_MASK : 0u) | \
     (SH3673510_BOARD_OCD_HW_PROTECT_EN ? SH3673520_SCONF6_OCD_EN_MASK : 0u) | \
     (SH3673510_BOARD_UV_HW_PROTECT_EN ? SH3673520_SCONF6_UV_EN_MASK : 0u) | \
     (SH3673510_BOARD_OV_HW_PROTECT_EN ? SH3673520_SCONF6_OV_EN_MASK : 0u))

/* SCONF7 0x46: b7 reserved, b6 RLD, b5:4 CADCT, b3 reserved, b2:0 CDV. */
#define SH3673510_BOARD_RLD                        0u /* 60uA load-detect pull-up */
#define SH3673510_BOARD_CADCT_CODE                 SH3673520_SCONF7_CADCT_4S_CODE
#define SH3673510_BOARD_CDV_CODE                   4u /* datasheet reset: 742.5uV state-detect threshold */
#define SH3673510_BOARD_SCONF7_VALUE \
    ((SH3673510_BOARD_RLD ? SH3673520_SCONF7_RLD_MASK : 0u) | \
     ((SH3673510_BOARD_CADCT_CODE << SH3673520_SCONF7_CADCT_SHIFT) & SH3673520_SCONF7_CADCT_MASK) | \
     ((SH3673510_BOARD_CDV_CODE << SH3673520_SCONF7_CDV_SHIFT) & SH3673520_SCONF7_CDV_MASK))

/* OWV/ALARMH 0x47: b7:4 OWV, b3 LOADON_INT, b2 LOADOFF_INT, b1 VADC_INT, b0 CADC_INT. */
#define SH3673510_BOARD_OWV_CODE                    5u /* 960mV open-wire threshold */
#define SH3673510_BOARD_LOADON_INT                  0u
#define SH3673510_BOARD_LOADOFF_INT                 1u
#define SH3673510_BOARD_VADC_INT                    1u
#define SH3673510_BOARD_CADC_INT                    1u
#define SH3673510_BOARD_OWV_ALARMH_VALUE \
    (((SH3673510_BOARD_OWV_CODE << SH3673520_OWV_SHIFT) & SH3673520_OWV_MASK) | \
     (SH3673510_BOARD_LOADON_INT ? SH3673520_ALARMH_LOADON_INT_MASK : 0u) | \
     (SH3673510_BOARD_LOADOFF_INT ? SH3673520_ALARMH_LOADOFF_INT_MASK : 0u) | \
     (SH3673510_BOARD_VADC_INT ? SH3673520_ALARMH_VADC_INT_MASK : 0u) | \
     (SH3673510_BOARD_CADC_INT ? SH3673520_ALARMH_CADC_INT_MASK : 0u))

/* ALARML 0x48: b7..b0 WK WDT OWD TEMP OCC OCD UV OV interrupt pulse enables. */
#define SH3673510_BOARD_WK_INT                      1u
#define SH3673510_BOARD_WDT_INT                     1u
#define SH3673510_BOARD_OWD_INT                     1u
#define SH3673510_BOARD_TEMP_INT                    1u
#define SH3673510_BOARD_OCC_INT                     1u
#define SH3673510_BOARD_OCD_INT                     1u
#define SH3673510_BOARD_UV_INT                      1u
#define SH3673510_BOARD_OV_INT                      1u
#define SH3673510_BOARD_ALARML_VALUE \
    ((SH3673510_BOARD_WK_INT ? SH3673520_ALARML_WK_INT_MASK : 0u) | \
     (SH3673510_BOARD_WDT_INT ? SH3673520_ALARML_WDT_INT_MASK : 0u) | \
     (SH3673510_BOARD_OWD_INT ? SH3673520_ALARML_OWD_INT_MASK : 0u) | \
     (SH3673510_BOARD_TEMP_INT ? SH3673520_ALARML_TEMP_INT_MASK : 0u) | \
     (SH3673510_BOARD_OCC_INT ? SH3673520_ALARML_OCC_INT_MASK : 0u) | \
     (SH3673510_BOARD_OCD_INT ? SH3673520_ALARML_OCD_INT_MASK : 0u) | \
     (SH3673510_BOARD_UV_INT ? SH3673520_ALARML_UV_INT_MASK : 0u) | \
     (SH3673510_BOARD_OV_INT ? SH3673520_ALARML_OV_INT_MASK : 0u))

/* Hardware short-circuit backup: SCV=2*VOCD2 and SCT=256us. */
#define SH3673510_BOARD_SC_MULTIPLIER_CODE           0u
#define SH3673510_BOARD_SC_DELAY_CODE                7u

/*
 * D014 independent AFE hardware-protection requested defaults.
 * Units: mV, ms, 0.1A, us, and (degC+40)*10 as named.
 * These values intentionally do not alias the software First/Second/Third
 * protection table in param.h.
 */
#define SH3673510_HW_DEFAULT_COV_MV                3750u
#define SH3673510_HW_DEFAULT_COV_DELAY_MS          1000u
#define SH3673510_HW_DEFAULT_COV_RECOVER_MV        3500u
#define SH3673510_HW_DEFAULT_COV_RECOVER_MS        1000u
#define SH3673510_HW_DEFAULT_CUV_MV                3000u
#define SH3673510_HW_DEFAULT_CUV_DELAY_MS          10000u
#define SH3673510_HW_DEFAULT_CUV_RECOVER_MV        3100u
#define SH3673510_HW_DEFAULT_CUV_RECOVER_MS        10000u
#define SH3673510_HW_DEFAULT_OCD1_A10              100u
#define SH3673510_HW_DEFAULT_OCD1_DELAY_MS         100u
#define SH3673510_HW_DEFAULT_OCD2_A10              150u
#define SH3673510_HW_DEFAULT_OCD2_DELAY_MS         100u
#define SH3673510_HW_DEFAULT_OCD_RECOVER_A10       100u
#define SH3673510_HW_DEFAULT_OCD_RECOVER_MS        2000u
#define SH3673510_HW_DEFAULT_OCC1_A10              100u
#define SH3673510_HW_DEFAULT_OCC1_DELAY_MS         100u
#define SH3673510_HW_DEFAULT_OCC_RECOVER_A10       100u
#define SH3673510_HW_DEFAULT_OCC_RECOVER_MS        100u
#define SH3673510_HW_DEFAULT_SC_A10                300u
#define SH3673510_HW_DEFAULT_SC_DELAY_US           256u
#define SH3673510_HW_DEFAULT_SC_RECOVER_MS         2000u
#define SH3673510_HW_DEFAULT_CHG_OT_X10            950u
#define SH3673510_HW_DEFAULT_CHG_OT_RECOVER_X10    900u
#define SH3673510_HW_DEFAULT_CHG_UT_X10            400u
#define SH3673510_HW_DEFAULT_CHG_UT_RECOVER_X10    430u
#define SH3673510_HW_DEFAULT_DSG_OT_X10            1000u
#define SH3673510_HW_DEFAULT_DSG_OT_RECOVER_X10    900u
#define SH3673510_HW_DEFAULT_DSG_UT_X10            200u
#define SH3673510_HW_DEFAULT_DSG_UT_RECOVER_X10    300u
#define SH3673510_HW_DEFAULT_TEMP_RECOVER_MS       1000u

/* D014 board GPIO truth from HS-D014-8S15A schematic. */
#define BMS_BOARD_CMNT_EN_PIN                        GPIO_PD4
#define BMS_BOARD_AFE_SCLK_PIN                       GPIO_PD7
#define BMS_BOARD_SWITCH_PIN                         GPIO_PA0
#define BMS_BOARD_RS485_EN_PIN                       GPIO_PA1
#define BMS_BOARD_SWS_PIN                            GPIO_PA7
#define BMS_BOARD_INT_WK_MCU_PIN                     GPIO_PB1
#define BMS_BOARD_AFE_MISO_PIN                       GPIO_PB6
#define BMS_BOARD_AFE_MOSI_PIN                       GPIO_PB7
#define BMS_BOARD_AFE_ALARM_PIN                      GPIO_PC0
#define BMS_BOARD_AFE_RESET_OUT_PIN                  GPIO_PC1
#define BMS_BOARD_SCI1_TX_PIN                        GPIO_PC2
#define BMS_BOARD_SCI1_RX_PIN                        GPIO_PC3
#define BMS_BOARD_DEBUG_LED_PIN                      GPIO_PC4
#define BMS_BOARD_CMNT_WK_PIN                        GPIO_PD3
#define BMS_BOARD_AFE_CS_PIN                         GPIO_PD2

#if (BMS_BOARD_DEBUG_LED_ENABLE > 1u)
#error "BMS_BOARD_DEBUG_LED_ENABLE must be 0 or 1"
#endif

#if (BMS_PRODUCT_ID == 14u) && SH3673510_PRODUCT_HEATER_SUPPORTED
#error "D014 heater is not schematic-verified; do not enable it without a new board review"
#endif


#if BMS_PRODUCTION_BUILD && (!SH3673510_SW_PROTECT_ENABLE || !SH3673510_HW_PROTECT_ENABLE || BMS_BOARD_DEBUG_LED_ENABLE)
#error "Production requires software/hardware protection and no debug LED"
#endif
/* Preserve the former fixed-UART mux gate. Removing SIF must not enable
 * suspend/deep sleep; changing this policy needs a separate hardware review. */
#define SH3673510_FIXED_UART_BLOCKS_PM 1u

#endif /* SH3673510_PROJECT_CONFIG_H_ */
