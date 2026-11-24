"""编译当前 D008 生产 SOC 并回放 CSV。"""
import argparse
import csv
import io
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIELDS = ("timestamp_ms", "cc2_ms", "current_ma", "min_mv", "max_mv", "mean_mv",
          "temperature_x10", "flags")
LIMITS = ((0, 2**32-1), (0, 2**32-1), (-2**31, 2**31-1),
          (0, 65535), (0, 65535), (0, 65535), (0, 65535), (0, 511))


def read_trace(path):
    with path.open(encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        if not set(FIELDS) <= set(reader.fieldnames or ()):
            raise ValueError("CSV 缺少回放字段")
        rows, expected = [], []
        for row in reader:
            values = [int(row[name]) for name in FIELDS]
            if not all(lo <= value <= hi for value, (lo, hi) in zip(values, LIMITS)):
                raise ValueError(f"第 {reader.line_num} 行超出输入范围")
            if values[-1] & 2 and not values[3] <= values[5] <= values[4]:
                raise ValueError(f"第 {reader.line_num} 行有效电压极值或均值不一致")
            rows.append(" ".join(map(str, values)))
            expected.append(int(row["expected_shown"]) if row.get("expected_shown") else None)
    if not rows:
        raise ValueError("CSV 没有样本")
    return "\n".join(rows) + "\n", expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--chemistry", choices=("lfp", "nmc"), default="lfp")
    parser.add_argument("--cells", type=int, default=16)
    parser.add_argument("--capacity-0p1ah", type=int, default=600)
    parser.add_argument("--start", type=int, choices=range(101))
    args = parser.parse_args()
    if not 4 <= args.cells <= (24 if args.chemistry == "lfp" else 23) or not 1 <= args.capacity_0p1ah <= 6553:
        parser.error("装配串数或容量无效")
    try:
        trace, expected = read_trace(args.trace)
        with tempfile.TemporaryDirectory(prefix="d008-replay-") as directory:
            runner = Path(directory) / "replay.exe"
            cc = shlex.split(os.environ.get("CC", "cc"))
            subprocess.run([*cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-Ibms/core",
                            "tools/soc_simulator/soc_replay.c", "bms/core/bms_soc.c",
                            "-o", str(runner)], cwd=ROOT, check=True)
            result = subprocess.run([str(runner), str(args.capacity_0p1ah),
                                     "1" if args.chemistry == "lfp" else "2", str(args.cells),
                                     str(args.start if args.start is not None else 101)],
                                    input=trace, text=True, stdout=subprocess.PIPE, check=True)
        actual = list(csv.DictReader(io.StringIO(result.stdout)))
        if len(actual) != len(expected):
            raise ValueError("回放输出行数不一致")
        for row, wanted in zip(actual, expected):
            if wanted is not None and int(row["shown"]) != wanted:
                summary = f'{row["timestamp_ms"]} ms 的显示为 {row["shown"]}%，预期 {wanted}%'
                raise ValueError(summary)
        print(result.stdout, end="")
        return 0
    except (OSError, ValueError, TypeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
