"""编译真实业务源码。主机可执行文件和日志均放临时目录。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = {"meter": ["meter"], "soc": ["soc", "meter", "product"], "control": ["control", "product"], "afe": ["afe", "product"], "dvc_bus": ["dvc_bus"], "commands": ["rtu", "registers", "product", "soc", "meter"], "wide": ["sdk/wide"], "policy": ["product"], "lifecycle": ["device", "store", "product", "history", "control", "soc", "meter"], "storage": ["store", "product"], "history": ["history"],
         "lishen": ["sif"], "lishen_extended": ["sif"], "wire": ["sif"]}

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
            sdk_mock = ["-I", str(ROOT / "tests" / "dvc_sdk")] if name == "dvc_bus" else []
            if name == "lifecycle":
                sdk_mock = ["-I", str(ROOT / "tests" / "lifecycle_sdk")]
            identity = ["-DD008_BUILD_ID=0x1234abcdu", "-DD008_BUILD_DIRTY=1"] if name == "commands" else []
            policy = ["-DD008_CONFIG_UPDATES=D008_UPDATE(UPDATE_CAPACITY,1,capacity_dah,uint16_t,300),"
                      "D008_UPDATE(UPDATE_HW_SC,2,hw_sc_a10,uint16_t,5200),"
                      "D008_UPDATE(UPDATE_SW_OV,1,sw[OV].trip,uint16_t,3650),"] if name == "policy" else []
            subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", *sdk_mock, *identity, *policy,
                            "-DD008_LISHEN_EXTENDED=" + str(int(name == "lishen_extended")),
                            "-DD008_CELLS=16", "-DD008_NMC=0", "-I", str(ROOT),
                            str(ROOT / "tests" / (case + "_test.c")),
                            *[str(ROOT / (unit + ".c")) for unit in units], "-o", str(exe)],
                           cwd=out, env=env, check=True)
            scenarios = ((), ("factory-restart",), ("factory-generation",), ("shutdown-io",)) if name == "lifecycle" else ((),)
            for args in scenarios:
                subprocess.run([str(exe), *args], cwd=out, env=env, check=True)

if __name__ == "__main__":
    main()
