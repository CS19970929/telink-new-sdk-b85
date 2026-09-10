#ifndef DVC1124_PROJECT_CONFIG_H_
#define DVC1124_PROJECT_CONFIG_H_

/*
 * HS-D008 / DVC1124-2 product configuration.
 *
 * Source precedence for every DVC register definition/encoding:
 *   1. DVC1124-2 Reference Manual V1.2
 *   2. HS-D008 schematic/BOM
 *   3. DVC11XX DemoCode V1.3 (secondary implementation reference only)
 *
 * Do not introduce undocumented D-series fields from the demo into DVC1124-2
 * register definitions. Do not guess product safety thresholds.
 *
 * Telink B85 uses the 8-bit I2C transfer address including the R/W bit.
 */
#ifndef DVC1124_DEFAULT_MODEL
#define DVC1124_DEFAULT_MODEL                DVC1124_MODEL_22
#endif
#ifndef DVC1124_DEFAULT_ADDR_MODE
#define DVC1124_DEFAULT_ADDR_MODE            DVC1124_ADDR_FIXED
#endif
#ifndef DVC1124_DEFAULT_HARDWIRE_CODE
#define DVC1124_DEFAULT_HARDWIRE_CODE        0u
#endif
#ifndef DVC1124_DEFAULT_EXPLICIT_WRITE_ADDR
#define DVC1124_DEFAULT_EXPLICIT_WRITE_ADDR  0x40u
#endif

/* DVC1124-2 supports 4..24 cells. HS-D008 default assembly is 24S. */
#ifndef DVC1124_DEFAULT_CELL_COUNT
#define DVC1124_DEFAULT_CELL_COUNT           24u
#endif

/* HS-D008: ten 2 mOhm shunts in parallel => 0.2 mOhm = 200 uOhm. */
#ifndef DVC1124_DEFAULT_SHUNT_UOHM
#define DVC1124_DEFAULT_SHUNT_UOHM           200u
#endif

/* HS-D008 schematic: NTC1 -> GP4, NTC2 -> GP1. */
#ifndef DVC1124_DEFAULT_BATTERY_NTC_GP
#define DVC1124_DEFAULT_BATTERY_NTC_GP       4u
#endif
#ifndef DVC1124_DEFAULT_MOS_NTC_GP
#define DVC1124_DEFAULT_MOS_NTC_GP           1u
#endif

/*
 * GP mode register defaults for the currently documented HS-D008 assembly.
 * 0x74 = 0x49: GP1=NTC, GP2=NTC, GP3=NTC.
 * 0x75 = 0x7F: GP4=NTC, GP5=low-side CHG, GP6=low-side DSG.
 * GP2/GP3 are routed to an external connector; override 0x74 if those external
 * thermistors are not fitted on a product variant.
 */
#ifndef DVC1124_GP123_MODE_VALUE
#define DVC1124_GP123_MODE_VALUE             0x49u
#endif
#ifndef DVC1124_GP456_MODE_VALUE
#define DVC1124_GP456_MODE_VALUE             0x7Fu
#endif

/* DVC1124-2 0x6D CPVS[2:0]=101 means 10 V. */
#ifndef DVC1124_CHARGE_PUMP_VOLTAGE_CODE
#define DVC1124_CHARGE_PUMP_VOLTAGE_CODE     5u
#endif

/*
 * Product short-circuit values are not present in the current parameter set.
 * Keep hardware SCD disabled instead of inventing a production threshold.
 * V1.2: SCD threshold = SCDT * 10 mV, valid enabled codes 1..63;
 *        SCD delay = SCDD * 7.81 us, code 0..255.
 */
#ifndef DVC1124_HW_SCD_THRESHOLD_MV
#define DVC1124_HW_SCD_THRESHOLD_MV          0u
#endif
#ifndef DVC1124_HW_SCD_DELAY_US
#define DVC1124_HW_SCD_DELAY_US              0u
#endif

/* Optional sleep current wake: 0 disables; otherwise CWT * 10 uV. */
#ifndef DVC1124_CURRENT_WAKE_THRESHOLD_UV
#define DVC1124_CURRENT_WAKE_THRESHOLD_UV    0u
#endif

/* Optional body-diode threshold: 0 disables; otherwise BDPT * 40 uV. */
#ifndef DVC1124_BODY_DIODE_THRESHOLD_UV
#define DVC1124_BODY_DIODE_THRESHOLD_UV      0u
#endif

/*
 * DVC I2C watchdog. Allowed values: 0, 4, 8, 16, 32 seconds.
 * 0 disables the DVC watchdog. If enabled, choose whether an I2C timeout
 * closes CHG/DSG. These are product safety-policy decisions.
 */
#ifndef DVC1124_I2C_WATCHDOG_SECONDS
#define DVC1124_I2C_WATCHDOG_SECONDS         0u
#endif
#ifndef DVC1124_I2C_TIMEOUT_CLOSE_CHG
#define DVC1124_I2C_TIMEOUT_CLOSE_CHG        0u
#endif
#ifndef DVC1124_I2C_TIMEOUT_CLOSE_DSG
#define DVC1124_I2C_TIMEOUT_CLOSE_DSG        0u
#endif

/* Telink-side I2C robustness. Never allow an unbounded BUSY wait in the AFE path. */
#ifndef DVC1124_I2C_CMD_TIMEOUT_US
#define DVC1124_I2C_CMD_TIMEOUT_US           5000u
#endif
#ifndef DVC1124_I2C_RETRY_COUNT
#define DVC1124_I2C_RETRY_COUNT              3u
#endif

/* Vendor demo waits 300 ms after AFE reset before normal measurement/configuration. */
#ifndef DVC1124_RESET_SETTLE_MS
#define DVC1124_RESET_SETTLE_MS              300u
#endif

#endif /* DVC1124_PROJECT_CONFIG_H_ */
