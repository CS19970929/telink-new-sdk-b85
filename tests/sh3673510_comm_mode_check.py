#!/usr/bin/env python3
"""D013 fixed direct-UART Modbus communication contract checks."""
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

assert literal(conf, "MODBUS_RS485_ENABLE") == 0
if re.search(r"(?m)^\s*#define\s+_FUNC_SIF_\b", conf):
    raise AssertionError("D013 must not enable one-wire/SIF")
if not re.search(r"(?m)^\s*#define\s+_FUNC_UART_\b", conf):
    raise AssertionError("D013 Modbus UART must be enabled")

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

# The shared driver keeps RS485 support behind a compile-time guard; D013 sets
# the guard to zero, so PA1 direction switching is compiled out completely.
if "#if MODBUS_RS485_ENABLE" not in uart:
    raise AssertionError("RS485-only code must stay compile-time guarded")
if "uart_gpio_set(D011_SCI1_TX_PIN, D011_SCI1_RX_PIN);" not in uart:
    raise AssertionError("D013 fixed PC2/PC3 UART mapping is missing")
if "bus_mux_on_uart_rx_byte" in uart or '#include "bus_mux.h"' in uart:
    raise AssertionError("UART driver must not depend on the removed mux detector")

# SIF source must be inert: no timer setup and no pin modulation.
for forbidden in ("SIF_SYNC", "BUS_STATE_OWC_TX", "FLD_IRQ_TMR0_EN", "gpio_write"):
    if forbidden in sif:
        raise AssertionError(f"active SIF implementation remains: {forbidden}")

print("D013 fixed direct-UART Modbus communication contract: PASS")
