#!/usr/bin/env python3
"""HS-D011 / SH3673510 integration contract checks."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"

def text(name: str) -> str:
    return (HERE / name).read_text(encoding="utf-8", errors="ignore")

def literal(src: str, name: str) -> int:
    m = re.search(rf"(?m)^\s*#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|[0-9]+)(?:[uUlL]*)\s*$", src)
    if not m:
        raise AssertionError(f"missing literal macro {name}")
    return int(m.group(1), 0)

def require(src: str, needle: str) -> None:
    if needle not in src:
        raise AssertionError(f"missing contract text: {needle}")

cfg = text("sh3673510_project_config.h")
conf = text("conf.h")
backend = text("bms_afe_backend.h")
afe = text("bms_afe.h")
reg = text("sh3673520_reg.h")
port = text("sh3673520_port.c")
port_h = text("sh3673520_port.h")
driver = text("sh3673520.c")
control = text("sh3673510_control.c")
bms = text("sh3673510_bms.c")
board = text("bms_board.c")
hw_profile = text("bms_afe_hw_profile.c")
app = text("app.c")
uart = text("modbus_uart.c")
modbus_h = text("modbus_rtu.h")

assert literal(cfg, "SH3673510_D011_CELL_COUNT") == 10
assert literal(cfg, "SH3673510_D011_SHUNT_UOHM") == 250
assert literal(cfg, "SH3673510_D011_NTC_NOMINAL_OHM") == 10000
require(cfg, "SH3673520_SPI_GROUP_B6_B7_D2_D7")
for field in (
    "SH3673510_D011_PD_EN",
    "SH3673510_D011_PUMP_EN",
    "SH3673510_D011_CGR_WK",
    "SH3673510_D011_PDSGT_CODE",
    "SH3673510_D011_MOS_EN",
    "SH3673510_D011_OCC_EN",
    "SH3673510_D011_CADC_EN",
    "SH3673510_D011_WDT_EN",
    "SH3673510_D011_TS4_HW_PROTECT_EN",
    "SH3673510_D011_TS3_HW_PROTECT_EN",
    "SH3673510_D011_TS2_HW_PROTECT_EN",
    "SH3673510_D011_TS1_HW_PROTECT_EN",
    "SH3673510_D011_SC_HW_PROTECT_EN",
    "SH3673510_D011_OCD_HW_PROTECT_EN",
    "SH3673510_D011_UV_HW_PROTECT_EN",
    "SH3673510_D011_OV_HW_PROTECT_EN",
    "SH3673510_D011_RLD",
    "SH3673510_D011_CDV_CODE",
    "SH3673510_D011_OWV_CODE",
    "SH3673510_D011_LOADON_INT",
    "SH3673510_D011_LOADOFF_INT",
    "SH3673510_D011_VADC_INT",
    "SH3673510_D011_CADC_INT",
    "SH3673510_D011_WK_INT",
    "SH3673510_D011_WDT_INT",
    "SH3673510_D011_OWD_INT",
    "SH3673510_D011_TEMP_INT",
    "SH3673510_D011_OCC_INT",
    "SH3673510_D011_OCD_INT",
    "SH3673510_D011_UV_INT",
    "SH3673510_D011_OV_INT",
):
    require(cfg, field)

for pin in (
    "D011_AFE_SCLK_PIN                       GPIO_PD7",
    "D011_AFE_MISO_PIN                       GPIO_PB6",
    "D011_AFE_MOSI_PIN                       GPIO_PB7",
    "D011_AFE_CS_PIN                         GPIO_PD2",
    "D011_AFE_ALARM_PIN                      GPIO_PC0",
    "D011_AFE_RESET_OUT_PIN                  GPIO_PC1",
    "D011_RS485_EN_PIN                       GPIO_PA1",
    "D011_SWITCH_PIN                         GPIO_PA0",
    "D011_INT_WK_MCU_PIN                     GPIO_PB1",
    "D011_HEATER_CHG_PIN                     GPIO_PB4",
    "D011_HEATER_FUSE_TRIGGER_PIN              GPIO_PB5",
):
    require(cfg, pin)

require(backend, "BMS_AFE_BACKEND_SH3673510")
require(backend, "#define BMS_AFE_BACKEND BMS_AFE_BACKEND_SH3673510")
require(afe, "sh3673510_bms_afe_init")
require(afe, "int32_t current_ma; uint32_t sample_tick_32k;")
require(conf, "#define FD_BMS_TYPE                    D11")
require(conf, "#define SeriesNum                      SH3673510_D011_CELL_COUNT")
require(conf, 'BMS_HARDWARE_VERDION_DEFAULT   "D011"')

require(port, "SPI_GPIO_GROUP_B6B7D2D7")
require(port, "*cs_pin = GPIO_PD2")
require(port, "SPI_MODE3")
require(port, "FLD_SPI_BUSY")
require(port_h, "#define SH3673520_PORT_SPI_CLOCK_HZ              500000UL")
require(port, "#define SH3673520_PORT_SPI_DIVIDER          15u")
require(port, "spi_master_init(SH3673520_PORT_SPI_DIVIDER, SPI_MODE3)")
if "requested_clock_hz" in port or "requested_clock_hz" in port_h:
    raise AssertionError("D011 SPI speed must be fixed in the port layer, not runtime-configured")

assert literal(reg, "SH3673520_MAX_CELLS") == 20
assert literal(reg, "SH3673510_MAX_CELLS") == 10
assert literal(reg, "SH3673520_REG_CELL20H") == 0x8F
assert literal(reg, "SH3673520_REG_CELL20L") == 0x90
require(driver, "uint8_t raw[SH3673520_MAX_CELLS * 2u]")
require(driver, "SH3673520_SetCellCount")
require(driver, "SH3673520_SetBalanceMask")
require(driver, "values[0] = (uint8_t)((cell_mask >> 16u) & 0x0Fu)")
require(driver, "values[1] = (uint8_t)((cell_mask >> 8u) & 0xFFu)")
require(driver, "values[2] = (uint8_t)(cell_mask & 0xFFu)")
require(driver, "sh36735xx_xfer_byte")

assert literal(reg, "SH3673520_SCONF2_CHGMOS_MASK") == 0x01
assert literal(reg, "SH3673520_SCONF2_DSGMOS_MASK") == 0x02
assert literal(reg, "SH3673520_SCONF5_MOS_EN_MASK") == 0x20
assert literal(reg, "SH3673520_SCONF6_TS2_EN_MASK") == 0x20
assert literal(reg, "SH3673520_SCONF6_TS1_EN_MASK") == 0x10
assert literal(reg, "SH3673520_FLAG1_SC_MASK") == 0x10
assert literal(reg, "SH3673520_RESET_SCONF2") == 0x50
assert literal(reg, "SH3673520_RESET_SCONF7") == 0x04
assert literal(reg, "SH3673520_RESET_OWV_ALARMH") == 0x57
assert literal(reg, "SH3673520_RESET_ALARML") == 0xFF
assert literal(reg, "SH3673520_SCONF7_CONFIG_MASK") == 0x77
assert literal(reg, "SH3673520_ALARML_ALL_MASK") == 0xFF

require(control, "sh3510_gpio_input(D011_AFE_RESET_OUT_PIN)")
require(control, "sh3510_gpio_input(D011_AFE_ALARM_PIN)")
if "sh3510_gpio_output_low(D011_AFE_RESET_OUT_PIN)" in control:
    raise AssertionError("AFE RESET output net must not be driven by MCU")

for needle in (
    "SH3673510_D011_CELL_COUNT",
    "s_static_reg_cfg",
    "SH3673510_D011_SCONF5_VALUE",
    "SH3673520_SetBalanceMask",
    "SH3673520_REG_OCD1V_OCD1T",
    "SH3673520_REG_OCD2V_OCD2T",
    "SH3673520_REG_SCV_SCT",
    "SH3673520_REG_OCCV_OCCT",
    "SH3673520_SCONF1_SLEEP",
    "Level_High",
):
    require(control, needle)

require(bms, "current.cadc_raw")
require(bms, "SH3673510_D011_SHUNT_UOHM")
require(bms, "g_stCellInfoReport.u16Ichg")
require(bms, "g_stCellInfoReport.u16IDischg")
require(bms, "s_aux.current_ma = current_ma;")
require(bms, "s_aux.sample_tick_32k = pm_get_32k_tick();")
require(app, "D011_SWITCH_PIN")
require(app, "#define APP_SAMPLE_PERIOD_US  200000u")
require(app, "bls_pm_registerAppWakeupLowPowerCb(app_sample_wakeup)")
require(app, "bls_pm_setAppWakeupLowPower(")
require(app, "static void app_sample_task(void)")
require(app, "if (bms_afe_get_aux_measurements(&sample))")
require(cfg, "#define D011_DEBUG_LED_ENABLE                   0u")
main = text("main.c")
require(main, "#if D011_DEBUG_LED_ENABLE")
if "hello World!!!" in app or "test_task_tick" in app:
    raise AssertionError("D011 production scheduler still contains demo sampling path")
require(board, "bms_board_heater_allowed")
require(board, "bms_board_balance_supported")
require(cfg, "#define SH3673510_PRODUCT_HEATER_SUPPORTED       1u")
require(cfg, "#define SH3673510_PRODUCT_BALANCE_SUPPORTED      1u")
require(bms, "sh3673510_control_set_balance")
require(bms, "SH3510_SHORT_RELEASE_SAMPLES")
require(bms, "SH3673520_BSTATUS2_LOADOFF_MASK")
require(bms, "s_output_inhibit")
require(bms, "s_requested_charge_on")
require(bms, "service_hw_flag_recovery")
require(bms, "hw_recovery_stable")
require(bms, "merge_hw_protection_faults")
require(bms, "SH3510_OCD_RELEASE_FILTER_10MS")
require(bms, "SH3673520_BSTATUS2_LOADOFF_MASK")
require(bms, "SH3673520_BSTATUS2_CHGING_MASK")
require(bms, "service_afe_reconfiguration")
require(bms, "s_hw_charge_protect")
require(bms, "s_hw_discharge_protect")
require(bms, "s_afe_reconfigure_required")
require(bms, "SH3673510_D011_HEATER_NTC_INDEX")
require(bms, "g_stCellInfoReport.u16Temperature[AFE1_TEMP3]")
require(bms, "g_stCellInfoReport.u16TempMin = bat_temp_min;")
require(bms, "g_stCellInfoReport.u16TempMax = bat_temp_max;")

# D011 is common-port: normal healthy operation requests both FETs ON.
require(app, "uint8_t chg_target = 1u;")
require(app, "uint8_t dsg_target = 1u;")
if "dsg_target = d011_switch_is_on()" in app:
    raise AssertionError("D011 common-port DSG must not be gated by PA0/SW1")
fet_start = bms.find("static uint8_t sh3510_apply_requested_fets")
fet_end = bms.find("static void publish_hw_status", fet_start)
if fet_start < 0 or fet_end <= fet_start:
    raise AssertionError("missing FET arbitration function")
fet_text = bms[fet_start:fet_end]
require(fet_text, "discharge_on = s_requested_discharge_on ? 1u : 0u;")
require(fet_text, "SH3673520_BSTATUS2_DSGING_MASK")
require(fet_text, "SH3673520_BSTATUS2_CHGING_MASK")
require(fet_text, "s_fet_command_valid")
if "D011_SWITCH_PIN" in fet_text or "key_on" in fet_text:
    raise AssertionError("PA0/SW1 must not directly gate common-port CHG/DSG FETs")
if "clear_recovered_flags" in bms:
    raise AssertionError("AFE flags must use physical-value recovery, not software-third state")
if "TS3-NC" in bms:
    raise AssertionError("D011 TS3 is a fitted 10K heater-MOS NTC, not NC")

require(uart, "D011_RS485_EN_PIN")
require(uart, "modbus_rs485_receive_mode")
require(uart, "modbus_rs485_transmit_mode")
require(uart, "uart_tx_is_busy()")
require(uart, "s_rs485_tx_start_tick")
require(uart, "s_rs485_tx_min_hold_us")
require(uart, "clock_time_exceed(s_rs485_tx_start_tick, s_rs485_tx_min_hold_us)")

require(modbus_h, "#define DVC1124_COMM_REG_COUNT                  0x0000u")
require(modbus_h, "#define DVC1124_RAW_REG_COUNT                   0x0000u")



# D011 safety invariants: old board aliases and accidental irreversible-fuse
# actuation must not re-enter production code.
production = "\n".join(text(name) for name in (
    "conf.h", "app.c", "modbus_uart.c", "sh3673510_control.c",
    "sh3673510_bms.c", "sh3673510_project_config.h",
))
for forbidden in ("CHG_IN_PIN", "RF_EN_PIN", "AFE1_PRO_EN_PIN", "MCU_LDO_PIN",
                  "D011_HEATER_RF_EN_PIN"):
    if re.search(rf"\b{re.escape(forbidden)}\b", production):
        raise AssertionError(f"obsolete/unsafe D011 alias remains: {forbidden}")

require(control, "sh3673510_board_force_heater_fuse_safe")
require(control, "D011_HEATER_FUSE_SAFE_LEVEL")
if re.search(r"gpio_write\s*\(\s*D011_HEATER_FUSE_TRIGGER_PIN\s*,\s*1", production):
    raise AssertionError("PB5 heater-fuse trigger must never be driven high before fuse logic is validated")
if "(void)requested_charge_on" in bms or "(void)requested_discharge_on" in bms:
    raise AssertionError("AFE FET API must honor caller requests")
# Short-circuit recovery must remain a distinct LOADOFF-qualified path even
# though normal OCD1/OCD2 FLAG recovery legitimately uses the OCP recovery current.
short_start = bms.find("static void service_short_recovery")
short_end = bms.find("static uint8_t hw_recovery_stable", short_start)
if short_start < 0 or short_end <= short_start:
    raise AssertionError("missing service_short_recovery")
short_text = bms[short_start:short_end]
if "u16IDischg" in short_text:
    raise AssertionError("short-circuit recovery must not use discharge current as load-release proof")
require(bms, "service_short_recovery")
require(bms, "SH3510_VALID_SNAPSHOT_RELEASE_COUNT")
require(control, "sh3673510_control_get_protection_actual")
require(hw_profile, "sense_uv > 80000u")
require(hw_profile, "sense_uv > 160000u")
require(hw_profile, "sense_uv > 44000u")
require(hw_profile, "p->ocd2_delay_ms > 400u")
require(hw_profile, "BMS_AFE_HW_PROFILE_SCHEMA_VERSION")


# Migration idempotency / compilation-safety guards.
def require_count(src: str, needle: str, expected: int = 1) -> None:
    actual = src.count(needle)
    if actual != expected:
        raise AssertionError(f"expected {expected} occurrence(s) of {needle!r}, got {actual}")

control_h = text("sh3673510_control.h")
modbus = text("modbus_rtu.c")
require_count(control, "static sh3673510_protection_actual_t s_protection_actual;")
require_count(control_h, "sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual);")
require_count(control_h, "sh3673510_board_force_heater_fuse_safe(void);")
require_count(bms, "#define SH3510_VALID_SNAPSHOT_RELEASE_COUNT 3u")
require_count(bms, "#define SH3510_SHORT_RELEASE_SAMPLES    10u")
for symbol in (
    "static uint8_t s_requested_charge_on;",
    "static uint8_t s_requested_discharge_on;",
    "static uint8_t s_output_inhibit;",
    "static uint8_t s_valid_snapshot_streak;",
    "static uint8_t s_short_latched;",
    "static uint8_t s_short_clear_pending;",
    "static uint16_t s_short_release_count;",
    "static uint16_t s_hw_recovery_count[HW_REC_COUNT];",
    "static uint8_t s_hw_charge_protect;",
    "static uint8_t s_hw_discharge_protect;",
    "static uint8_t s_afe_reconfigure_required;",
):
    require_count(bms, symbol)
require_count(modbus, "#define BMS_AFE_ACTUAL_REG_BASE  0x2180u")
require_count(modbus, "#define BMS_AFE_ACTUAL_REG_COUNT 11u")
require_count(modbus, "static u16 read_afe_actual_reg(u16 reg);")


# Function-level safety invariants that text-level dedupe must not destroy.
require_count(control, "void sh3673510_board_force_heater_fuse_safe(void)\n{")
require_count(control, "uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual)\n{")
require(bms, "s_output_inhibit = 1u;\n    s_valid_snapshot_streak = 0u;\n    /* Preserve s_short_latched across AFE communication reinitialization. */")
require(bms, "void sh3673510_bms_afe_sleep(void)\n{\n    s_output_inhibit = 1u;\n    s_valid_snapshot_streak = 0u;")
require_count(bms, "s_short_latched = 0u;", 2)


# Hardware FLAG recovery must be based on physical recovery windows and the
# actual quantized AFE threshold, not software Third-level activity.
hw_start = bms.find("static void service_hw_flag_recovery")
hw_end = bms.find("static uint8_t service_afe_reconfiguration", hw_start)
if hw_start < 0 or hw_end <= hw_start:
    raise AssertionError("missing hardware FLAG recovery state machine")
hw_text = bms[hw_start:hw_end]
for needle in (
    "sh3673510_control_get_protection_actual",
    "u16VCellMax <= hw.cov_recover_mv",
    "u16VCellMax < actual.ov_mv",
    "u16VCellMin >= hw.cuv_recover_mv",
    "u16VCellMin > actual.uv_mv",
    "dsg_ocp_release_ok",
    "SH3673520_BSTATUS2_LOADOFF_MASK",
    "SH3673520_BSTATUS2_CHGING_MASK",
    "u16IDischg <= hw.ocd_recover_a10",
    "u16IDischg < actual.ocd1_a10",
    "u16IDischg < actual.ocd2_a10",
    "hw.ocd_recover_ms",
    "u16Ichg <= hw.occ_recover_a10",
    "u16Ichg < actual.occ_a10",
    "bat_max <= hw.chg_ot_recover_x10",
    "bat_min >= hw.chg_ut_recover_x10",
):
    require(hw_text, needle)
if "unMdlFault_Third" in hw_text:
    raise AssertionError("hardware FLAG recovery must be independent from software Third-level activity")

# Heater and balance policy belong to common bms_features; the SH backend only
# publishes measurements and exposes hardware primitives.
for forbidden in ("static void apply_heater", "static void apply_balance",
                  "SH3510_REINIT_TRIGGER", "SH3510_REINIT_COOLDOWN"):
    if forbidden in bms:
        raise AssertionError(f"SH backend owns common policy: {forbidden}")

sample_start = bms.find("void sh3673510_bms_afe_sample(void)")
sample_end = bms.find("uint8_t sh3673510_bms_afe_apply_protection_config", sample_start)
if sample_start < 0 or sample_end <= sample_start:
    raise AssertionError("missing SH sample function")
sample_text = bms[sample_start:sample_end]
for forbidden in ("apply_heater();", "apply_balance();",
                  "sh3510_apply_requested_fets();", "sh3673510_control_init()"):
    if forbidden in sample_text:
        raise AssertionError(f"SH sample bypasses common owner: {forbidden}")

# Realtime battery temperature extrema must be refreshed from TS1/TS2 on every
# valid sample. MOS/heater temperatures remain independently reported.
publish_start = bms.find("static uint8_t publish_measurements")
publish_end = bms.find("void sh3673510_bms_afe_init", publish_start)
if publish_start < 0 or publish_end <= publish_start:
    raise AssertionError("missing publish_measurements")
publish_text = bms[publish_start:publish_end]
for needle in (
    "battery_temperature_snapshot(&bat_temp_min, &bat_temp_max)",
    "g_stCellInfoReport.u16TempMin = bat_temp_min;",
    "g_stCellInfoReport.u16TempMax = bat_temp_max;",
    "g_stCellInfoReport.u16TempMin = 0u;",
    "g_stCellInfoReport.u16TempMax = 0u;",
):
    require(publish_text, needle)

print("HS-D011 SH3673510 integration contract: PASS")
