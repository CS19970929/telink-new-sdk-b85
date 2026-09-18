#ifndef SH3673510_PROJECT_CONFIG_H_
#define SH3673510_PROJECT_CONFIG_H_

#include "sh3673520_reg.h"
#include "sh3673520_port.h"

/*
 * HS-D011-10S50A board / product profile.
 * Schematic: HS-D011-10S50A-V1, 2026-08-21.
 * MCU: TLSR8251F512ET32.
 * AFE: SH3673510, 10 cells.
 * Current shunt: RS1..RS8 = eight 2mOhm parts in parallel -> 250uOhm.
 * SPI: PB6=MISO, PB7=MOSI, PD7=SCLK, PD2=CS-M.
 * NTC: actual fitted sensors are 10K; RN3/RN4=10M on the schematic is a drawing error.
 *
 * The SH36735xx register definitions live in sh3673520_reg.h.  Every static
 * D011 AFE bit choice is intentionally exposed below so future products can
 * change one field without editing register-transaction code.
 */
#define SH3673510_D011_CELL_COUNT              10u
#define SH3673510_D011_SHUNT_UOHM              250u
#define SH3673510_D011_NTC_NOMINAL_OHM         10000UL
#define SH3673510_D011_SPI_GROUP               SH3673520_SPI_GROUP_B6_B7_D2_D7

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

#define SH3673510_D011_BAT_NTC1_INDEX           0u  /* TS1, 10K */
#define SH3673510_D011_BAT_NTC2_INDEX           1u  /* TS2, 10K */
#define SH3673510_D011_HEATER_NTC_INDEX         2u  /* TS3, 10K near heater MOS; reversible heater safety cutoff */
#define SH3673510_D011_MOS_NTC_INDEX            3u  /* TS4, 10K near charge/discharge MOS */

/* -------------------------------------------------------------------------
 * Static SH3673510 register profile, SH36735XX CV1.0A sections 10.2.1-10.2.9.
 * 0/1 macros correspond to the named single bit; *_CODE macros are raw field
 * codes exactly as documented by the AFE datasheet.
 * ------------------------------------------------------------------------- */

/* SCONF1 0x40: normal operating command after initialization. */
#define SH3673510_D011_SCONF1_BOOT_VALUE         SH3673520_SCONF1_NORMAL

/* SCONF2 0x41, b7..b0: LTCLR PD_EN PD_CTL PUMP_EN PDSG_CTL PDSGMOS DSGMOS CHGMOS. */
#define SH3673510_D011_LTCLR                      0u /* runtime-only flag-clear gate */
#define SH3673510_D011_PD_EN                      SH3673510_HW_PROTECT_ENABLE /* autonomous low-cell Powerdown belongs to the HW protection path */
#define SH3673510_D011_PD_CTL                     0u /* no immediate MCU Powerdown command */
#define SH3673510_D011_PUMP_EN                    1u
#define SH3673510_D011_PDSG_CTL                   0u
#define SH3673510_D011_PDSGMOS                    0u /* pre-discharge is MCU-forced/off in this product */
#define SH3673510_D011_DSGMOS_BOOT                0u /* runtime-controlled after valid samples */
#define SH3673510_D011_CHGMOS_BOOT                0u /* runtime-controlled after valid samples */
#define SH3673510_D011_SCONF2_VALUE \
    ((SH3673510_D011_LTCLR ? SH3673520_SCONF2_LTCLR_MASK : 0u) | \
     (SH3673510_D011_PD_EN ? SH3673520_SCONF2_PD_EN_MASK : 0u) | \
     (SH3673510_D011_PD_CTL ? SH3673520_SCONF2_PD_CTL_MASK : 0u) | \
     (SH3673510_D011_PUMP_EN ? SH3673520_SCONF2_PUMP_EN_MASK : 0u) | \
     (SH3673510_D011_PDSG_CTL ? SH3673520_SCONF2_PDSG_CTL_MASK : 0u) | \
     (SH3673510_D011_PDSGMOS ? SH3673520_SCONF2_PDSGMOS_MASK : 0u) | \
     (SH3673510_D011_DSGMOS_BOOT ? SH3673520_SCONF2_DSGMOS_MASK : 0u) | \
     (SH3673510_D011_CHGMOS_BOOT ? SH3673520_SCONF2_CHGMOS_MASK : 0u))

/* SCONF3 0x42: b7 reserved, b6 CGR_WK, b5:4 LD_WK, b3:2 CRLD_EN, b1 OWD_EN, b0 OWD_TRG. */
#define SH3673510_D011_CGR_WK                     1u
#define SH3673510_D011_LD_WK_CODE                 SH3673520_SCONF3_LD_WK_OFF
#define SH3673510_D011_CRLD_EN_CODE               SH3673520_SCONF3_CRLD_CPLUS_CODE
#define SH3673510_D011_OWD_EN                     0u
#define SH3673510_D011_OWD_TRG                    0u /* trigger bit is command-like; keep zero in static profile */
#define SH3673510_D011_SCONF3_VALUE \
    ((SH3673510_D011_CGR_WK ? SH3673520_SCONF3_CGR_WK_MASK : 0u) | \
     ((SH3673510_D011_LD_WK_CODE << SH3673520_SCONF3_LD_WK_SHIFT) & SH3673520_SCONF3_LD_WK_MASK) | \
     ((SH3673510_D011_CRLD_EN_CODE << SH3673520_SCONF3_CRLD_EN_SHIFT) & SH3673520_SCONF3_CRLD_EN_MASK) | \
     (SH3673510_D011_OWD_EN ? SH3673520_SCONF3_OWD_EN_MASK : 0u) | \
     (SH3673510_D011_OWD_TRG ? SH3673520_SCONF3_OWD_TRG_MASK : 0u))

