#!/usr/bin/env python3
"""SH3673520 protocol/driver contract check.

Ubuntu CI compiles the real sh3673520.c against a deterministic host port mock
and executes protocol, retry, init-order and conversion tests. Windows TC32 CI
also runs the source/vector checks; the production compiler validates the real
B85 port in the firmware build.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = REPO_ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk"
VENDOR = SDK_ROOT / "vendor" / "ble_sample"
REG_H = VENDOR / "sh3673520_reg.h"
DRIVER_C = VENDOR / "sh3673520.c"

EXPECTED_DEFINES = {
    "SH3673520_SPI_CMD_WRITE": 0x01,
    "SH3673520_SPI_CMD_READ": 0x02,
    "SH3673520_SPI_CMD_RESET": 0x0B,
    "SH3673520_SPI_RESET_KEY1": 0xBB,
    "SH3673520_SPI_RESET_KEY2": 0xCC,
    "SH3673520_SPI_ACK": 0xA5,
    "SH3673520_CRC8_POLY": 0x07,
    "SH3673520_REG_SCONF5": 0x44,
    "SH3673520_REG_FLAG2": 0x59,
    "SH3673520_REG_BSTATUS1": 0x5B,
    "SH3673520_REG_BSTATUS2": 0x5C,
    "SH3673520_REG_TEMP1H": 0x5D,
    "SH3673520_REG_CURH": 0x67,
    "SH3673520_REG_CELL1H": 0x69,
    "SH3673520_REG_CELL20L": 0x90,
    "SH3673520_REG_CADCDH": 0x91,
    "SH3673520_REG_VTOPH": 0x93,
    "SH3673520_REG_VCHGRH": 0x95,
    "SH3673520_REG_OWDL": 0x99,
    "SH3673520_SCONF5_CADC_EN_MASK": 0x08,
}


def parse_define(text: str, name: str) -> int:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|\d+)(?:[uUlL]*)\s*$",
        text,
        flags=re.MULTILINE,
    )
    if not match:
        raise AssertionError(f"missing numeric define: {name}")
    return int(match.group(1), 0)


def crc8(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def source_contract_checks() -> None:
    reg_text = REG_H.read_text(encoding="utf-8")
    driver_text = DRIVER_C.read_text(encoding="utf-8")

    for name, expected in EXPECTED_DEFINES.items():
        actual = parse_define(reg_text, name)
        if actual != expected:
            raise AssertionError(f"{name}: expected 0x{expected:X}, got 0x{actual:X}")

    read_header = bytes((0x02, 0x5B, 0x02))
    write_frame = bytes((0x01, 0x44, 0x38))
    reset_frame = bytes((0x0B, 0xBB, 0xCC))
    for vector in (b"", b"\x00", read_header, write_frame, reset_frame):
        value = crc8(vector)
        if not 0 <= value <= 0xFF:
            raise AssertionError("CRC range failure")

    if "malloc(" in driver_text or "calloc(" in driver_text or "free(" in driver_text:
        raise AssertionError("dynamic memory is forbidden in SH3673520 driver")
    if "SH3673520_REG_FLAG2" in re.sub(
        r"/\*.*?\*/|//[^\n]*", "", driver_text, flags=re.DOTALL
    ):
        raise AssertionError("generic driver must not poll read-clear FLAG2")
    if "SH3673520_TRANSACTION_ATTEMPTS" not in driver_text:
        raise AssertionError("bounded transaction retry policy missing")


HOST_C = r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sh3673520.h"
#include "sh3673520_port.h"

#define MOCK_MAX_TRANSACTIONS 32u
#define MOCK_MAX_BYTES 64u

static uint8_t g_tx[MOCK_MAX_TRANSACTIONS][MOCK_MAX_BYTES];
static uint8_t g_rx[MOCK_MAX_TRANSACTIONS][MOCK_MAX_BYTES];
static uint8_t g_len[MOCK_MAX_TRANSACTIONS];
static unsigned g_script_count;
static unsigned g_transaction;
static unsigned g_pos;
static int g_mismatch;

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
}

#define CHECK(cond, msg) do { if (!(cond)) { fail(msg); return 1; } } while (0)

static void mock_clear(void)
{
    memset(g_tx, 0, sizeof(g_tx));
    memset(g_rx, 0, sizeof(g_rx));
    memset(g_len, 0, sizeof(g_len));
    g_script_count = 0u;
    g_transaction = 0u;
    g_pos = 0u;
    g_mismatch = 0;
}

static void mock_add(const uint8_t *tx, const uint8_t *rx, uint8_t length)
{
    if ((g_script_count >= MOCK_MAX_TRANSACTIONS) || (length > MOCK_MAX_BYTES)) {
        g_mismatch = 1;
        return;
    }
    memcpy(g_tx[g_script_count], tx, length);
    memcpy(g_rx[g_script_count], rx, length);
    g_len[g_script_count] = length;
    ++g_script_count;
}

static void script_read(uint8_t reg, const uint8_t *data, uint8_t length, int bad_crc)
{
    uint8_t tx[MOCK_MAX_BYTES];
    uint8_t rx[MOCK_MAX_BYTES];
    uint8_t total = (uint8_t)(length + 5u);
    uint8_t index;
    uint8_t crc;

    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));
    tx[0] = SH3673520_SPI_CMD_READ;
    tx[1] = reg;
    tx[2] = length;

    rx[0] = SH3673520_SPI_RESPONSE_IDLE;
    rx[1] = SH3673520_SPI_CMD_READ;
    rx[2] = reg;
    rx[3] = length;
    for (index = 0u; index < length; ++index) {
        rx[(uint8_t)(4u + index)] = data[index];
    }
    crc = SH3673520_Crc8(rx, (size_t)length + 4u);
    rx[(uint8_t)(4u + length)] = bad_crc ? (uint8_t)(crc ^ 0x5Au) : crc;

    mock_add(tx, rx, total);
}

static void script_write(uint8_t reg, uint8_t value)
{
    uint8_t tx[5];
    uint8_t rx[5];

    tx[0] = SH3673520_SPI_CMD_WRITE;
    tx[1] = reg;
    tx[2] = value;
    tx[3] = SH3673520_Crc8(tx, 3u);
    tx[4] = 0x00u;

    rx[0] = SH3673520_SPI_RESPONSE_IDLE;
    rx[1] = SH3673520_SPI_CMD_WRITE;
    rx[2] = reg;
    rx[3] = value;
    rx[4] = SH3673520_SPI_ACK;

    mock_add(tx, rx, 5u);
}

static void script_reset(void)
{
    uint8_t tx[5];
    uint8_t rx[5];

    tx[0] = SH3673520_SPI_CMD_RESET;
    tx[1] = SH3673520_SPI_RESET_KEY1;
    tx[2] = SH3673520_SPI_RESET_KEY2;
    tx[3] = SH3673520_Crc8(tx, 3u);
    tx[4] = 0x00u;

    rx[0] = SH3673520_SPI_RESPONSE_IDLE;
    rx[1] = SH3673520_SPI_CMD_RESET;
    rx[2] = SH3673520_SPI_RESET_KEY1;
    rx[3] = SH3673520_SPI_RESET_KEY2;
    rx[4] = SH3673520_SPI_ACK;

    mock_add(tx, rx, 5u);
}

sh3673520_port_status_t sh3673520_port_init(void)
{
    return SH3673520_PORT_OK;
}

sh3673520_port_status_t sh3673520_port_begin(void)
{
    if (g_transaction >= g_script_count) {
        g_mismatch = 1;
        return SH3673520_PORT_ERR_SPI;
    }
    g_pos = 0u;
    return SH3673520_PORT_OK;
}

sh3673520_port_status_t sh3673520_port_xfer(uint8_t tx, uint8_t *rx)
{
    if ((rx == NULL) ||
        (g_transaction >= g_script_count) ||
        (g_pos >= g_len[g_transaction])) {
        g_mismatch = 1;
        return SH3673520_PORT_ERR_SPI;
    }

    if (tx != g_tx[g_transaction][g_pos]) {
        g_mismatch = 1;
    }
    *rx = g_rx[g_transaction][g_pos];
    ++g_pos;
    return SH3673520_PORT_OK;
}

void sh3673520_port_end(void)
{
    if ((g_transaction >= g_script_count) ||
        (g_pos != g_len[g_transaction])) {
        g_mismatch = 1;
    }
    ++g_transaction;
}

sh3673520_port_status_t sh3673520_port_recover(void)
{
    return SH3673520_PORT_OK;
}

void sh3673520_port_delay_us(uint32_t us)
{
    (void)us;
}

void sh3673520_port_delay_ms(uint32_t ms)
{
    (void)ms;
}

static int test_crc_and_conversions(void)
{
    const uint8_t reset[3] = {0x0Bu, 0xBBu, 0xCCu};
    int32_t current_ma;
    uint32_t resistance;

    CHECK(SH3673520_Crc8(NULL, 0u) == 0u, "empty CRC");
    CHECK(SH3673520_Crc8(reset, sizeof(reset)) == 0x5Eu, "reset CRC vector");

    CHECK(SH3673520_DecodeSigned16(0x00u, 0x00u) == 0L, "decode zero");
    CHECK(SH3673520_DecodeSigned16(0x7Fu, 0xFFu) == 32767L, "decode positive max");
    CHECK(SH3673520_DecodeSigned16(0x80u, 0x00u) == -32768L, "decode negative min");
    CHECK(SH3673520_DecodeSigned16(0xFFu, 0xFFu) == -1L, "decode -1");

    CHECK(SH3673520_CellRawToMilliVolt(6400L) == 1000L, "cell conversion");
    CHECK(SH3673520_PackRawToMilliVolt(256L) == 1000L, "pack conversion");

    CHECK(SH3673520_CurrentRawToMilliAmp(29127L, 100000u, &current_ma) ==
          SH3673520_OK, "current conversion status");
    CHECK(current_ma == 1000L, "current + conversion");
    CHECK(SH3673520_CurrentRawToMilliAmp(-29127L, 100000u, &current_ma) ==
          SH3673520_OK, "current negative status");
    CHECK(current_ma == -1000L, "current - conversion");
    CHECK(SH3673520_CurrentRawToMilliAmp(1L, 0u, &current_ma) ==
          SH3673520_ERR_INVALID_PARAM, "zero shunt rejection");

    CHECK(SH3673520_NtcRawToOhm(16384L, &resistance) == SH3673520_OK,
          "NTC conversion status");
    CHECK(resistance == 10000u, "NTC midpoint conversion");
    CHECK(SH3673520_NtcRawToOhm(32768L, &resistance) ==
          SH3673520_ERR_INVALID_PARAM, "NTC denominator boundary");

    return 0;
}

static int test_frames_and_retry(void)
{
    uint8_t value;
    uint8_t read_value = 0xA6u;
    sh3673520_comm_stats_t stats;

    mock_clear();
    SH3673520_ClearCommStats();

    script_read(SH3673520_REG_BSTATUS1, &read_value, 1u, 0);
    CHECK(SH3673520_ReadReg(SH3673520_REG_BSTATUS1, &value) == SH3673520_OK,
          "single register read");
    CHECK(value == read_value, "single register data");
    CHECK(g_mismatch == 0, "read frame encoding");
    CHECK(g_transaction == g_script_count, "read transaction count");

    mock_clear();
    script_write(SH3673520_REG_SCONF5, 0x38u);
    CHECK(SH3673520_WriteReg(SH3673520_REG_SCONF5, 0x38u) == SH3673520_OK,
          "single register write");
    CHECK(g_mismatch == 0, "write frame encoding");

    mock_clear();
    script_reset();
    CHECK(SH3673520_Reset() == SH3673520_OK, "software reset");
    CHECK(g_mismatch == 0, "reset frame encoding");

    mock_clear();
    SH3673520_ClearCommStats();
    script_read(SH3673520_REG_BSTATUS2, &read_value, 1u, 1);
    script_read(SH3673520_REG_BSTATUS2, &read_value, 1u, 0);
    CHECK(SH3673520_ReadReg(SH3673520_REG_BSTATUS2, &value) == SH3673520_OK,
          "CRC retry recovers");
    SH3673520_GetCommStats(&stats);
    CHECK(stats.crc_error_count == 1u, "CRC error statistic");
    CHECK(stats.retry_count == 1u, "retry statistic");
    CHECK(stats.consecutive_failures == 0u, "failure streak cleared");

    CHECK(SH3673520_ReadRegs(0x3Fu, &value, 1u) ==
          SH3673520_ERR_INVALID_PARAM, "read low address rejection");
    CHECK(SH3673520_WriteReg(SH3673520_REG_BSTATUS1, 0u) ==
          SH3673520_ERR_INVALID_PARAM, "read-only register write rejection");

    return 0;
}

static int test_init_and_measurements(void)
{
    uint8_t status_pair[2] = {0x03u, 0x80u};
    uint8_t sconf5_off = 0x30u;
    uint8_t sconf5_on = 0x38u;
    uint8_t cell_raw[8] = {
        0x10u, 0x00u, 0x20u, 0x00u, 0x30u, 0x00u, 0x40u, 0x00u
    };
    uint8_t pack_raw[2] = {0x01u, 0x00u};
    uint8_t cur_vadc[2] = {0x7Fu, 0xFFu};
    uint8_t cur_cadc[2] = {0xFFu, 0xFFu};
    uint8_t temps[10] = {
        0x40u, 0x00u, 0x40u, 0x00u, 0x40u, 0x00u, 0x40u, 0x00u, 0x00u, 0x00u
    };
    int32_t cells[4];
    int32_t pack_mv;
    sh3673520_current_raw_t current;
    sh3673520_temperature_raw_t temperature;
    sh3673520_device_status_t status;

    mock_clear();
    SH3673520_ClearCommStats();

    script_reset();
    script_read(SH3673520_REG_BSTATUS1, status_pair, 2u, 0);
    script_read(SH3673520_REG_SCONF5, &sconf5_off, 1u, 0);
    script_write(SH3673520_REG_SCONF5, sconf5_on);
    script_read(SH3673520_REG_SCONF5, &sconf5_on, 1u, 0);

    CHECK(SH3673520_Init() == SH3673520_OK, "initialization sequence");
    CHECK(SH3673520_IsReady() != 0u, "ready state");
    CHECK(g_mismatch == 0, "init frame/order contract");
    CHECK(g_transaction == g_script_count, "init transaction count");

    script_read(SH3673520_REG_CELL1H, cell_raw, 8u, 0);
    CHECK(SH3673520_ReadCellVoltages(cells, 4u) == SH3673520_OK,
          "cell voltage read");
    CHECK(cells[0] == 640L && cells[3] == 2560L, "cell voltage conversion");

    script_read(SH3673520_REG_VTOPH, pack_raw, 2u, 0);
    CHECK(SH3673520_ReadPackVoltage(&pack_mv) == SH3673520_OK,
          "pack voltage read");
    CHECK(pack_mv == 1000L, "pack voltage semantic conversion");

    script_read(SH3673520_REG_CURH, cur_vadc, 2u, 0);
    script_read(SH3673520_REG_CADCDH, cur_cadc, 2u, 0);
    CHECK(SH3673520_ReadCurrent(&current) == SH3673520_OK, "current read");
    CHECK(current.vadc_raw == 32767L && current.cadc_raw == -1L,
          "current signed decode");

    script_read(SH3673520_REG_TEMP1H, temps, 10u, 0);
    CHECK(SH3673520_ReadTemperatures(&temperature) == SH3673520_OK,
          "temperature read");
    CHECK(temperature.external_raw[0] == 16384L &&
          temperature.external_raw[3] == 16384L &&
          temperature.internal_raw == 0L, "temperature raw decode");

    script_read(SH3673520_REG_BSTATUS1, status_pair, 2u, 0);
    CHECK(SH3673520_ReadStatus(&status) == SH3673520_OK, "status read");
    CHECK(status.bstatus1 == status_pair[0] && status.bstatus2 == status_pair[1],
          "status values");

    CHECK(g_mismatch == 0, "measurement frame contract");
    CHECK(g_transaction == g_script_count, "all scripted transactions consumed");

    return 0;
}

int main(void)
{
    if (test_crc_and_conversions() != 0) {
        return 1;
    }
    if (test_frames_and_retry() != 0) {
        return 1;
    }
    if (test_init_and_measurements() != 0) {
        return 1;
    }

    puts("SH3673520 host contract checks PASS");
    return 0;
}
"""


def run_native_contract_if_available() -> None:
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        print("native C compiler not found; skipped host-native driver harness")
        return

    with tempfile.TemporaryDirectory(prefix="sh3673520_contract_") as tmp:
        tmpdir = Path(tmp)
        harness = tmpdir / "sh3673520_host_contract.c"
        exe = tmpdir / ("sh3673520_host_contract.exe" if os.name == "nt" else "sh3673520_host_contract")
        harness.write_text(HOST_C, encoding="utf-8")

        command = [
            compiler,
            "-std=c99",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(VENDOR),
            str(DRIVER_C),
            str(harness),
            "-o",
            str(exe),
        ]
        subprocess.run(command, check=True, cwd=REPO_ROOT)
        subprocess.run([str(exe)], check=True, cwd=REPO_ROOT)


def main() -> int:
    source_contract_checks()
    run_native_contract_if_available()
    print("SH3673520 contract check PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
