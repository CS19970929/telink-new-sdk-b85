#!/usr/bin/env python3
"""D014 fixed Modbus-RS485 communication contract checks."""
from pathlib import Path
from project_paths import Sources, host_includes, selected_source
import re

ROOT = Path(__file__).resolve().parents[1]
HERE = Sources(ROOT)


def text(name: str) -> str:
    return (HERE / name).read_text(encoding="utf-8", errors="ignore")


def literal(src: str, name: str) -> int:
    m = re.search(rf"(?m)^\s*#define\s+{re.escape(name)}\s+([0-9]+)(?:[uUlL]*)\s*(?:/\*.*\*/)?$", src)
    if not m:
        raise AssertionError(f"missing literal macro {name}")
    return int(m.group(1), 10)


conf = text("bms_product.h")
app = selected_source(HERE / "app.c")
uart = selected_source(HERE / "modbus_uart.c")
main = selected_source(HERE / "main.c")

assert literal(conf, "BMS_PRODUCT_RS485_ENABLE") == 1
if re.search(r"(?m)^\s*#define\s+BMS_PRODUCT_SIF_ENABLE\b", conf):
    raise AssertionError("D014 must not enable one-wire/SIF")
if not re.search(r"(?m)^\s*#define\s+BMS_PRODUCT_UART_ENABLE\b", conf):
    raise AssertionError("D014 Modbus UART must be enabled")

# Fixed Modbus UART is initialized directly. No inert SIF/mux enters the IRQ.
assert "modbus_uart_init();" in app
assert "modbus_uart_irq_proc();" in main
assert "SH3673510_FIXED_UART_BLOCKS_PM" in text("app.c")
assert literal((ROOT / "bms/products/sh3673510_defaults.h").read_text(encoding="utf8"), "SH3673510_FIXED_UART_BLOCKS_PM") == 1
order = (ROOT / "bms/products" / HERE.product / "sources.txt").read_text(encoding="utf-8")
for name in ("bus_mux.c", "bus_mux.h", "sif_send.c", "sif_send.h"):
    assert not any(name == Path(entry).name for entry in (ROOT / "bms/products" / HERE.product / "sources.txt").read_text(encoding="utf8").splitlines())
    assert name not in order
assert "vendor/ble_sample/dvc1124" not in order
assert not any("/dvc1124/" in entry for entry in (ROOT / "bms/products" / HERE.product / "sources.txt").read_text().splitlines())
assert "DVC1124_ConfigService" not in selected_source(HERE / "modbus_rtu.c")
assert "dvc1124_config_service.h" not in selected_source(HERE / "modbus_rtu.h")

# D014 retains DE//RE control around each Modbus response.
for required in (
    "modbus_rs485_receive_mode",
    "modbus_rs485_transmit_mode",
    "uart_tx_is_busy()",
):
    if required not in uart:
        raise AssertionError(f"missing D014 RS485 contract: {required}")
assert "#if BMS_PRODUCT_RS485_ENABLE" in text("modbus_uart.c")
assert "BMS_BOARD_RS485_EN_PIN" in text("modbus_uart.c")
if "bus_mux_on_uart_rx_byte" in uart or '#include "bus_mux.h"' in uart:
    raise AssertionError("UART driver must not depend on the removed mux detector")

# DE release must be gated by both the B85 TX_DONE state and the calculated
# complete line time for the actual response length.  A fixed post-event delay
# alone is not sufficient evidence that a long frame has finished.
assert literal(uart, "MODBUS_UART_BITS_PER_CHAR") == 10
if literal(uart, "MODBUS_RS485_TX_EXTRA_GUARD_US") < 100:
    raise AssertionError("D014 RS485 extra release guard is unexpectedly short")
for required in (
    "modbus_rs485_min_hold_us",
    "s_rs485_tx_start_tick",
    "s_rs485_tx_min_hold_us",
    "frame_ticks = len * MODBUS_UART_BITS_PER_CHAR * ticks_per_bit;",
    "s_rs485_tx_min_hold_us = modbus_rs485_min_hold_us(len);",
    "clock_time_exceed(s_rs485_tx_start_tick, s_rs485_tx_min_hold_us)",
):
    if required not in uart:
        raise AssertionError(f"missing D014 full-frame RS485 hold: {required}")

print("D014 fixed Modbus RS485 communication contract: PASS")
