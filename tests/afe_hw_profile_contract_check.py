#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001' / 'tc_ble_single_sdk' / 'vendor' / 'ble_sample'

def text(name):
    return (HERE / name).read_text(encoding='utf-8')

def macro_int(src, name):
    m = re.search(rf'(?m)^\s*#define\s+{re.escape(name)}\s+\(?([0-9]+)\)?\s*[uUlL]*\s*(?:/\*.*\*/)?$', src)
    if not m:
        raise AssertionError(f'missing integer macro: {name}')
    return int(m.group(1), 10)

p = text('bms_afe_hw_profile.c')
h = text('bms_afe_hw_profile.h')
m = text('modbus_rtu.c')
c = text('sh3673510_control.c')
b = text('sh3673510_bms.c')
config = text('bms_config_store.c')
product = text('sh3673510_project_config.h')

assert 'BMS_CONFIG_AFE_WORDS             35u' in config
assert 'storage_record_save(&g_bms_config_store' in config
assert 'flash_kv32' not in config
assert 'BMS_AFE_HW_PROFILE_SCHEMA_VERSION' in p
assert 'bms_afe_hw_profile_build_default(&cfg->afe_hw)' in config
assert 'bms_afe_hw_profile_build_migration_default' not in p
assert 'bms_afe_hw_profile_build_migration_default' not in h
assert 'void bms_afe_hw_profile_build_default' in p
assert 'SH3673510_HW_DEFAULT_COV_MV' in p
assert 'SH3673510_HW_DEFAULT_OCD1_A10' in p
assert 'SH3673510_HW_DEFAULT_OCC1_A10' in p
assert 'sh3510_effective_current_a10' in p
assert 'qty != BMS_AFE_HW_PROFILE_WORD_COUNT' in m
assert 'bms_afe_hw_profile_set(&candidate)' in m
commit = m[m.index('static u8 commit_protection_update'):m.index('u16 mb_crc16')]
assert 'bms_afe_apply_protection_config' not in commit
apply = c[c.index('uint8_t sh3673510_control_apply_protection'):c.index('uint8_t sh3673510_control_get_protection_actual')]
assert 'g_tParam.protect' not in apply
assert 'bms_afe_hw_profile_get(&hw)' in b

# D014 current sense: 667uOhm. Requested values are rounded upward by AFE
# hardware. Recovery must be below the effective encoded threshold, not
# necessarily below the user's requested threshold.
shunt = macro_int(product, 'SH3673510_D011_SHUNT_UOHM')
ocd1_req = macro_int(product, 'SH3673510_HW_DEFAULT_OCD1_A10')
ocd_rec = macro_int(product, 'SH3673510_HW_DEFAULT_OCD_RECOVER_A10')
occ1_req = macro_int(product, 'SH3673510_HW_DEFAULT_OCC1_A10')
occ_rec = macro_int(product, 'SH3673510_HW_DEFAULT_OCC_RECOVER_A10')

def effective_a10(requested_a10, step_uv, max_code):
    sense_uv = (requested_a10 * shunt + 5) // 10
    steps = (sense_uv + step_uv - 1) // step_uv
    steps = max(1, min(steps, max_code + 1))
    actual_uv = steps * step_uv
    return (actual_uv * 10 + shunt - 1) // shunt

assert ocd1_req == ocd_rec == 100
assert occ1_req == occ_rec == 100
assert effective_a10(ocd1_req, 5000, 15) == 150
assert effective_a10(occ1_req, 1375, 31) == 104
assert ocd_rec < effective_a10(ocd1_req, 5000, 15)
assert occ_rec < effective_a10(occ1_req, 1375, 31)

print('Independent D014 AFE hardware protection defaults + effective-threshold validation: PASS')
