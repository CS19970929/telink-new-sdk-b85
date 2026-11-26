"""编译真实业务源码。主机可执行文件和日志均放临时目录。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = {"meter": ["meter"], "wide": ["sdk/wide"], "storage": ["store"], "history": ["history"],
         "lishen": ["sif"], "lishen_extended": ["sif"]}

def main():
    compiler = os.environ.get("HOST_CC") or shutil.which("cc") or shutil.which("gcc")
    if not compiler and os.name == "nt":
        compiler = "C:/qp/qtools/MinGW32/bin/cc.exe"
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env["PATH"]
    with tempfile.TemporaryDirectory(prefix="d008-host-") as out:
        for name, units in CASES.items():
            case = "lishen" if name.startswith("lishen") else name
            exe = Path(out) / (name + (".exe" if os.name == "nt" else ""))
            subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-DD008_LISHEN_EXTENDED=" + str(int(name == "lishen_extended")),
                            "-DD008_CELLS=16", "-DD008_NMC=0", "-I", str(ROOT),
                            str(ROOT / "tests" / (case + "_test.c")),
                            *[str(ROOT / (unit + ".c")) for unit in units], "-o", str(exe)],
                           cwd=out, env=env, check=True)
            subprocess.run([str(exe)], cwd=out, env=env, check=True)

if __name__ == "__main__":
    main()
