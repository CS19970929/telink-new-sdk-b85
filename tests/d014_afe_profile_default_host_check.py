#!/usr/bin/env python3
"""Execute D014's real AFE profile builder and validator with board defaults."""

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
source = (VENDOR / "bms_afe_hw_profile.c").read_text(encoding="utf-8")
profile_header = (VENDOR / "bms_afe_hw_profile.h").read_text(encoding="utf-8")
product_header = (VENDOR / "sh3673510_project_config.h").read_text(encoding="utf-8")
compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if compiler is None:
    raise SystemExit("C host compiler required for D014 AFE migration check")

# Compile the production builder/validator bodies, while replacing only MCU
# headers and persistence with host declarations. Keep D014 board macro values
# from sh3673510_project_config.h so changed defaults are exercised.
body = source[source.index("#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124\nstatic u16 ms10_to_ms"):
              source.index("u8 bms_afe_hw_profile_init(void)")]
profile_type = profile_header[profile_header.index("typedef struct"):profile_header.index("} bms_afe_hw_profile_t;") + len("} bms_afe_hw_profile_t;")]
fields = sorted(set(re.findall(r"s->(u16\w+)", body)))
macro_lines = [line for line in product_header.splitlines()
               if re.match(r"#define\s+(SH3673510_HW_DEFAULT_|SH3673510_D011_SHUNT_UOHM)", line)]
if len(macro_lines) != 31:
    raise AssertionError("D014 AFE product defaults changed")

prefix = f"""
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define BMS_AFE_BACKEND_DVC1124 1
#define BMS_AFE_BACKEND_SH3673510 2
#define BMS_AFE_BACKEND BMS_AFE_BACKEND_SH3673510
#define BMS_AFE_HW_PROFILE_SCHEMA_VERSION 1u
#define BMS_AFE_HW_MODEL_SH3673510 0x3510u
#define BMS_AFE_HW_EN_COV (1u << 0)
#define BMS_AFE_HW_EN_CUV (1u << 1)
#define BMS_AFE_HW_EN_OCD1 (1u << 2)
#define BMS_AFE_HW_EN_OCD2 (1u << 3)
#define BMS_AFE_HW_EN_OCC1 (1u << 4)
#define BMS_AFE_HW_EN_OCC2 (1u << 5)
#define BMS_AFE_HW_EN_SC (1u << 6)
#define BMS_AFE_HW_EN_TEMP (1u << 7)
{chr(10).join(macro_lines)}
{profile_type}
struct PRT_E2ROM_PARAS {{ {''.join('u16 ' + field + ';' for field in fields)} }};
struct {{ struct PRT_E2ROM_PARAS protect; }} g_tParam;
"""
suffix = """
int main(void)
{
    bms_afe_hw_profile_t p;
    bms_afe_hw_profile_build_default(&p);
    if (p.ocd1_a10 != SH3673510_HW_DEFAULT_OCD1_A10 ||
        p.ocd_recover_a10 != SH3673510_HW_DEFAULT_OCD_RECOVER_A10 ||
        p.occ1_a10 != SH3673510_HW_DEFAULT_OCC1_A10 ||
        p.occ_recover_a10 != SH3673510_HW_DEFAULT_OCC_RECOVER_A10 ||
        !bms_afe_hw_profile_validate(&p)) return 1;
    p.ocd_recover_a10 = sh3510_effective_current_a10(p.ocd1_a10, 5000u, 15u);
    if (bms_afe_hw_profile_validate(&p)) return 2;
    p.ocd_recover_a10 = SH3673510_HW_DEFAULT_OCD_RECOVER_A10;
    p.occ_recover_a10 = sh3510_effective_current_a10(p.occ1_a10, 1375u, 31u);
    if (bms_afe_hw_profile_validate(&p)) return 3;
    puts("D014 independent AFE defaults and effective recovery boundaries: PASS");
    return 0;
}
"""

temp_root = Path(os.environ.get("LOCALAPPDATA", tempfile.gettempdir())) / "CodexTemp" / "d014" / "afe-profile-test"
temp_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=temp_root) as tmp:
    harness = Path(tmp) / "afe_profile_host.c"
    executable = Path(tmp) / "afe_profile_host.exe"
    harness.write_text(prefix + body + suffix, encoding="utf-8")
    subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", str(harness), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
