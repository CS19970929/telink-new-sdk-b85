#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
D008_REF = "origin/refactor/d008-bms-phase2"


def git_copy(path: str) -> None:
    data = subprocess.check_output(["git", "show", f"{D008_REF}:{path}"])
    dst = ROOT / path
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)


for rel in (
    "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_sw_protection.c",
    "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_sw_protection.h",
    "tests/sw_protection_contract_check.py",
    "docs/SOFTWARE_PROTECTION.md",
):
    git_copy(rel)

path = HERE / "sh3673510_bms.c"
text = path.read_text(encoding="utf-8")


def sub_once(src: str, pattern: str, repl: str, label: str) -> str:
    out, count = re.subn(pattern, repl, src, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one replacement, got {count}")
    return out

if '#include "bms_sw_protection.h"' not in text:
    text = text.replace('#include "bms_state.h"\n', '#include "bms_state.h"\n#include "bms_sw_protection.h"\n', 1)

text = sub_once(
    text,
    r'typedef struct\s*\{\s*uint16_t trip_count;\s*uint16_t recover_count;\s*uint8_t active;\s*\}\s*sh3510_filter_t;\s*',
    '',
    'remove SH software filter type',
)
text = sub_once(
    text,
    r'typedef enum\s*\{\s*F_CELL_OV.*?F_COUNT\s*\}\s*sh3510_filter_id_t;\s*',
    '',
    'remove SH software filter enum',
)
text = text.replace('static sh3510_filter_t s_filter[SH3510_LEVEL_COUNT][F_COUNT];\n', '', 1)
text = text.replace('static union MDLCHGFAULT_REG s_prev_fault[SH3510_LEVEL_COUNT];\n', '', 1)
text = text.replace('#define SH3510_LEVEL_COUNT            3u\n', '', 1)

text = sub_once(
    text,
    r'static uint16_t level_value\(.*?(?=static uint16_t filter_samples\()',
    '',
    'remove SH level helper',
)
text = sub_once(
    text,
    r'static uint8_t filter_update\(.*?(?=static uint16_t ntc_temp\()',
    '',
    'remove SH software filter function',
)
text = sub_once(
    text,
    r'static union MDLCHGFAULT_REG \*fault_reg\(.*?(?=static uint8_t battery_temperature_snapshot\()',
    '',
    'remove SH private fault history helpers',
)
text = sub_once(
    text,
    r'static void update_faults\(void\).*?(?=static uint8_t charge_blocked\(void\))',
    '',
    'remove SH software protection update',
)

text = sub_once(
    text,
    r'static uint8_t charge_blocked\(void\)\s*\{.*?\n\}',
    '''static uint8_t charge_blocked(void)\n{\n    return (s_hw_charge_protect ||\n            bms_sw_protection_charge_blocked() ||\n            s_heater_on) ? 1u : 0u;\n}''',
    'replace SH charge software blocking',
)
text = sub_once(
    text,
    r'static uint8_t discharge_blocked\(void\)\s*\{.*?\n\}',
    '''static uint8_t discharge_blocked(void)\n{\n    return (s_hw_discharge_protect ||\n            bms_sw_protection_discharge_blocked() ||\n            s_short_latched ||\n            bms_error_get(BMS_ERROR_DSG_SHORT) ||\n            bms_error_get(BMS_ERROR_CBC_DSG)) ? 1u : 0u;\n}''',
    'replace SH discharge software blocking',
)

anchor = '    sh3673510_control_status_t status;\n'
if '    bms_sw_protection_inputs_t sw;\n' not in text:
    if anchor not in text:
        raise SystemExit('publish_measurements status anchor not found')
    text = text.replace(anchor, anchor + '    bms_sw_protection_inputs_t sw;\n', 1)

old = '''    publish_hw_status(&status);\n    if (!s_afe_reconfigure_required) {\n        update_faults();\n#if SH3673510_HW_PROTECT_ENABLE\n        merge_hw_protection_faults(&status);\n        service_short_recovery(&status);\n        service_hw_flag_recovery(&status);\n#endif\n    }\n'''
new = '''    publish_hw_status(&status);\n    if (!s_afe_reconfigure_required) {\n        memset(&sw, 0, sizeof(sw));\n        sw.battery_temp_valid = battery_temperature_snapshot(&sw.battery_temp_min,\n                                                              &sw.battery_temp_max);\n        sw.mos_temp_valid = s_ntc_valid[SH3673510_D011_MOS_NTC_INDEX] ? 1u : 0u;\n        if (sw.mos_temp_valid)\n            sw.mos_temp = g_stCellInfoReport.u16Temperature[MOS_TEMP1];\n#if SH3673510_SW_PROTECT_ENABLE\n        bms_sw_protection_update(&sw);\n#else\n        bms_sw_protection_clear();\n#endif\n#if SH3673510_HW_PROTECT_ENABLE\n        merge_hw_protection_faults(&status);\n        service_short_recovery(&status);\n        service_hw_flag_recovery(&status);\n#endif\n        bms_sw_protection_record_fault_edges();\n    }\n'''
if old not in text:
    raise SystemExit('SH publish/protection anchor not found')
text = text.replace(old, new, 1)

old_init = '    memset(s_filter, 0, sizeof(s_filter));\n    memset(s_prev_fault, 0, sizeof(s_prev_fault));\n'
if old_init not in text:
    raise SystemExit('SH init software-filter anchor not found')
text = text.replace(old_init, '    bms_sw_protection_init();\n', 1)
path.write_text(text, encoding="utf-8")

# The older integration check asserted an obsolete comment sentence. Keep the
# actual RS485 safety contract: DE is held until UART is idle and the computed
# complete-frame line time has elapsed.
it = ROOT / "tests" / "sh3673510_d011_integration_check.py"
itext = it.read_text(encoding="utf-8")
old_contract = 'require(uart, "DMA completion can precede the UART stop bit")'
new_contract = '''require(uart, "s_rs485_tx_start_tick")\nrequire(uart, "s_rs485_tx_min_hold_us")\nrequire(uart, "clock_time_exceed(s_rs485_tx_start_tick, s_rs485_tx_min_hold_us)")'''
if old_contract in itext:
    itext = itext.replace(old_contract, new_contract, 1)
it.write_text(itext, encoding="utf-8")

print("SH3673510 common software protection migration applied")
