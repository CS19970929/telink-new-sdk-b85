#!/usr/bin/env python3
"""Deterministic SOC replay, independent battery model and regression runner.

The executable under test is compiled from the production SocEnhance.c.  This
Python layer owns file normalization, scenario generation and metrics only; it
does not implement a second copy of the firmware SOC estimator.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
from typing import Iterable, Iterator


ROOT = Path(__file__).resolve().parents[2]
HOST_CHECK = ROOT / "tests/d008_power_soc_host_check.py"
TEMP_ROOT = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")) / \
    "CodexTemp/telink-bms/soc-simulator"
RUNNER = TEMP_ROOT / ("soc_replay.exe" if os.name == "nt" else "soc_replay")
GOLDEN_TRACE_DIR = ROOT / "tests/soc/traces"

INPUT_COLUMNS = (
    "scenario", "step", "timestamp_32k", "current_ma", "cell_min_mv",
    "cell_max_mv", "cell_delta_mv", "pack_voltage_mv", "temp_min_x10",
    "temp_max_x10", "sample_valid", "voltage_valid", "balancing", "heating",
    "openwire_active", "openwire_suspected", "afe_fault", "temperature_fault",
    "current_fault", "pack_fault", "cell_ovp", "cell_uvp", "charger_known",
    "charger_present", "load_known", "load_present", "event", "true_soc",
    "firmware_seed_soc",
)

SCENARIOS = (
    "standard_discharge", "standard_charge", "ebike", "storage_low_power",
    "rest_10m", "rest_30m", "rest_2h", "deadzone_24h", "ocv_large_deviation",
    "mcu_reset", "flash_restore", "power_cycle_50", "temperature_change",
    "high_rate_sag", "near_uv_load", "charger_toggle", "regenerative_charge",
    "current_zero_offset", "current_offset_step", "capacity_aging",
    "balancing_active", "heating_active", "protection_trigger",
    "direction_switch", "afe_invalid", "cell_voltage_anomaly", "open_wire",
)

# An independent, deliberately simple LFP truth model.  The table is not read
# from firmware and therefore cannot accidentally validate the estimator with
# its own answer.
TRUTH_OCV = (
    (0.0, 2.80), (5.0, 3.10), (10.0, 3.18), (20.0, 3.24), (40.0, 3.29),
    (60.0, 3.32), (80.0, 3.36), (90.0, 3.40), (97.0, 3.47), (100.0, 3.55),
)


def truth_ocv_v(soc: float) -> float:
    soc = min(100.0, max(0.0, soc))
    for (s0, v0), (s1, v1) in zip(TRUTH_OCV, TRUTH_OCV[1:]):
        if soc <= s1:
            return v0 + (v1 - v0) * (soc - s0) / (s1 - s0)
    return TRUTH_OCV[-1][1]


def ensure_runner() -> Path:
    sources = [
        HOST_CHECK,
        ROOT / "tests/fixtures/d008_power_soc/soc.c",
        ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c",
        ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.h",
    ]
    newest = max(path.stat().st_mtime for path in sources)
    if RUNNER.exists() and RUNNER.stat().st_mtime >= newest:
        return RUNNER
    TEMP_ROOT.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    if os.name == "nt":
        env["PATH"] = r"C:\qp\qtools\MinGW32\bin;" + env.get("PATH", "")
    subprocess.run(
        [sys.executable, str(HOST_CHECK), "--compile-soc-executable", str(RUNNER)],
        cwd=ROOT, env=env, check=True,
    )
    return RUNNER


def as_int(row: dict, name: str, default: int = 0) -> int:
    value = row.get(name, default)
    if value in (None, ""):
        return default
    if isinstance(value, bool):
        return int(value)
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def normalize_row(row: dict, scenario: int, step: int) -> dict:
    cells = row.get("cell_voltage") or row.get("cell_voltages_mv") or []
    if isinstance(cells, str):
        cells = [float(value) for value in cells.replace(";", " ").split()]
    cell_min = as_int(row, "cell_min_mv", int(min(cells)) if cells else 3300)
    cell_max = as_int(row, "cell_max_mv", int(max(cells)) if cells else cell_min)
    timestamp = row.get("timestamp_32k")
    if timestamp in (None, ""):
        if row.get("timestamp_ms") not in (None, ""):
            timestamp = int(float(row["timestamp_ms"]) * 32)
        else:
            timestamp = int(float(row.get("timestamp_s", step * 0.2)) * 32000)
    temperature = row.get("temperature_decic")
    default_temp = int(temperature) + 400 if temperature not in (None, "") else 650
    event_value = row.get("event", 2 if step == 0 else 0)
    if isinstance(event_value, str):
        event_value = {"": 0, "RESET": 1, "COLD_RESET": 2, "FLASH_RESTORE": 3}.get(
            event_value.upper(), 0)
    current = row.get("current_ma", row.get("calibrated_current_ma", 0))
    true_value = next(
        (row.get(key) for key in ("true_soc", "soc_ground_truth", "soc_est")
         if row.get(key) not in (None, "")),
        50,
    )
    true_soc = float(true_value)
    result = {
        "scenario": as_int(row, "scenario", scenario),
        "step": as_int(row, "step", step),
        "timestamp_32k": int(timestamp) & 0xFFFFFFFF,
        "current_ma": int(float(current)),
        "cell_min_mv": cell_min,
        "cell_max_mv": cell_max,
        "cell_delta_mv": as_int(row, "cell_delta_mv", max(0, cell_max - cell_min)),
        "pack_voltage_mv": as_int(row, "pack_voltage_mv", int((cell_min + cell_max) * 8)),
        "temp_min_x10": as_int(row, "temp_min_x10", default_temp),
        "temp_max_x10": as_int(row, "temp_max_x10", default_temp),
        "sample_valid": as_int(row, "sample_valid", as_int(row, "valid", 1)),
        "voltage_valid": as_int(row, "voltage_valid", 1),
        "balancing": as_int(row, "balancing", as_int(row, "balancing_active", 0)),
        "heating": as_int(row, "heating", as_int(row, "heating_active", 0)),
        "openwire_active": as_int(row, "openwire_active", 0),
        "openwire_suspected": as_int(row, "openwire_suspected", 0),
        "afe_fault": as_int(row, "afe_fault", 0),
        "temperature_fault": as_int(row, "temperature_fault", 0),
        "current_fault": as_int(row, "current_fault", 0),
        "pack_fault": as_int(row, "pack_fault", 0),
        "cell_ovp": as_int(row, "cell_ovp", 0),
        "cell_uvp": as_int(row, "cell_uvp", 0),
        "charger_known": as_int(row, "charger_known", 1),
        "charger_present": as_int(row, "charger_present", 1 if float(current) < -200 else 0),
        "load_known": as_int(row, "load_known", 1),
        "load_present": as_int(row, "load_present", 1 if float(current) > 200 else 0),
        "event": int(event_value),
        "true_soc": true_soc,
        "firmware_seed_soc": as_int(row, "firmware_seed_soc", round(true_soc)),
    }
    return result


def load_raw_rows(path: Path) -> Iterator[dict]:
    if path.suffix.lower() == ".json":
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            data = data.get("records", [data])
        yield from data
        return
    if path.suffix.lower() in (".jsonl", ".ndjson"):
        with path.open(encoding="utf-8") as stream:
            for line in stream:
                if line.strip():
                    yield json.loads(line)
        return
    with path.open(newline="", encoding="utf-8-sig") as stream:
        yield from csv.DictReader(stream)


def load_rows(path: Path, scenario: int = 1) -> Iterator[dict]:
    for index, row in enumerate(load_raw_rows(path)):
        yield normalize_row(row, scenario, index)


def write_input(rows: Iterable[dict], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=INPUT_COLUMNS, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def run_replay(normalized: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(ensure_runner()), "--replay", str(normalized), str(output)], check=True)


def temperature_for(name: str, progress: float) -> float:
    if name == "temperature_change":
        return -10.0 + 55.0 * progress
    if name == "heating_active":
        return -5.0 + 10.0 * progress
    return 25.0


def scenario_shape(name: str) -> tuple[float, float, float]:
    # duration seconds, initial truth SOC, usable capacity Ah
    table = {
        "standard_discharge": (4.05 * 3600, 100, 100),
        "standard_charge": (4.2 * 3600, 0, 100),
        "ebike": (3600, 95, 100), "storage_low_power": (12 * 3600, 80, 100),
        "rest_10m": (11 * 60, 60, 100), "rest_30m": (31 * 60, 60, 100),
        "rest_2h": (2 * 3600, 60, 100), "deadzone_24h": (24 * 3600, 80, 100),
        "ocv_large_deviation": (45 * 60, 45, 100), "mcu_reset": (45 * 60, 70, 100),
        "flash_restore": (45 * 60, 70, 100), "power_cycle_50": (30 * 60, 50, 100),
        "temperature_change": (2 * 3600, 80, 100), "high_rate_sag": (20 * 60, 60, 100),
        "near_uv_load": (20 * 60, 12, 100), "charger_toggle": (30 * 60, 45, 100),
        "regenerative_charge": (45 * 60, 70, 100), "current_zero_offset": (2 * 3600, 70, 100),
        "current_offset_step": (2 * 3600, 70, 100), "capacity_aging": (3.5 * 3600, 100, 85),
        "balancing_active": (20 * 60, 85, 100), "heating_active": (20 * 60, 35, 100),
        "protection_trigger": (20 * 60, 15, 100), "direction_switch": (30 * 60, 50, 100),
        "afe_invalid": (20 * 60, 65, 100), "cell_voltage_anomaly": (20 * 60, 65, 100),
        "open_wire": (20 * 60, 65, 100),
    }
    return table[name]


def scenario_current_a(name: str, t: float, progress: float) -> float:
    if name in ("rest_10m", "rest_30m", "rest_2h", "ocv_large_deviation",
                "balancing_active", "heating_active", "open_wire"):
        return 0.0
    if name in ("standard_discharge", "capacity_aging"):
        return 25.0
    if name == "standard_charge":
        return -25.0 if progress < 0.92 else -4.0
    if name == "ebike":
        phase = t % 40.0
        return 28.0 if phase < 8 else (9.0 if phase < 25 else (0.0 if phase < 34 else -3.0))
    if name == "storage_low_power": return 0.4
    if name == "deadzone_24h": return 0.15
    if name in ("mcu_reset", "flash_restore", "power_cycle_50"): return 3.0
    if name == "temperature_change": return 2.5
    if name == "high_rate_sag": return 45.0 if int(t / 20) % 2 == 0 else 2.0
    if name == "near_uv_load": return 35.0 if int(t / 15) % 2 == 0 else 4.0
    if name == "charger_toggle": return -5.0 if int(t / 30) % 2 == 0 else 0.0
    if name == "regenerative_charge": return -8.0 if int(t / 12) % 4 == 0 else 10.0
    if name in ("current_zero_offset", "current_offset_step"): return 2.0
    if name == "protection_trigger": return 12.0
    if name == "direction_switch": return 8.0 if int(t / 3) % 2 == 0 else -8.0
    if name in ("afe_invalid", "cell_voltage_anomaly"): return 4.0
    return 0.0


def generate_scenario(name: str, scenario_id: int, seed: int) -> Iterator[dict]:
    duration_s, true_soc, capacity_ah = scenario_shape(name)
    dt_s = 0.2
    steps = int(duration_s / dt_s) + 1
    rng = random.Random(seed + scenario_id * 7919)
    timestamp = 0
    seed_soc = 85 if name == "ocv_large_deviation" else round(true_soc)
    for step in range(steps):
        progress = step / max(1, steps - 1)
        t = step * dt_s
        current_a = scenario_current_a(name, t, progress)
        temp_c = temperature_for(name, progress)
        temp_capacity = 0.88 if temp_c < 0 else (0.95 if temp_c < 10 else 1.0)
        effective_capacity = capacity_ah * temp_capacity
        if current_a >= 0:
            true_soc -= current_a * dt_s / 3600.0 / effective_capacity * 100.0
        else:
            true_soc -= current_a * 0.985 * dt_s / 3600.0 / effective_capacity * 100.0
        true_soc = min(100.0, max(0.0, true_soc))
        resistance = 0.0025 * (1.35 if temp_c < 5 else 1.0)
        center_mv = truth_ocv_v(true_soc) * 1000.0 - current_a * resistance * 1000.0
        imbalance = 5.0 + 18.0 * abs(50.0 - true_soc) / 50.0
        rest_like = abs(current_a) < 0.2
        noise = rng.gauss(0.0, 0.35 if rest_like else 1.5)
        min_mv = int(round(center_mv - imbalance / 2 + noise))
        max_mv = int(round(center_mv + imbalance / 2 + noise))
        current_offset = 80 if name == "current_zero_offset" else 0
        if name == "current_offset_step" and progress > 0.5: current_offset = 350
        measured_ma = int(round(current_a * 1000 + current_offset + rng.gauss(0, 8)))
        if abs(measured_ma) <= 200: measured_ma = 0
        active = 0.30 <= progress <= 0.70
        sample_valid = not (name == "afe_invalid" and active and int(t * 5) % 20 < 8)
        voltage_valid = not (name == "cell_voltage_anomaly" and active)
        openwire = name == "open_wire" and active
        if not voltage_valid:
            min_mv, max_mv = 0, 5200
        event = 2 if step == 0 else 0
        if name == "mcu_reset" and step == steps // 2: event = 1
        if name in ("flash_restore", "power_cycle_50") and step == steps // 2: event = 3
        cell_uvp = name == "protection_trigger" and progress > 0.75
        timestamp = (timestamp + (1 if step == 0 else 6400)) & 0xFFFFFFFF
        row = {
            "scenario": scenario_id, "step": step, "timestamp_32k": timestamp,
            "current_ma": measured_ma, "cell_min_mv": max(0, min_mv),
            "cell_max_mv": max(0, max_mv), "cell_delta_mv": max(0, max_mv - min_mv),
            "pack_voltage_mv": max(0, int(round((min_mv + max_mv) * 8))),
            "temp_min_x10": int(round((temp_c + 40) * 10)),
            "temp_max_x10": int(round((temp_c + 40) * 10)),
            "sample_valid": int(sample_valid), "voltage_valid": int(voltage_valid),
            "balancing": int(name == "balancing_active" and active),
            "heating": int(name == "heating_active" and active),
            "openwire_active": int(openwire), "openwire_suspected": int(openwire),
            "afe_fault": int(not sample_valid), "temperature_fault": 0,
            "current_fault": 0, "pack_fault": 0, "cell_ovp": 0,
            "cell_uvp": int(cell_uvp), "charger_known": 1,
            "charger_present": int(current_a < -0.2), "load_known": 1,
            "load_present": int(current_a > 0.2), "event": event,
            "true_soc": true_soc, "firmware_seed_soc": seed_soc,
        }
        yield row


def generate_all_scenarios(seed: int) -> Iterator[dict]:
    for scenario_id, name in enumerate(SCENARIOS, 1):
        yield from generate_scenario(name, scenario_id, seed)


def generate_fuzz(count: int, seed: int) -> Iterator[dict]:
    rng = random.Random(seed)
    for index in range(count):
        scenario_id = 10000 + index
        true_soc = rng.uniform(2, 98)
        seed_soc = int(min(100, max(0, true_soc + rng.uniform(-15, 15))))
        timestamp = rng.randrange(0, 0xFFFFFFFF)
        capacity = rng.uniform(8, 40)
        offset = rng.randint(-350, 350)
        for step in range(rng.randint(60, 180)):
            timestamp = (timestamp + rng.choice((3200, 6400, 8000, 9600, 12800))) & 0xFFFFFFFF
            current_a = rng.uniform(-30, 45) if rng.random() < 0.25 else rng.uniform(-8, 12)
            true_soc = min(100.0, max(0.0,
                true_soc - current_a * 0.2 / 3600.0 / capacity * 100.0))
            center = truth_ocv_v(true_soc) * 1000 - current_a * rng.uniform(1.0, 4.0)
            delta = rng.randint(2, 80)
            min_mv = int(center - delta / 2 + rng.gauss(0, 3))
            max_mv = min_mv + delta
            invalid = rng.random() < 0.005
            protection = rng.random() < 0.002
            yield normalize_row({
                "scenario": scenario_id, "step": step, "timestamp_32k": timestamp,
                "current_ma": int(current_a * 1000 + offset + rng.gauss(0, 15)),
                "cell_min_mv": min_mv, "cell_max_mv": max_mv,
                "pack_voltage_mv": int((min_mv + max_mv) * 8),
                "temperature_decic": int(rng.uniform(-150, 550)),
                "sample_valid": int(not invalid), "voltage_valid": int(not invalid),
                "balancing": int(rng.random() < 0.01), "heating": int(rng.random() < 0.005),
                "openwire_suspected": int(rng.random() < 0.001),
                "afe_fault": int(invalid), "cell_uvp": int(protection and current_a > 0),
                "cell_ovp": int(protection and current_a < 0),
                "charger_present": int(current_a < -0.2), "load_present": int(current_a > 0.2),
                "event": 2 if step == 0 else (1 if rng.random() < 0.001 else 0),
                "true_soc": true_soc, "firmware_seed_soc": seed_soc,
            }, scenario_id, step)


def calculate_metrics(output: Path, names: dict[int, str] | None = None) -> dict:
    aggregate: dict[int, dict] = {}
    with output.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            sid = int(row["scenario"]); item = aggregate.setdefault(sid, {
                "name": (names or {}).get(sid, f"fuzz_{sid}"), "count": 0, "sum": 0.0,
                "sum_sq": 0.0, "max": 0.0, "final": 0.0, "max_jump": 0,
                "max_unexplained_jump": 0, "reverse_changes": 0, "bounds_failures": 0,
                "capacity_failures": 0, "ocv_corrections": 0,
                "ocv_convergence_error": None, "truth_full_step": None,
                "firmware_full_step": None, "truth_empty_step": None,
                "firmware_empty_step": None, "eta_error_sum": 0.0,
                "eta_error_count": 0, "last_full_0p1ah": 0,
            })
            estimate = int(row["soc_est"]); display = int(row["soc_display"])
            true_soc = float(row["true_soc"]); error = abs(estimate - true_soc)
            item["count"] += 1; item["sum"] += error; item["sum_sq"] += error * error
            item["max"] = max(item["max"], error); item["final"] = error
            if not (0 <= estimate <= 100 and 0 <= display <= 100): item["bounds_failures"] += 1
            if int(row["remaining_0p1ah"]) < 0 or int(row["full_0p1ah"]) <= 0:
                item["capacity_failures"] += 1
            previous = item.get("previous_display")
            if previous is not None:
                jump = abs(display - previous); item["max_jump"] = max(item["max_jump"], jump)
                explained = int(row["endpoint_state"]) in (2, 4) or int(row["sample_state"]) in (1, 2)
                if not explained: item["max_unexplained_jump"] = max(item["max_unexplained_jump"], jump)
                current = int(row["current_ma"])
                if current > 200 and display > previous: item["reverse_changes"] += 1
                if current < -200 and display < previous: item["reverse_changes"] += 1
            item["previous_display"] = display
            if int(row["last_action"]) == 2: item["ocv_corrections"] += 1
            if int(row["ocv_state"]) >= 2:
                item["ocv_convergence_error"] = round(error, 4)
            step = int(row["step"]); endpoint = int(row["endpoint_state"])
            if true_soc >= 99.5 and item["truth_full_step"] is None: item["truth_full_step"] = step
            if endpoint == 2 and item["firmware_full_step"] is None: item["firmware_full_step"] = step
            if true_soc <= 0.5 and item["truth_empty_step"] is None: item["truth_empty_step"] = step
            if endpoint == 4 and item["firmware_empty_step"] is None: item["firmware_empty_step"] = step
            item["last_full_0p1ah"] = int(row["full_0p1ah"])
            if int(row["eta_state"]) == 2:
                current_a = abs(int(row["current_ma"])) / 1000.0
                if current_a > 0.2:
                    capacity_ah = 85.0 if item["name"] == "capacity_aging" else 100.0
                    if int(row["eta_direction"]) == 2 and int(row["tte_min"]) != 65535:
                        expected = true_soc / 100.0 * capacity_ah / current_a * 60.0
                        item["eta_error_sum"] += abs(int(row["tte_min"]) - expected); item["eta_error_count"] += 1
                    if int(row["eta_direction"]) == 1 and int(row["ttf_min"]) != 65535:
                        expected = (100.0 - true_soc) / 100.0 * capacity_ah / current_a * 60.0
                        item["eta_error_sum"] += abs(int(row["ttf_min"]) - expected); item["eta_error_count"] += 1
    results = []
    sample_count = sum(item["count"] for item in aggregate.values())
    for sid, item in sorted(aggregate.items()):
        count = item.pop("count"); item.pop("previous_display", None)
        item["scenario"] = sid; item["max_soc_error"] = round(item.pop("max"), 4)
        item["mean_soc_error"] = round(item.pop("sum") / count, 4)
        item["rms_soc_error"] = round(math.sqrt(item.pop("sum_sq") / count), 4)
        item["final_soc_error"] = round(item.pop("final"), 4)
        item["full_timing_error_s"] = (None if item["truth_full_step"] is None or
            item["firmware_full_step"] is None else
            round(abs(item["firmware_full_step"] - item["truth_full_step"]) * 0.2, 3))
        item["empty_timing_error_s"] = (None if item["truth_empty_step"] is None or
            item["firmware_empty_step"] is None else
            round(abs(item["firmware_empty_step"] - item["truth_empty_step"]) * 0.2, 3))
        expected_capacity = 850 if item["name"] == "capacity_aging" else 1000
        item["capacity_estimate_error_0p1ah"] = abs(item.pop("last_full_0p1ah") - expected_capacity)
        eta_count = item.pop("eta_error_count")
        item["mean_tte_ttf_error_min"] = (None if eta_count == 0 else
                                            round(item.pop("eta_error_sum") / eta_count, 3))
        if eta_count == 0: item.pop("eta_error_sum")
        structural_pass = (item["bounds_failures"] == 0 and item["capacity_failures"] == 0 and
                           item["max_unexplained_jump"] <= 1)
        if item["name"].startswith("fuzz_"):
            accuracy_pass = True
        elif item["name"] == "ocv_large_deviation":
            accuracy_pass = item["ocv_corrections"] >= 1 and item["final_soc_error"] < item["max_soc_error"]
        else:
            accuracy_pass = item["max_soc_error"] <= 20
        item["pass"] = structural_pass and accuracy_pass
        results.append(item)
    return {
        "scenario_count": len(results), "sample_count": sample_count,
        "passed": sum(1 for item in results if item["pass"]),
        "failed": sum(1 for item in results if not item["pass"]), "scenarios": results,
    }


def write_report(metrics: dict, path: Path) -> None:
    path.write_text(json.dumps(metrics, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def command_replay(args: argparse.Namespace) -> int:
    out = args.output.resolve(); normalized = out.with_suffix(".normalized.csv")
    write_input(load_rows(args.input.resolve()), normalized); run_replay(normalized, out)
    metrics = calculate_metrics(out); write_report(metrics, args.metrics.resolve())
    print(json.dumps({"output": str(out), "metrics": str(args.metrics.resolve()), **{k: metrics[k] for k in ("scenario_count", "sample_count", "passed", "failed")}}, ensure_ascii=False))
    return 0 if metrics["failed"] == 0 else 1


def command_scenario(args: argparse.Namespace) -> int:
    names = SCENARIOS if args.all else (args.name,)
    invalid = [name for name in names if name not in SCENARIOS]
    if invalid: raise SystemExit(f"unknown scenario: {invalid[0]}")
    output_dir = args.output_dir.resolve(); output_dir.mkdir(parents=True, exist_ok=True)
    rows = (row for index, name in enumerate(names, 1) for row in generate_scenario(name, index, args.seed))
    normalized = output_dir / "scenario_input.csv"; output = output_dir / "scenario_output.csv"
    write_input(rows, normalized); run_replay(normalized, output)
    metrics = calculate_metrics(output, {index: name for index, name in enumerate(names, 1)})
    write_report(metrics, output_dir / "scenario_metrics.json")
    print(json.dumps({k: metrics[k] for k in ("scenario_count", "sample_count", "passed", "failed")}, ensure_ascii=False))
    return 0 if metrics["failed"] == 0 else 1


def command_validate(args: argparse.Namespace) -> int:
    output_dir = args.output_dir.resolve(); output_dir.mkdir(parents=True, exist_ok=True)
    normalized = output_dir / "validation_input.csv"; output = output_dir / "validation_output.csv"
    golden_traces = sorted(path for path in GOLDEN_TRACE_DIR.glob("*")
                           if path.suffix.lower() in (".csv", ".json", ".jsonl", ".ndjson"))
    def rows() -> Iterator[dict]:
        yield from generate_all_scenarios(args.seed)
        yield from generate_fuzz(args.monte_carlo, args.seed + 1)
        for index, trace in enumerate(golden_traces):
            yield from load_rows(trace, 20000 + index)
    write_input(rows(), normalized); run_replay(normalized, output)
    names = {index: name for index, name in enumerate(SCENARIOS, 1)}
    names.update({20000 + index: "golden_" + trace.stem
                  for index, trace in enumerate(golden_traces)})
    metrics = calculate_metrics(output, names); metrics["seed"] = args.seed
    metrics["monte_carlo_count"] = args.monte_carlo
    metrics["golden_trace_count"] = len(golden_traces)
    write_report(metrics, output_dir / "validation_metrics.json")
    print(json.dumps({k: metrics[k] for k in ("scenario_count", "sample_count", "passed", "failed", "monte_carlo_count", "golden_trace_count")}, ensure_ascii=False))
    return 0 if metrics["failed"] == 0 else 1


def command_compare(args: argparse.Namespace) -> int:
    rows = list(load_rows(args.input.resolve()))
    firmware = {}
    for index, row in enumerate(load_raw_rows(args.input.resolve())):
        value = row.get("firmware_soc_est", row.get("soc_est"))
        if value not in (None, ""): firmware[index] = float(value)
    normalized = args.output.resolve().with_suffix(".normalized.csv")
    replay = args.output.resolve().with_suffix(".host.csv")
    write_input(rows, normalized); run_replay(normalized, replay)
    differences = []
    with replay.open(newline="", encoding="utf-8") as stream:
        for index, row in enumerate(csv.DictReader(stream)):
            if index in firmware: differences.append(int(row["soc_est"]) - firmware[index])
    result = {"samples": len(differences), "max_abs_difference": max(map(abs, differences), default=None),
              "mean_difference": (sum(differences) / len(differences)) if differences else None,
              "host_replay": str(replay)}
    args.output.resolve().write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False)); return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    replay = sub.add_parser("replay", help="deterministically replay CSV/JSON through production C")
    replay.add_argument("input", type=Path); replay.add_argument("--output", type=Path, required=True)
    replay.add_argument("--metrics", type=Path, required=True); replay.set_defaults(func=command_replay)
    scenario = sub.add_parser("scenario", help="run one or all 27 standard scenarios")
    choice = scenario.add_mutually_exclusive_group(required=True); choice.add_argument("--all", action="store_true")
    choice.add_argument("--name", choices=SCENARIOS); scenario.add_argument("--seed", type=int, default=20260921)
    scenario.add_argument("--output-dir", type=Path, default=TEMP_ROOT / "scenarios")
    scenario.set_defaults(func=command_scenario)
    validate = sub.add_parser("validate", help="run standard scenarios plus Monte Carlo/fuzz")
    validate.add_argument("--monte-carlo", type=int, default=5000)
    validate.add_argument("--seed", type=int, default=20260921)
    validate.add_argument("--output-dir", type=Path, default=TEMP_ROOT / "validation")
    validate.set_defaults(func=command_validate)
    compare = sub.add_parser("compare", help="offline Firmware SOC vs Host SOC A/B")
    compare.add_argument("input", type=Path); compare.add_argument("--output", type=Path, required=True)
    compare.set_defaults(func=command_compare)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
