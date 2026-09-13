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
    "D011_HEATER_RF_EN_PIN                   GPIO_PB5",
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

require(uart, "RS485_EN_PIN")
require(uart, "modbus_rs485_receive_mode")
require(uart, "modbus_rs485_transmit_mode")
require(uart, "uart_tx_is_busy()")
require(uart, "DMA completion can precede the UART stop bit")

require(modbus_h, "#define DVC1124_COMM_REG_COUNT                  0x0000u")
require(modbus_h, "#define DVC1124_RAW_REG_COUNT                   0x0000u")

print("HS-D011 SH3673510 integration contract: PASS")
