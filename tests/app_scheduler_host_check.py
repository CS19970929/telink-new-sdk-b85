"""Run production scheduling and aging code against observable hardware stubs."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample"


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    app = (MOD / "app.c").read_text(encoding="utf-8")
    # Exercise the actual event deadline gate, stub only event payload collection.
    event = function(app, "static void app_event_log_1s_task(")
    event = event[event.index("    _attribute_data_retention_"):event.index("\tmemset(&sample")]
    scheduler = "\n".join([
        function(app, "static void app_sample_task("),
        "static void app_event_log_1s_task(void){\n" + event + "note('E');}",
        function(app, "_attribute_no_inline_ void main_loop("),
    ])
    runtime = re.sub(r'^#include[^\n]*', '', (MOD / "runtime.c").read_text(), flags=re.M)
    with tempfile.TemporaryDirectory(prefix="d008-scheduler-") as folder:
        for name, production in (("scheduler", scheduler), ("runtime", runtime)):
            fixture = (ROOT / "tests/fixtures/d008_scheduler" / (name + ".c")).read_text()
            path = Path(folder) / (name + ".c")
            path.write_text(fixture.replace("/* PRODUCTION_SOURCE */", production))
            exe = Path(folder) / (name + ".exe")
            subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c99", "-Wall", "-Wextra", "-Werror", str(path), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
    print("PASS scheduler order, deadlines, wrap, overrun, callback, invalid sample, shutdown; aging/reentry")


if __name__ == "__main__":
    main()
