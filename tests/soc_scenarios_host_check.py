"""使用生产纯 C SOC 运行固定数值和端点轨迹。"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
cc = shlex.split(os.environ.get("CC", "cc"))
with tempfile.TemporaryDirectory(prefix="d008-soc-") as directory:
    executable = Path(directory) / "soc_host.exe"
    flags = shlex.split(os.environ.get("BMS_HOST_CFLAGS", ""))
    subprocess.run([*cc, "-std=c99", "-Wall", "-Wextra", "-Werror", *flags,
                    "-Ibms/core", "tests/bms_soc_host_test.c", "bms/core/bms_soc.c",
                    "-o", str(executable)], cwd=ROOT, check=True)
    subprocess.run([str(executable)], check=True)
print("PASS D008 SOC integration, cutoff, bounds, rest, and checkpoint traces")