/* SCONF4 0x43: b7:5 PDSGT, b4:0 CN. */
#define SH3673510_D011_PDSGT_CODE                 SH3673520_SCONF4_PDSGT_490MS
#define SH3673510_D011_SCONF4_VALUE \
    (((SH3673510_D011_PDSGT_CODE << SH3673520_SCONF4_PDSGT_SHIFT) & SH3673520_SCONF4_PDSGT_MASK) | \
     (SH3673510_D011_CELL_COUNT & SH3673520_SCONF4_CELL_COUNT_MASK))

/* SCONF5 0x44: b7:6 reserved, b5 MOS_EN, b4 OCC_EN, b3 CADC_EN, b2 WDT_EN, b1:0 WDT. */
#define SH3673510_D011_MOS_EN                     SH3673510_HW_PROTECT_ENABLE /* isolate AFE autonomous FET recovery with HW tests */
#define SH3673510_D011_OCC_EN                     SH3673510_HW_PROTECT_ENABLE
#define SH3673510_D011_CADC_EN                    1u /* current acquisition stays on in every test mode */
#define SH3673510_D011_WDT_EN                     SH3673510_HW_PROTECT_ENABLE
#define SH3673510_D011_WDT_CODE                   SH3673520_SCONF5_WDT_32S_CODE
#define SH3673510_D011_SCONF5_VALUE \
    ((SH3673510_D011_MOS_EN ? SH3673520_SCONF5_MOS_EN_MASK : 0u) | \
     (SH3673510_D011_OCC_EN ? SH3673520_SCONF5_OCC_EN_MASK : 0u) | \
     (SH3673510_D011_CADC_EN ? SH3673520_SCONF5_CADC_EN_MASK : 0u) | \
     (SH3673510_D011_WDT_EN ? SH3673520_SCONF5_WDT_EN_MASK : 0u) | \
     ((SH3673510_D011_WDT_CODE << SH3673520_SCONF5_WDT_SHIFT) & SH3673520_SCONF5_WDT_MASK))

/* SCONF6 0x45: b7..b0 TS4 TS3 TS2 TS1 SC OCD UV OV protection enables. */
#define SH3673510_D011_TS4_HW_PROTECT_EN           0u /* TS4 MOS temperature is software-protected with its own threshold */
#define SH3673510_D011_TS3_HW_PROTECT_EN           0u /* TS3 heater-MOS sensor sampled, not part of AFE common battery-temp protection */
#define SH3673510_D011_TS2_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_D011_TS1_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_D011_SC_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_D011_OCD_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_D011_UV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_D011_OV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_D011_SCONF6_VALUE \
    ((SH3673510_D011_TS4_HW_PROTECT_EN ? SH3673520_SCONF6_TS4_EN_MASK : 0u) | \
     (SH3673510_D011_TS3_HW_PROTECT_EN ? SH3673520_SCONF6_TS3_EN_MASK : 0u) | \
     (SH3673510_D011_TS2_HW_PROTECT_EN ? SH3673520_SCONF6_TS2_EN_MASK : 0u) | \
     (SH3673510_D011_TS1_HW_PROTECT_EN ? SH3673520_SCONF6_TS1_EN_MASK : 0u) | \
     (SH3673510_D011_SC_HW_PROTECT_EN ? SH3673520_SCONF6_SC_EN_MASK : 0u) | \
     (SH3673510_D011_OCD_HW_PROTECT_EN ? SH3673520_SCONF6_OCD_EN_MASK : 0u) | \
     (SH3673510_D011_UV_HW_PROTECT_EN ? SH3673520_SCONF6_UV_EN_MASK : 0u) | \
     (SH3673510_D011_OV_HW_PROTECT_EN ? SH3673520_SCONF6_OV_EN_MASK : 0u))

/* SCONF7 0x46: b7 reserved, b6 RLD, b5:4 CADCT, b3 reserved, b2:0 CDV. */
#define SH3673510_D011_RLD                        0u /* 60uA load-detect pull-up */
#define SH3673510_D011_CADCT_CODE                 SH3673520_SCONF7_CADCT_4S_CODE
#define SH3673510_D011_CDV_CODE                   4u /* datasheet reset: 742.5uV state-detect threshold */
#define SH3673510_D011_SCONF7_VALUE \
    ((SH3673510_D011_RLD ? SH3673520_SCONF7_RLD_MASK : 0u) | \
     ((SH3673510_D011_CADCT_CODE << SH3673520_SCONF7_CADCT_SHIFT) & SH3673520_SCONF7_CADCT_MASK) | \
     ((SH3673510_D011_CDV_CODE << SH3673520_SCONF7_CDV_SHIFT) & SH3673520_SCONF7_CDV_MASK))

