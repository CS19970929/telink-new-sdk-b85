"""Run production scheduling code against observable hardware stubs."""
from validation_support import function
from pathlib import Path
from project_paths import Sources, host_includes, selected_source
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOD = Sources(ROOT)



def main():
    app = selected_source(MOD / "app.c")
    # Exercise the actual event deadline gate, stub only event payload collection.
    event = function(app, "static void app_event_log_1s_task(")
    event = event[event.index("    _attribute_data_retention_"):event.index("\tmemset(&sample")]
    scheduler = "\n".join([
        function(app, "static void app_sample_task("),
        "static void app_event_log_1s_task(void){\n" + event + "note('E');}",
        function(app, "_attribute_no_inline_ void main_loop("),
    ])
    with tempfile.TemporaryDirectory(prefix="d008-scheduler-") as folder:
        for name, production in (("scheduler", scheduler),):
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
