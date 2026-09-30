#!/usr/bin/env python3
"""Fast deterministic smoke check for the production-C SOC simulator."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SIMULATOR = ROOT / "tools/soc_simulator/soc_simulator.py"


def run(*args: str) -> None:
    subprocess.run([sys.executable, str(SIMULATOR), *args], cwd=ROOT, check=True)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="bms-soc-sim-") as directory:
        output = Path(directory)
        run("scenario", "--name", "rest_10m", "--seed", "20260921",
            "--output-dir", str(output / "scenario"))
        scenario_metrics = json.loads((output / "scenario/scenario_metrics.json").read_text(encoding="utf-8"))
        assert scenario_metrics["passed"] == 1 and scenario_metrics["failed"] == 0

        source = output / "scenario/scenario_input.csv"
        first = output / "first.csv"
        second = output / "second.csv"
        run("replay", str(source), "--output", str(first), "--metrics", str(output / "first.json"))
        run("replay", str(source), "--output", str(second), "--metrics", str(output / "second.json"))
        assert digest(first) == digest(second), "same normalized input did not replay deterministically"
        assert json.loads((output / "first.json").read_text(encoding="utf-8"))["failed"] == 0
    print("PASS SOC simulator: production C compile, scenario metrics and deterministic replay")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
