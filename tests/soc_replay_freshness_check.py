"""从当前生产 C 编译回放，验证时间回绕、重启与固定预期值。"""
import csv
import io
from pathlib import Path
import subprocess
import sys

from validation_support import ROOT

command = [sys.executable, str(ROOT / "tools/soc_simulator/soc_simulator.py"),
           str(ROOT / "tests/soc/traces/short-discharge.csv"), "--start", "82"]
output = subprocess.check_output(command, cwd=ROOT, text=True)
rows = list(csv.DictReader(io.StringIO(output)))
assert [int(row["charge_as10"]) for row in rows] == [1771200, 1771000, 1770800, 1770800, 1770600]
assert all(row["shown"] == "82" for row in rows)
print("PASS D008 SOC golden CSV and reset timing")
