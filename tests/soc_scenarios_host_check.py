"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_shared_soc_host_check():
    print("CHECK shared_soc_host_check", flush=True)
    """同一 SOC 长时场景走各产品实际 app 样本入口；存储/环境仍为显式替身。"""
    import subprocess
    import sys
    from validation_support import ROOT, evidence

    subprocess.run([sys.executable, str(ROOT/'tests/d008_power_soc_host_check.py'), '--soc-only'], check=True)
    evidence({'domain': 'soc', 'virtual_days': [30, 90], 'capacity_fixture_ah': 100,
              'boundary': '生产 SOC 全函数体与所选 app 样本函数；100Ah 共同比较场景，非产品默认容量精度签核'})

def check_soc_simulator_check():
    print("CHECK soc_simulator_check", flush=True)
    #!/usr/bin/env python3
    """Fast deterministic smoke check for the production-C SOC simulator."""

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
        assert main() in (None, 0)

if __name__ == "__main__":
    check_shared_soc_host_check()
    check_soc_simulator_check()
