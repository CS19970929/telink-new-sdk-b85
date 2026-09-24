#!/usr/bin/env python3
"""HS-D014 / SH3673510 integration contract checks."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def text(name: str) -> str:
    return (HERE / name).read_text(encoding="utf-8", errors="ignore")


def literal(src: str, name: str) -> int:
    m = re.search(rf"(?m)^\s*#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|[0-9]+)(?:[uUlL]*)\s*(?:/\*.*\*/)?\s*$", src)
    if not m:
        raise AssertionError(f"missing literal macro {name}")
    return int(m.group(1), 0)


def require(src: str, needle: str) -> None:
    if needle not in src:
        raise AssertionError(f"missing D014 contract text: {needle}")


cfg = text("sh3673510_project_config.h")
conf = text("conf.h")
app = text("app.c")
main = text("main.c")
control = text("sh3673510_control.c")
bms = text("sh3673510_bms.c")
sw_protection = text("bms_sw_protection.c")
board = text("bms_board.c")
uart = text("modbus_uart.c")
port = text("sh3673520_port.c")
port_h = text("sh3673520_port.h")
driver = text("sh3673520.c")

# Product electrical profile from the D014 schematic.
assert literal(cfg, "SH3673510_D011_CELL_COUNT") == 8
assert literal(cfg, "SH3673510_D011_SHUNT_UOHM") == 667
assert literal(cfg, "SH3673510_D011_NTC_NOMINAL_OHM") == 10000
assert literal(cfg, "SH3673510_PRODUCT_HEATER_SUPPORTED") == 0
assert literal(cfg, "SH3673510_PRODUCT_BALANCE_SUPPORTED") == 1
assert literal(cfg, "SH3673510_PRODUCT_HEATER_NTC_SUPPORTED") == 0
assert literal(cfg, "SH3673510_PRODUCT_MOS_NTC_SUPPORTED") == 1
assert literal(cfg, "SH3673510_D011_TS4_HW_PROTECT_EN") == 0

# Canonical D014 board nets.
for pin in (
    "D014_CMNT_EN_PIN                        GPIO_PD4",
    "D014_AFE_SCLK_PIN                       GPIO_PD7",
    "D014_SWITCH_PIN                         GPIO_PA0",
    "D014_RS485_EN_PIN                       GPIO_PA1",
    "D014_SWS_PIN                            GPIO_PA7",
    "D014_INT_WK_MCU_PIN                     GPIO_PB1",
    "D014_AFE_MISO_PIN                       GPIO_PB6",
    "D014_AFE_MOSI_PIN                       GPIO_PB7",
    "D014_AFE_ALARM_PIN                      GPIO_PC0",
    "D014_AFE_RESET_OUT_PIN                  GPIO_PC1",
    "D014_SCI1_TX_PIN                        GPIO_PC2",
    "D014_SCI1_RX_PIN                        GPIO_PC3",
    "D014_DEBUG_LED_PIN                      GPIO_PC4",
    "D014_CMNT_WK_PIN                        GPIO_PD3",
    "D014_AFE_CS_PIN                         GPIO_PD2",
):
    require(cfg, pin)

# Product identity and communications.
require(conf, "#define FD_BMS_TYPE                    D14")
require(conf, "#define SeriesNum                      SH3673510_D011_CELL_COUNT")
require(conf, "#define MODBUS_RS485_ENABLE              1")
require(conf, 'BMS_HARDWARE_VERDION_DEFAULT   "D014"')
if not re.search(r'BMS_SERIAL_NUMBER_DEFAULT\s+"D014-[^"]+"', conf):
    raise AssertionError("D014 default serial number must retain the D014- prefix")
require(conf, '#define DEV_NAME_STR  "BT_D014"')
require(conf, "#define D14             D11")

# AFE SPI remains the verified SH36735xx Mode-3, 500-kHz implementation.
require(cfg, "SH3673520_SPI_GROUP_B6_B7_D2_D7")
require(port, "SPI_GPIO_GROUP_B6B7D2D7")
require(port, "SPI_MODE3")
require(port, "*cs_pin = GPIO_PD2")
require(port_h, "#define SH3673520_PORT_SPI_CLOCK_HZ              500000UL")

# The 8S profile must actually drive acquisition/balance sizing.
require(bms, "int32_t cell[SH3673510_D011_CELL_COUNT];")
require(bms, "SH3673520_ReadCellVoltages(cell, SH3673510_D011_CELL_COUNT)")
require(bms, "SH3673510_D011_SHUNT_UOHM")
require(control, "SH3673520_SetBalanceMask")
require(control, "SH3673510_D011_CELL_COUNT")
require(driver, "SH3673520_SetCellCount")

# D014 intentionally has no qualified heater output or TS3 heater NTC.
require(board, "if (SH3673510_PRODUCT_HEATER_SUPPORTED)")
require(control, "#if SH3673510_PRODUCT_HEATER_SUPPORTED")
require(bms, "#if SH3673510_PRODUCT_HEATER_NTC_SUPPORTED")
require(bms, "#if SH3673510_PRODUCT_MOS_NTC_SUPPORTED")
require(bms, "sw.mos_temp_valid = s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX]")
require(sw_protection, "inputs->battery_temp_valid && inputs->mos_temp_valid")
require(sw_protection, "p->u16TmosOTp_Third")
if "D011_HEATER_FUSE_TRIGGER_PIN" in app:
    raise AssertionError("D014 app must not drive the inherited D011 heater-fuse pin")
if "HT-RF-EN" in app or "HT-CHG" in app:
    raise AssertionError("D014 app reintroduced a D011-only heater net")

# D014 app/RS485 paths use canonical D014 names, not board-name inheritance.
for token in (
    "D014_SWITCH_PIN",
    "D014_INT_WK_MCU_PIN",
    "D014_AFE_ALARM_PIN",
    "D014_AFE_RESET_OUT_PIN",
    "D014_CMNT_EN_PIN",
    "D014_CMNT_WK_PIN",
):
    require(app, token)
require(main, "D014_DEBUG_LED_ENABLE")
require(main, "D014_DEBUG_LED_PIN")
require(uart, "D014_RS485_EN_PIN")
require(uart, "modbus_rs485_receive_mode")
require(uart, "modbus_rs485_transmit_mode")
require(uart, "uart_tx_is_busy()")

# Common-port FET arbitration and independent SW/HW protection remain inherited.
require(app, "uint8_t chg_target = 1u;")
require(app, "uint8_t dsg_target = 1u;")
require(bms, "service_hw_flag_recovery")
require(bms, "service_short_recovery")
require(bms, "service_afe_reconfiguration")
require(bms, "s_output_inhibit")
require(bms, "s_requested_charge_on")
require(bms, "s_requested_discharge_on")

print("HS-D014 SH3673510 integration contract: PASS")
