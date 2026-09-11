#ifndef DVC1124_PROJECT_CONFIG_H_
#define DVC1124_PROJECT_CONFIG_H_

/*
 * HS-D008 / DVC1124-2 board defaults.
 *
 * Source precedence:
 *   1. DVC1124-2 Reference Manual V1.2       -> register facts/encoding
 *   2. HS-D008 schematic/BOM                 -> board wiring/assembly
 *   3. Product/BMS parameter storage          -> protection/product policy
 *   4. DVC11XX DemoCode V1.3                 -> secondary timing/example only
 *
 * Important: this file contains DEFAULTS, not an immutable AFE preset image.
 * Runtime AFE configuration must be readable/writable through the DVC1124
 * semantic API. Protection thresholds are owned by the BMS parameter layer.
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

/* HS-D008: ten 2mOhm shunts in parallel => 0.2mOhm = 200uOhm. */
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
 * GP modes are expressed by function, not raw 0x49/0x7F magic values.
 * GP2/GP3 are routed to an external connector; product variants must override
 * these defaults when external NTCs are not assembled.
 */
#ifndef DVC1124_GP1_DEFAULT_MODE
#define DVC1124_GP1_DEFAULT_MODE             DVC1124_GP14_NTC
#endif
#ifndef DVC1124_GP2_DEFAULT_MODE
#define DVC1124_GP2_DEFAULT_MODE             DVC1124_GP236_NTC
#endif
#ifndef DVC1124_GP3_DEFAULT_MODE
#define DVC1124_GP3_DEFAULT_MODE             DVC1124_GP236_NTC
#endif
#ifndef DVC1124_GP4_DEFAULT_MODE
#define DVC1124_GP4_DEFAULT_MODE             DVC1124_GP14_NTC
#endif
#ifndef DVC1124_GP5_DEFAULT_MODE
#define DVC1124_GP5_DEFAULT_MODE             DVC1124_GP5_LOW_CHG
#endif
#ifndef DVC1124_GP6_DEFAULT_MODE
#define DVC1124_GP6_DEFAULT_MODE             DVC1124_GP6_LOW_DSG
#endif

#ifndef DVC1124_GP123_MODE_VALUE
#define DVC1124_GP123_MODE_VALUE \
    DVC1124_GP123_ENCODE(DVC1124_GP1_DEFAULT_MODE, \
                         DVC1124_GP2_DEFAULT_MODE, \
                         DVC1124_GP3_DEFAULT_MODE)
#endif
#ifndef DVC1124_GP456_MODE_VALUE
#define DVC1124_GP456_MODE_VALUE \
    DVC1124_GP456_ENCODE(DVC1124_GP4_DEFAULT_MODE, \
                         DVC1124_GP5_DEFAULT_MODE, \
                         DVC1124_GP6_DEFAULT_MODE)
#endif

/* Named operating defaults corresponding to the current validated behavior. */
#ifndef DVC1124_DEFAULT_HIGH_SIDE_FET_MASK
#define DVC1124_DEFAULT_HIGH_SIDE_FET_MASK       0u
#endif
#ifndef DVC1124_DEFAULT_CADC_WORK_ENABLE
#define DVC1124_DEFAULT_CADC_WORK_ENABLE         1u
#endif
#ifndef DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE
#define DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE 1u
#endif
#ifndef DVC1124_DEFAULT_CC1_WORK_TIME
#define DVC1124_DEFAULT_CC1_WORK_TIME            DVC1124_CC1_WORK_4MS
#endif
#ifndef DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME
#define DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME      DVC1124_CC1_SLEEP_WAKE_32MS
#endif

/* DVC1124-2 0x6D CPVS=101 means 10V. */
#ifndef DVC1124_CHARGE_PUMP_VOLTAGE_CODE
#define DVC1124_CHARGE_PUMP_VOLTAGE_CODE         DVC1124_CPVS_10V
#endif

#ifndef DVC1124_DEFAULT_CELL_MEASUREMENT_MASK
#define DVC1124_DEFAULT_CELL_MEASUREMENT_MASK    0u
#endif
#ifndef DVC1124_DEFAULT_CELL_VOLTAGE_SIGNED
#define DVC1124_DEFAULT_CELL_VOLTAGE_SIGNED      0u
#endif

