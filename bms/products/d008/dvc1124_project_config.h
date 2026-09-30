#ifndef DVC1124_PROJECT_CONFIG_H_
#define DVC1124_PROJECT_CONFIG_H_

#include "d008_product_profile.h"

/*
 * HS-D008 / DVC1124-2 fixed board and fail-safe configuration.
 *
 * Source precedence:
 *   1. DVC1124-2 Reference Manual V1.2       -> register facts/encoding
 *   2. HS-D008 schematic/BOM                 -> board wiring/assembly
 *   3. D008 product profile                  -> assembled cell count/chemistry
 *   4. This file                             -> fixed DVC board/fail-safe policy
 *   5. Runtime Flash parameters              -> protection thresholds only
 *   6. DVC11XX DemoCode V1.3                 -> secondary timing/example only
 *
 * IMPORTANT OWNERSHIP RULE:
 * Values in this file are firmware-owned product policy. They are applied after
 * every AFE reset and are not restored from historical DVC operating-config
 * Flash. The 0x2800 semantic window exposes them for diagnostics only.
 *
 * Runtime-persistent protection parameters are owned separately by:
 *   - g_tParam.protect: software protection
 *   - bms_afe_hw_profile_t: DVC hardware protection thresholds/delays
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

/* Physical series count comes from the explicitly selected D008 assembly. */
#ifndef DVC1124_DEFAULT_CELL_COUNT
#define DVC1124_DEFAULT_CELL_COUNT           D008_PRODUCT_CELL_COUNT
#endif

/* HS-D008: ten 2mOhm shunts in parallel => 0.2mOhm = 200uOhm. */
#ifndef DVC1124_DEFAULT_SHUNT_UOHM
#define DVC1124_DEFAULT_SHUNT_UOHM           200u
#endif

#ifndef BMS_PRODUCTION_BUILD
#define BMS_PRODUCTION_BUILD                 0u
#endif
#if (BMS_PRODUCTION_BUILD > 1u)
#error "BMS_PRODUCTION_BUILD must be 0 or 1"
#endif

/*
 * Protection-path isolation switches.
 *
 * 1/1: production behavior (software + DVC hardware protection).
 * 1/0: software-protection-only bench test; DVC autonomous HW protection and
 *      fail-safe sources are deliberately disabled.
 * 0/1: DVC hardware with software voltage/current disabled; temperature stays independent.
 * 0/0: voltage/current threshold isolation; temperature defaults ON.
 * DVC1124_SW_TEMP_PROTECT_ENABLE controls all software temperature protections.
 *
 * Measurement, I2C communication and CHGF/DSGF sampling remain active in all
 * modes. The requested AFE hardware protection profile remains stored even when
 * HW=0; only its application to DVC is disabled in that bench mode.
 */
#ifndef DVC1124_SW_PROTECT_ENABLE
#define DVC1124_SW_PROTECT_ENABLE            1u
#endif
/* Battery OTP/UTP, MOS OTP and required NTC validity remain independent
 * of voltage/current isolation. No external-temperature HW backup exists. */
#ifndef DVC1124_SW_TEMP_PROTECT_ENABLE
#define DVC1124_SW_TEMP_PROTECT_ENABLE       1u
#endif
#ifndef DVC1124_HW_PROTECT_ENABLE
#define DVC1124_HW_PROTECT_ENABLE            1u
#endif
#if ((DVC1124_SW_PROTECT_ENABLE > 1u) || (DVC1124_HW_PROTECT_ENABLE > 1u) || (DVC1124_SW_TEMP_PROTECT_ENABLE > 1u))
#error "DVC1124 protection enable macros must be 0 or 1"
#endif
#if BMS_PRODUCTION_BUILD && ((DVC1124_SW_PROTECT_ENABLE != 1u) || \
                             (DVC1124_HW_PROTECT_ENABLE != 1u) || \
                             (DVC1124_SW_TEMP_PROTECT_ENABLE != 1u))
#error "Production build requires software, hardware and temperature protection enabled"
#endif

/*
 * HS-D008 final temperature-role definition:
 *   GP1 = heater MOS / heater circuit temperature
 *   GP2 = battery temperature #1
 *   GP3 = battery temperature #2
 *   GP4 = power MOS temperature
 *
 * battery_ntc_gp in dvc1124_config_t remains the primary battery channel for
 * legacy diagnostics. Product protection always uses both GP2 and GP3.
 */
#ifndef DVC1124_DEFAULT_HEATER_NTC_GP
#define DVC1124_DEFAULT_HEATER_NTC_GP        1u
#endif
#ifndef DVC1124_DEFAULT_BATTERY_NTC_GP
#define DVC1124_DEFAULT_BATTERY_NTC_GP       2u
#endif
#ifndef DVC1124_DEFAULT_BATTERY_NTC2_GP
#define DVC1124_DEFAULT_BATTERY_NTC2_GP      3u
#endif
#ifndef DVC1124_DEFAULT_MOS_NTC_GP
#define DVC1124_DEFAULT_MOS_NTC_GP           4u
#endif

/*
 * Independent irreversible heater-circuit fail-safe.
 * Temperature encoding is (degC + 40) * 10, therefore 95C == 1350.
 * If software commands the heater OFF while GP1 remains at/above 95C for 10s,
 * the heater circuit is considered stuck/abnormal and PD4/MCC-EN-RF is fired.
 * This is intentionally independent of the normal power-MOS OTP parameters.
 */
#ifndef DVC1124_HEATER_OFF_FAULT_TEMP_X10
#define DVC1124_HEATER_OFF_FAULT_TEMP_X10    1350u
#endif
#ifndef DVC1124_HEATER_OFF_FAULT_CONFIRM_MS
#define DVC1124_HEATER_OFF_FAULT_CONFIRM_MS  10000u
#endif

