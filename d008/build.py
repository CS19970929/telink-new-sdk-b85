"""仅链接 TLSR8251 ELF 与 MAP。默认产物位于用户临时目录。"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

APP = Path(__file__).resolve().parent
PACKAGE = APP.parent / "tc_ble_single_sdk-V3.4.2.8_Patch_0001"
SDK = PACKAGE / "tc_ble_single_sdk"
PATCH = PACKAGE / "tc_ble_single_sdk_v3.4.2.8_Patch" / "patch_0001_20250929"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--awake", action="store_true", help="禁用普通 Suspend，作台架对照")
    args = parser.parse_args()
    out = Path(tempfile.mkdtemp(prefix="d008-clean-"))
    tool_dir = Path(os.environ.get("TC32_BIN", "C:/TelinkIoTStudio/opt/tc32/bin"))
    compiler = shutil.which("tc32-elf-gcc", path=str(tool_dir)) or shutil.which("tc32-elf-gcc")
    if not compiler:
        parser.error("请设置 TC32_BIN")
    tools = Path(compiler).parent
    env = dict(os.environ)
    env["PATH"] = str(tools) + os.pathsep + os.environ["PATH"]
    def run(command):
        return subprocess.run(command, cwd=out, env=env, check=True)
    def tool(name):
        return str(tools / ("tc32-elf-" + name + (".exe" if os.name == "nt" else "")))

    sources = [APP / "sdk/cstartup_825x.S", APP / "main.c", APP / "meter.c", APP / "sdk/wide.c",
               PATCH / "drivers/B85/clock.c"]
    for name in ("common/div_mod.S", "common/string.c", "common/utility.c",
                 "drivers/B85/analog.c", "drivers/B85/gpio.c", "drivers/B85/flash.c",
                 "drivers/B85/timer.c", "drivers/B85/adc.c", "drivers/B85/aes.c",
                 "drivers/B85/driver_ext/rf_pa.c", "vendor/common/tlkapi_debug.c",
                 "application/print/u_printf.c", "application/print/putchar.c", "drivers/B85/bsp.c", "drivers/B85/watchdog.c",
                 "drivers/B85/driver_ext/ext_misc.c", "drivers/B85/driver_ext/ext_calibration.c",
                 "drivers/B85/flash/flash_common.c", "vendor/common/ble_flash.c",
                 "vendor/common/user_config.c"):
        sources.append(SDK / name)
    includes = [APP, SDK, SDK / "drivers/B85", SDK / "common", SDK / "vendor/common"]
    flags = ["-O2", "-std=gnu99", "-fshort-wchar", "-fpack-struct", "-fshort-enums",
             "-fno-jump-tables", "-ffunction-sections", "-fdata-sections",
             "-finline-small-functions", "-fms-extensions", "-DCHIP_TYPE=1",
             "-include", str(APP / "board.h"), "-DBENCH_AWAKE=" + str(int(args.awake))]
    objects = []
    for index, source in enumerate(sources):
        obj = out / (str(index) + "-" + source.stem + ".o")
        options = ["-DMCU_STARTUP_8251=1"] if source.suffix == ".S" else flags
        run([compiler, *options, *["-I" + str(p) for p in includes],
             "-c", str(source), "-o", str(obj)])
        objects.append(str(obj))
    elf = out / "d008.elf"
    run([tool("ld"), "--gc-sections", "-T", str(PATCH / "project/tlsr_tc32/B85/boot.link"),
         "-L" + str(SDK / "proj_lib"), "-Map=" + str(out / "d008.map"), "-o", str(elf),
         *objects, "--start-group", "-llt_825x", "-llt_general_stack", "--end-group"])
    symbols = subprocess.check_output([tool("nm"), str(elf)], text=True, env=env)
    values = {p[2]: int(p[0], 16) for line in symbols.splitlines()
              if len(p := line.split()) == 3}
    reserve = values["__SRAM_SIZE"] - values["_ram_use_end_"]
    if values["__SRAM_SIZE"] != 0x848000 or reserve < 3072 or values["_sstored_"] > 0x1f000:
        raise SystemExit("目标超出 TLSR8251 的 RAM 或 APP 槽")
    print(f"{elf}  APP={values['_sstored_']} B  main stack={reserve} B")

if __name__ == "__main__":
    main()
