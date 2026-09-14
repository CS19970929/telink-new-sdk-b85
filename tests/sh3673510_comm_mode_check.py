#!/usr/bin/env python3
"""D011 fixed Modbus-RS485 communication contract checks."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def text(name: str) -> str:
    return (HERE / name).read_text(encoding="utf-8", errors="ignore")


def literal(src: str, name: str) -> int:
    m = re.search(rf"(?m)^\s*#define\s+{re.escape(name)}\s+([0-9]+)(?:[uUlL]*)\s*$", src)
    if not m:
        raise AssertionError(f"missing literal macro {name}")
    return int(m.group(1), 10)


conf = text("conf.h")
bus = text("bus_mux.c")
uart = text("modbus_uart.c")
sif = text("sif_send.c")

assert literal(conf, "MODBUS_RS485_ENABLE") == 1
if re.search(r"(?m)^\s*#define\s+_FUNC_SIF_\b", conf):
    raise AssertionError("D011 must not enable one-wire/SIF")
if not re.search(r"(?m)^\s*#define\s+_FUNC_UART_\b", conf):
    raise AssertionError("D011 Modbus UART must be enabled")

# Historical mux API is now only a fixed-UART compatibility shim.
assert "modbus_uart_init();" in bus
assert "BUS_STATE_UART_MODBUS" in bus
for forbidden in (
    "RX_HIGH_STABLE_US", "UART_DETECT_WINDOW_US", "UART_FALL_MIN_COUNT",
    "enter_owc_idle", "enter_owc_tx", "owc_listen_init",
    "gpio_set_interrupt_risc0", "gpio_en_interrupt_risc0",
):
    if forbidden in bus:
        raise AssertionError(f"one-wire bus switching remains: {forbidden}")

# D011 retains DE//RE control around each Modbus response.
for required in (
    "#if MODBUS_RS485_ENABLE",
    "D011_RS485_EN_PIN",
    "modbus_rs485_receive_mode",
    "modbus_rs485_transmit_mode",
    "uart_tx_is_busy()",
):
    if required not in uart:
        raise AssertionError(f"missing D011 RS485 contract: {required}")
if "bus_mux_on_uart_rx_byte" in uart or '#include "bus_mux.h"' in uart:
    raise AssertionError("UART driver must not depend on the removed mux detector")

# SIF source must be inert: no timer setup and no pin modulation.
for forbidden in ("SIF_SYNC", "BUS_STATE_OWC_TX", "FLD_IRQ_TMR0_EN", "gpio_write"):
    if forbidden in sif:
        raise AssertionError(f"active SIF implementation remains: {forbidden}")

print("D011 fixed Modbus RS485 communication contract: PASS")