/*
 * GP modes are firmware-owned board routing. GP1..GP4 are populated NTC inputs
 * with product roles defined above; GP5/GP6 are low-side FET controls.
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

/* D008 uses GP5/GP6 low-side CHG/DSG; unused high-side FET drive is masked. */
#ifndef DVC1124_DEFAULT_HIGH_SIDE_FET_MASK
#define DVC1124_DEFAULT_HIGH_SIDE_FET_MASK       1u
#endif
#ifndef DVC1124_DEFAULT_CADC_WORK_ENABLE
#define DVC1124_DEFAULT_CADC_WORK_ENABLE         1u
#endif

/* CWT=0 disables current wake; keep CAES consistent with that fixed policy. */
#ifndef DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE
#define DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE 0u
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

/* D008 does not currently consume a DVC GP interrupt: mask all sources. */
#ifndef DVC1124_DEFAULT_INTERRUPT_MASK
#define DVC1124_DEFAULT_INTERRUPT_MASK           0xFFu
#endif

/* R82 DPC reset default is 16. */
#ifndef DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH
#define DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH    16u
#endif

/*
 * DVC 0x53/0x54 are mask registers. HS-D008 is common-port, therefore DBDM /
 * CBDM are cleared so R81 AUTO_DIODE (10b) can reopen the protected FET after
 * current reversal. DWM/CWM are overlaid below by the compile-time I2C timeout
 * close policy. Mask semantics: 0 = source may act, 1 = source is masked.
 */
#ifndef DVC1124_DEFAULT_DSG_MASK_POLICY
#define DVC1124_DEFAULT_DSG_MASK_POLICY \
    ((uint8_t)(DVC1124_DSG_MASK_RESET & (uint8_t)~DVC1124_DSGMASK_DBDM_MASK))
#endif
#ifndef DVC1124_DEFAULT_CHG_MASK_POLICY
#define DVC1124_DEFAULT_CHG_MASK_POLICY \
    ((uint8_t)(DVC1124_CHG_MASK_RESET & (uint8_t)~DVC1124_CHGMASK_CBDM_MASK))
#endif

/* COTT=0 keeps DVC core over-temperature shutdown disabled. */
#ifndef DVC1124_DEFAULT_CORE_OT_CODE
#define DVC1124_DEFAULT_CORE_OT_CODE             0u
#endif

/*
 * SCD itself is a runtime AFE hardware-protection parameter and is applied from
 * bms_afe_hw_profile_t. These legacy zero defaults remain only as a conservative
 * compile-time fallback and are not an operating-config Flash owner.
 */
#ifndef DVC1124_HW_SCD_THRESHOLD_MV
#define DVC1124_HW_SCD_THRESHOLD_MV          0u
#endif
#ifndef DVC1124_HW_SCD_DELAY_US
#define DVC1124_HW_SCD_DELAY_US              0u
#endif

/* Fixed current-wake policy: 0 disables; otherwise CWT * 10uV. */
#ifndef DVC1124_CURRENT_WAKE_THRESHOLD_UV
#define DVC1124_CURRENT_WAKE_THRESHOLD_UV    0u
#endif

/*
 * Common-port reverse-current recovery threshold. Vendor FETControl example
 * uses 80uV (BDPT=2). With the HS-D008 200uOhm shunt this is nominally 0.4A.
 * This is topology/fail-safe policy rather than a customer protection setting.
 */
#ifndef DVC1124_BODY_DIODE_THRESHOLD_UV
#define DVC1124_BODY_DIODE_THRESHOLD_UV      80u
#endif

/*
 * DVC hardware I2C watchdog fail-safe. 4s is the shortest supported watchdog
 * period. When it expires both CHG and DSG autonomous-close sources are enabled.
 * These values are firmware-owned and must not be restored from Flash.
 */
#ifndef DVC1124_I2C_WATCHDOG_SECONDS
#define DVC1124_I2C_WATCHDOG_SECONDS         4u
#endif
#ifndef DVC1124_I2C_TIMEOUT_CLOSE_CHG
#define DVC1124_I2C_TIMEOUT_CLOSE_CHG        1u
#endif
#ifndef DVC1124_I2C_TIMEOUT_CLOSE_DSG
#define DVC1124_I2C_TIMEOUT_CLOSE_DSG        1u
#endif

/* Telink-side transaction timeout/retry; independent of DVC hardware watchdog. */
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

/*
 * Boot-only residual current-zero calibration.
 * CC2 has a fixed 256 ms conversion period; 270 ms gives one fresh conversion
 * per sample without adding a main-loop learner or periodic Flash writes.
 */
#ifndef DVC1124_BOOT_ZERO_ENABLE
#define DVC1124_BOOT_ZERO_ENABLE              1u
#endif
#ifndef DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS
#define DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS  270u
#endif
#ifndef DVC1124_BOOT_ZERO_MAX_ABS_MA
#define DVC1124_BOOT_ZERO_MAX_ABS_MA          1500u
#endif
#ifndef DVC1124_BOOT_ZERO_MAX_SPREAD_MA
#define DVC1124_BOOT_ZERO_MAX_SPREAD_MA       200u
#endif
#if (DVC1124_BOOT_ZERO_ENABLE > 1u)
#error "DVC1124_BOOT_ZERO_ENABLE must be 0 or 1"
#endif
#if DVC1124_BOOT_ZERO_ENABLE && (DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS < 256u)
#error "DVC1124 boot-zero interval must cover one complete CC2 conversion"
#endif

#endif /* DVC1124_PROJECT_CONFIG_H_ */
