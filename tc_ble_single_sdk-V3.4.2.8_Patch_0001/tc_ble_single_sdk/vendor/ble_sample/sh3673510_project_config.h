#ifndef SH3673510_PROJECT_CONFIG_H_
#define SH3673510_PROJECT_CONFIG_H_

/*
 * HS-D011-10S50A board truth.
 * Schematic: HS-D011-10S50A-V1, 2026-08-21.
 * MCU: TLSR8251F512ET32.
 * AFE: SH3673510, 10 cells.
 * Current shunt: RS1..RS8 = eight 2mOhm parts in parallel -> 250uOhm.
 * SPI: PB6=MISO, PB7=MOSI, PD7=SCLK, PD2=CS-M.
 * NTC: actual fitted sensors are 10K; RN3/RN4=10M on the schematic is a drawing error.
 */
#define SH3673510_D011_CELL_COUNT              10u
#define SH3673510_D011_SHUNT_UOHM              250u
#define SH3673510_D011_NTC_NOMINAL_OHM         10000UL
#define SH3673510_D011_SPI_GROUP               SH3673520_SPI_GROUP_B6_B7_D2_D7

#define SH3673510_D011_BAT_NTC1_INDEX           0u  /* TS1, 10K */
#define SH3673510_D011_BAT_NTC2_INDEX           1u  /* TS2, 10K */
#define SH3673510_D011_HEATER_NTC_INDEX         2u  /* TS3, 10K near heater MOS; currently not used by control policy */
#define SH3673510_D011_MOS_NTC_INDEX            3u  /* TS4, 10K near charge/discharge MOS */

/* Deterministic short-circuit backup: reset-default 2*OCD2, 256us. */
#define SH3673510_D011_SC_MULTIPLIER_CODE       0u
#define SH3673510_D011_SC_DELAY_CODE            7u

/* Hardware watchdog uses its longest documented interval (WDT[1:0]=00). */
#define SH3673510_D011_WDT_CODE                 0u

/* Board GPIO truth. */
#define D011_CMNT_EN_PIN                        GPIO_PD4
#define D011_AFE_SCLK_PIN                       GPIO_PD7
#define D011_SWITCH_PIN                         GPIO_PA0
#define D011_RS485_EN_PIN                       GPIO_PA1
#define D011_SWS_PIN                            GPIO_PA7
#define D011_INT_WK_MCU_PIN                     GPIO_PB1
#define D011_HEATER_CHG_PIN                     GPIO_PB4
#define D011_HEATER_FUSE_SAFE_LEVEL             0u
#define D011_HEATER_FUSE_TRIGGER_PIN            GPIO_PB5  /* irreversible heater-fuse trigger; keep LOW until a separately validated fuse state machine authorizes firing. */
#define D011_AFE_MISO_PIN                       GPIO_PB6
#define D011_AFE_MOSI_PIN                       GPIO_PB7
#define D011_AFE_ALARM_PIN                      GPIO_PC0
#define D011_AFE_RESET_OUT_PIN                  GPIO_PC1
#define D011_SCI1_TX_PIN                        GPIO_PC2
#define D011_SCI1_RX_PIN                        GPIO_PC3
#define D011_DEBUG_LED_PIN                      GPIO_PC4
#define D011_CMNT_WK_PIN                        GPIO_PD3
#define D011_AFE_CS_PIN                         GPIO_PD2

#endif /* SH3673510_PROJECT_CONFIG_H_ */
