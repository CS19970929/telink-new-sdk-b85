#pragma once
/* Product inputs; register composition is in the shared SH backend. */
#ifndef BMS_BOARD_DEBUG_LED_ENABLE
#define BMS_BOARD_DEBUG_LED_ENABLE                   0u
#endif
#define SH3673510_BOARD_CELL_COUNT               8u
#define SH3673510_BOARD_SHUNT_UOHM              667u
#define SH3673510_PRODUCT_BALANCE_SUPPORTED      1u
#define SH3673510_PRODUCT_HEATER_SUPPORTED       0u
#define SH3673510_PRODUCT_HEATER_NTC_SUPPORTED   0u
#define SH3673510_PRODUCT_MOS_NTC_SUPPORTED 1u

/* D014 schematic: TS3 NC; fitted TS4 MOS 10K-3435 (RN4 drawing discrepancy).
 * Static AFE inputs and independent HW defaults; no software threshold aliases. */
#define SH3673510_BOARD_NTC_NOMINAL_OHM 10000UL
#define SH3673510_BOARD_SPI_GROUP               SH3673520_SPI_GROUP_B6_B7_D2_D7
#define SH3673510_BOARD_BAT_NTC1_INDEX           0u  /* TS1, 10K-3435 */
#define SH3673510_BOARD_BAT_NTC2_INDEX           1u  /* TS2, 10K-3435 */
#define SH3673510_BOARD_HEATER_NTC_INDEX         2u  /* TS3 heater sensor role; only used when supported */
#define SH3673510_BOARD_MOS_NTC_INDEX            3u  /* TS4 MOS sensor role; only used when supported */
#define SH3673510_BOARD_SCONF1_BOOT_VALUE         SH3673520_SCONF1_NORMAL
#define SH3673510_BOARD_LTCLR                      0u /* runtime-only flag-clear gate */
#define SH3673510_BOARD_PD_EN                      SH3673510_HW_PROTECT_ENABLE /* autonomous low-cell Powerdown belongs to the HW protection path */
#define SH3673510_BOARD_PD_CTL                     0u /* no immediate MCU Powerdown command */
#define SH3673510_BOARD_PUMP_EN                    1u
#define SH3673510_BOARD_PDSG_CTL                   0u
#define SH3673510_BOARD_PDSGMOS                    0u /* pre-discharge is MCU-forced/off in this product */
#define SH3673510_BOARD_DSGMOS_BOOT                0u /* runtime-controlled after valid samples */
#define SH3673510_BOARD_CHGMOS_BOOT                0u /* runtime-controlled after valid samples */
#define SH3673510_BOARD_CGR_WK                     1u
#define SH3673510_BOARD_LD_WK_CODE                 SH3673520_SCONF3_LD_WK_OFF
#define SH3673510_BOARD_CRLD_EN_CODE               SH3673520_SCONF3_CRLD_CPLUS_CODE
#define SH3673510_BOARD_OWD_EN                     0u
#define SH3673510_BOARD_OWD_TRG                    0u /* trigger bit is command-like; keep zero in static profile */
#define SH3673510_BOARD_PDSGT_CODE                 SH3673520_SCONF4_PDSGT_490MS
#define SH3673510_BOARD_MOS_EN                     SH3673510_HW_PROTECT_ENABLE /* isolate AFE autonomous FET recovery with HW tests */
#define SH3673510_BOARD_OCC_EN                     SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_CADC_EN                    1u /* current acquisition stays on in every test mode */
#define SH3673510_BOARD_WDT_EN                     SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_WDT_CODE                   SH3673520_SCONF5_WDT_32S_CODE
#define SH3673510_BOARD_TS4_HW_PROTECT_EN           0u /* AFE TS4 shares battery OTC/UTC thresholds; MOS-only OTP uses software policy */
#define SH3673510_BOARD_TS3_HW_PROTECT_EN           0u /* independent heater policy; no common battery threshold */
#define SH3673510_BOARD_TS2_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_TS1_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_SC_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_OCD_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_UV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_OV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_RLD                        0u /* 60uA load-detect pull-up */
#define SH3673510_BOARD_CADCT_CODE                 SH3673520_SCONF7_CADCT_4S_CODE
#define SH3673510_BOARD_CDV_CODE                   4u /* datasheet reset: 742.5uV state-detect threshold */
#define SH3673510_BOARD_OWV_CODE                    5u /* 960mV open-wire threshold */
#define SH3673510_BOARD_LOADON_INT                  0u
#define SH3673510_BOARD_LOADOFF_INT                 1u
#define SH3673510_BOARD_VADC_INT                    1u
#define SH3673510_BOARD_CADC_INT                    1u
#define SH3673510_BOARD_WK_INT                      1u
#define SH3673510_BOARD_WDT_INT                     1u
#define SH3673510_BOARD_OWD_INT                     1u
#define SH3673510_BOARD_TEMP_INT                    1u
#define SH3673510_BOARD_OCC_INT                     1u
#define SH3673510_BOARD_OCD_INT                     1u
#define SH3673510_BOARD_UV_INT                      1u
#define SH3673510_BOARD_OV_INT                      1u
#define SH3673510_BOARD_SC_MULTIPLIER_CODE           0u
#define SH3673510_BOARD_SC_DELAY_CODE                7u
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

/* Preserve fixed-UART low-power gate until hardware validation. */
#define SH3673510_FIXED_UART_BLOCKS_PM 1u

#if SH3673510_PRODUCT_HEATER_SUPPORTED || SH3673510_PRODUCT_HEATER_NTC_SUPPORTED
#error "D014 has no fitted heater or TS3 sensor"
#endif
