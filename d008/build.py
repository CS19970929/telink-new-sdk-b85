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
    parser.add_argument("--profile", choices=("bench","16s-lfp","20s-nmc","23s-lfp","24s-lfp","custom"), default="16s-lfp")
    parser.add_argument("--cells", type=int)
    parser.add_argument("--chemistry", choices=("lfp","nmc"))
    parser.add_argument("--sif", choices=("lishen","a133"), default="lishen")
    parser.add_argument("--fuse-fitted", action="store_true", help="装配确认已安装熔断支路")
    parser.add_argument("--comm-test", action="store_true", help="实板通信测试，功率输出关闭，使用 RAM 装配")
    parser.add_argument("--image", type=Path, help="按指定路径导出经原厂工具处理的 BIN")
    args = parser.parse_args()
    if args.comm_test and args.profile == "bench":
        parser.error("通信测试需要选择实际电池装配")
    if args.image and args.image.exists():
        parser.error("目标镜像已存在，请使用新的文件名")
    product = args.profile != "bench"
    if args.profile == "custom":
        if not args.cells or not args.chemistry:
            parser.error("定制装配需同时指定串数与化学体系")
        cells, chemistry = args.cells, args.chemistry
    else:
        if args.cells is not None or args.chemistry is not None:
            parser.error("只有 custom 装配可以覆盖串数和化学体系")
        spec = "16s-lfp" if not product else args.profile
        count, chemistry = spec.split("s-")
        cells = int(count)
    if not 4 <= cells <= (23 if chemistry == "nmc" else 24):
        parser.error("装配超出芯片电压或串数范围")
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

    sources = [APP / "sdk/cstartup_825x.S", APP / ("run.c" if product else "main.c"), APP / "meter.c", APP / "sdk/wide.c",
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
    if product:
        sources.extend(APP / (name + ".c") for name in
                       ("product", "dvc_bus", "afe", "control", "soc", "store", "nor",
                        "device", "registers", "rtu", "radio", "sif", "wired"))
        sources.extend(SDK / path for path in ("drivers/B85/i2c.c", "drivers/B85/uart.c",
                                               "vendor/common/flash_prot.c"))
        sources.extend(sorted((SDK / "drivers/B85/flash").glob("flash_mid*.c")))
    if args.comm_test:
        sources.append(APP / "tests/hil.c")
    includes = [APP, SDK, SDK / "drivers/B85", SDK / "common", SDK / "vendor/common"]
    flags = ["-O2", "-std=gnu99", "-fshort-wchar", "-fpack-struct", "-fshort-enums",
             "-fno-jump-tables", "-ffunction-sections", "-fdata-sections",
             "-finline-small-functions", "-fms-extensions", "-DCHIP_TYPE=1",
             "-include", str(APP / "board.h"), "-DBENCH_AWAKE=" + str(int(args.awake))]
    flags.extend(["-DD008_CELLS=" + str(cells), "-DD008_NMC=" + str(int(chemistry == "nmc")),
                  "-DD008_LISHEN=" + str(int(args.sif == "lishen")),
                  "-DD008_FUSE_FITTED=" + str(int(args.fuse_fitted)),
                  "-DAPP_FLASH_PROTECTION_ENABLE=" + str(int(product))])
    flags.append("-DD008_COMM_TEST=" + str(int(args.comm_test)))
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
    if args.image:
        import zlib
        image = out / "d008.bin"
        run([tool("objcopy"), "-O", "binary", str(elf), str(image)])
        checker = SDK / "script/tl_check_fw/tl_check_fw2.exe"
        result = subprocess.run([str(checker), str(image)], cwd=checker.parent, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
        data = image.read_bytes()
        if (b"done" not in result.stdout.lower() or data[8:12] != b"KNLT"
                or int.from_bytes(data[24:28], "little") != len(data)
                or len(data) > 0x1f000 or zlib.crc32(data) != 0xffffffff):
            raise SystemExit("原厂处理后的启动标志、长度或 CRC 不合格")
        with args.image.open("xb") as target:
            target.write(data)
        print(f"{args.image}  {len(data)} B")
    print(f"{elf}  APP={values['_sstored_']} B  main stack={reserve} B")

if __name__ == "__main__":
    main()