#ifndef DVC1124_DEFAULT_VADC_ENABLE
#define DVC1124_DEFAULT_VADC_ENABLE              1u
#endif
#ifndef DVC1124_DEFAULT_VADC_SYNC_WITH_CC2
#define DVC1124_DEFAULT_VADC_SYNC_WITH_CC2       1u
#endif
#ifndef DVC1124_DEFAULT_VADC_PERIOD
#define DVC1124_DEFAULT_VADC_PERIOD              DVC1124_VADC_EVERY_1_CC2
#endif
#ifndef DVC1124_DEFAULT_VADC_TIME
#define DVC1124_DEFAULT_VADC_TIME                DVC1124_VADC_TIME_1P54MS
#endif

#ifndef DVC1124_DEFAULT_V3P3_SLEEP_ENABLE
#define DVC1124_DEFAULT_V3P3_SLEEP_ENABLE        1u
#endif
#ifndef DVC1124_DEFAULT_V3P3_WORK_ENABLE
#define DVC1124_DEFAULT_V3P3_WORK_ENABLE         1u
#endif
#ifndef DVC1124_DEFAULT_V3P3_TIMEOUT_RESTART
#define DVC1124_DEFAULT_V3P3_TIMEOUT_RESTART     0u
#endif
#ifndef DVC1124_DEFAULT_TIMED_WAKE
#define DVC1124_DEFAULT_TIMED_WAKE               DVC1124_TIMED_WAKE_OFF
#endif
#ifndef DVC1124_DEFAULT_INTERRUPT_MASK
#define DVC1124_DEFAULT_INTERRUPT_MASK           0x00u
#endif

/* R82 DPC reset default is 16. Keep it named so product tuning is explicit. */
#ifndef DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH
#define DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH    16u
#endif

/* COTT=0 keeps the DVC core over-temperature shutdown disabled. */
#ifndef DVC1124_DEFAULT_CORE_OT_CODE
#define DVC1124_DEFAULT_CORE_OT_CODE             0u
#endif

/*
 * Product safety defaults below remain disabled until the product thresholds
 * have been verified on hardware. They are defaults only; the runtime AFE
 * configuration service must be able to override them deliberately.
 */
#ifndef DVC1124_HW_SCD_THRESHOLD_MV
#define DVC1124_HW_SCD_THRESHOLD_MV          0u
#endif
#ifndef DVC1124_HW_SCD_DELAY_US
#define DVC1124_HW_SCD_DELAY_US              0u
#endif

/* 0 disables; otherwise CWT * 10uV. */
#ifndef DVC1124_CURRENT_WAKE_THRESHOLD_UV
#define DVC1124_CURRENT_WAKE_THRESHOLD_UV    0u
#endif

/* 0 disables; otherwise BDPT * 40uV. */
#ifndef DVC1124_BODY_DIODE_THRESHOLD_UV
#define DVC1124_BODY_DIODE_THRESHOLD_UV      0u
#endif

/* Allowed watchdog values: 0, 4, 8, 16, 32 seconds. */
#ifndef DVC1124_I2C_WATCHDOG_SECONDS
#define DVC1124_I2C_WATCHDOG_SECONDS         0u
#endif
#ifndef DVC1124_I2C_TIMEOUT_CLOSE_CHG
#define DVC1124_I2C_TIMEOUT_CLOSE_CHG        0u
#endif
#ifndef DVC1124_I2C_TIMEOUT_CLOSE_DSG
#define DVC1124_I2C_TIMEOUT_CLOSE_DSG        0u
#endif

/* Telink-side I2C robustness. Never allow an unbounded BUSY wait. */
#ifndef DVC1124_I2C_CMD_TIMEOUT_US
#define DVC1124_I2C_CMD_TIMEOUT_US           5000u
#endif
#ifndef DVC1124_I2C_RETRY_COUNT
#define DVC1124_I2C_RETRY_COUNT              3u
#endif

/* Vendor demo waits 300ms after AFE reset before normal access. */
#ifndef DVC1124_RESET_SETTLE_MS
#define DVC1124_RESET_SETTLE_MS              300u
#endif

#endif /* DVC1124_PROJECT_CONFIG_H_ */
