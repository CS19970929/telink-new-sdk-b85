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
control = text("sh3673510_control.c")
bms = text("sh3673510_bms.c")
uart = text("modbus_uart.c")
modbus_h = text("modbus_rtu.h")

assert literal(cfg, "SH3673510_D011_CELL_COUNT") == 10
assert literal(cfg, "SH3673510_D011_SHUNT_UOHM") == 250
assert literal(cfg, "SH3673510_D011_SPI_TARGET_HZ") == 375000
require(cfg, "SH3673520_SPI_GROUP_B6_B7_D2_D7")

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
require(conf, "#define FD_BMS_TYPE                    D11")
require(conf, "#define SeriesNum                      SH3673510_D011_CELL_COUNT")
require(conf, 'BMS_HARDWARE_VERDION_DEFAULT   "D011"')

require(port, "SPI_GPIO_GROUP_B6B7D2D7")
require(port, "*cs_pin = GPIO_PD2")
require(port, "SPI_MODE3")
require(port, "FLD_SPI_BUSY")

assert literal(reg, "SH3673520_SCONF2_CHGMOS_MASK") == 0x01
assert literal(reg, "SH3673520_SCONF2_DSGMOS_MASK") == 0x02
assert literal(reg, "SH3673520_SCONF5_MOS_EN_MASK") == 0x20
assert literal(reg, "SH3673520_SCONF6_TS2_EN_MASK") == 0x20
assert literal(reg, "SH3673520_SCONF6_TS1_EN_MASK") == 0x10
assert literal(reg, "SH3673520_FLAG1_SC_MASK") == 0x10

require(control, "sh3510_gpio_input(D011_AFE_RESET_OUT_PIN)")
require(control, "sh3510_gpio_input(D011_AFE_ALARM_PIN)")
if "sh3510_gpio_output_low(D011_AFE_RESET_OUT_PIN)" in control:
    raise AssertionError("AFE RESET output net must not be driven by MCU")

for needle in (
    "SH3673510_D011_CELL_COUNT",
    "SH3673520_SCONF5_MOS_EN_MASK",
    "SH3673520_SCONF5_WDT_EN_MASK",
    "SH3673520_SCONF6_ALL_PROTECT_MASK",
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
require(bms, "D011_SWITCH_PIN")
require(bms, "sh3673510_board_wake_active")
require(bms, "sh3673510_control_set_balance")
require(bms, "sh3673510_board_set_heater")
require(bms, "SH3510_SHORT_RELEASE_SAMPLES")
require(bms, "SH3673520_BSTATUS2_LOADOFF_MASK")
require(bms, "s_output_inhibit")
require(bms, "s_requested_charge_on")

require(uart, "D011_RS485_EN_PIN")
require(uart, "modbus_rs485_receive_mode")
require(uart, "modbus_rs485_transmit_mode")
require(uart, "uart_tx_is_busy()")
require(uart, "DMA completion can precede the UART stop bit")

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
if "FLAG1_SC_MASK" in bms and "u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv" in bms:
    raise AssertionError("short-circuit recovery must not use current-to-zero as load-release proof")
require(bms, "service_short_recovery")
require(bms, "SH3510_VALID_SNAPSHOT_RELEASE_COUNT")
require(control, "sh3673510_control_get_protection_actual")
require(control, "p->u16IdsgOcp_First > 3200u")
require(control, "p->u16IdsgOcp_Second > 6400u")
require(control, "p->u16IchgOcp_First > 1760u")
require(control, "p->u16IdsgOcp_Filter > 40u")
require(control, "p->u16SocUp_First > 100u")


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
require_count(bms, "s_short_latched = 0u;", 1)

print("HS-D011 SH3673510 integration contract: PASS")