/* OWV/ALARMH 0x47: b7:4 OWV, b3 LOADON_INT, b2 LOADOFF_INT, b1 VADC_INT, b0 CADC_INT. */
#define SH3673510_D011_OWV_CODE                    5u /* 960mV open-wire threshold */
#define SH3673510_D011_LOADON_INT                  0u
#define SH3673510_D011_LOADOFF_INT                 1u
#define SH3673510_D011_VADC_INT                    1u
#define SH3673510_D011_CADC_INT                    1u
#define SH3673510_D011_OWV_ALARMH_VALUE \
    (((SH3673510_D011_OWV_CODE << SH3673520_OWV_SHIFT) & SH3673520_OWV_MASK) | \
     (SH3673510_D011_LOADON_INT ? SH3673520_ALARMH_LOADON_INT_MASK : 0u) | \
     (SH3673510_D011_LOADOFF_INT ? SH3673520_ALARMH_LOADOFF_INT_MASK : 0u) | \
     (SH3673510_D011_VADC_INT ? SH3673520_ALARMH_VADC_INT_MASK : 0u) | \
     (SH3673510_D011_CADC_INT ? SH3673520_ALARMH_CADC_INT_MASK : 0u))

/* ALARML 0x48: b7..b0 WK WDT OWD TEMP OCC OCD UV OV interrupt pulse enables. */
#define SH3673510_D011_WK_INT                      1u
#define SH3673510_D011_WDT_INT                     1u
#define SH3673510_D011_OWD_INT                     1u
#define SH3673510_D011_TEMP_INT                    1u
#define SH3673510_D011_OCC_INT                     1u
#define SH3673510_D011_OCD_INT                     1u
#define SH3673510_D011_UV_INT                      1u
#define SH3673510_D011_OV_INT                      1u
#define SH3673510_D011_ALARML_VALUE \
    ((SH3673510_D011_WK_INT ? SH3673520_ALARML_WK_INT_MASK : 0u) | \
     (SH3673510_D011_WDT_INT ? SH3673520_ALARML_WDT_INT_MASK : 0u) | \
     (SH3673510_D011_OWD_INT ? SH3673520_ALARML_OWD_INT_MASK : 0u) | \
     (SH3673510_D011_TEMP_INT ? SH3673520_ALARML_TEMP_INT_MASK : 0u) | \
     (SH3673510_D011_OCC_INT ? SH3673520_ALARML_OCC_INT_MASK : 0u) | \
     (SH3673510_D011_OCD_INT ? SH3673520_ALARML_OCD_INT_MASK : 0u) | \
     (SH3673510_D011_UV_INT ? SH3673520_ALARML_UV_INT_MASK : 0u) | \
     (SH3673510_D011_OV_INT ? SH3673520_ALARML_OV_INT_MASK : 0u))

/* Hardware short-circuit backup: SCV=2*VOCD2 and SCT=256us. */
#define SH3673510_D011_SC_MULTIPLIER_CODE           0u
#define SH3673510_D011_SC_DELAY_CODE                7u

/* Board GPIO truth. */
#define D011_CMNT_EN_PIN                        GPIO_PD4
#define D011_AFE_SCLK_PIN                       GPIO_PD7
#define D011_SWITCH_PIN                         GPIO_PA0
#define D011_RS485_EN_PIN                       GPIO_PA1
#define D011_SWS_PIN                            GPIO_PA7
#define D011_INT_WK_MCU_PIN                     GPIO_PB1
#define D011_HEATER_CHG_PIN                     GPIO_PB4
#define D011_HEATER_FUSE_SAFE_LEVEL               0u
#define D011_HEATER_FUSE_TRIGGER_PIN              GPIO_PB5  /* irreversible heater-fuse trigger; keep LOW until a separately validated fuse state machine authorizes firing. */
#define D011_AFE_MISO_PIN                       GPIO_PB6
#define D011_AFE_MOSI_PIN                       GPIO_PB7
#define D011_AFE_ALARM_PIN                      GPIO_PC0
#define D011_AFE_RESET_OUT_PIN                  GPIO_PC1
#define D011_SCI1_TX_PIN                        GPIO_PC2
#define D011_SCI1_RX_PIN                        GPIO_PC3
#define D011_DEBUG_LED_PIN                      GPIO_PC4
#ifndef D011_DEBUG_LED_ENABLE
#define D011_DEBUG_LED_ENABLE                   0u
#endif
#if (D011_DEBUG_LED_ENABLE > 1u)
#error "D011_DEBUG_LED_ENABLE must be 0 or 1"
#endif
#define D011_CMNT_WK_PIN                        GPIO_PD3
#define D011_AFE_CS_PIN                         GPIO_PD2

#endif /* SH3673510_PROJECT_CONFIG_H_ */