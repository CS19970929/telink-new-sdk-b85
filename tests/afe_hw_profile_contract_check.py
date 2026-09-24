#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001' / 'tc_ble_single_sdk' / 'vendor' / 'ble_sample'

def text(name):
    return (HERE / name).read_text(encoding='utf-8')

p = text('bms_afe_hw_profile.c')
m = text('modbus_rtu.c')
c = text('sh3673510_control.c')
b = text('sh3673510_bms.c')
config = text('bms_config_store.c')
param = text('param.h')

assert 'BMS_CONFIG_AFE_WORDS             35u' in config
assert 'storage_record_save(&g_bms_config_store' in config
assert 'flash_kv32' not in config
assert 'BMS_AFE_HW_PROFILE_SCHEMA_VERSION' in p
assert 'g_tParam.protect' in p  # one-time migration/default source only
assert 'bms_cold_kv_store_get_afe_hw_profile(&p)' in p
assert 'bms_cold_kv_store_set_afe_hw_profile(&p)' in p
assert 'return bms_cold_kv_store_set_afe_hw_profile(p)' in p
assert 'qty != BMS_AFE_HW_PROFILE_WORD_COUNT' in m
assert 'bms_afe_hw_profile_set(&candidate)' in m
commit = m[m.index('static u8 commit_protection_update'):m.index('u16 mb_crc16')]
assert 'bms_afe_apply_protection_config' not in commit
apply = c[c.index('uint8_t sh3673510_control_apply_protection'):c.index('uint8_t sh3673510_control_get_protection_actual')]
assert 'g_tParam.protect' not in apply
assert 'bms_afe_hw_profile_get(&hw)' in b

def macro_int(name):
    pattern = rf'(?m)^\s*#define\s+{re.escape(name)}\s+\(?([0-9]+)\)?\s*[uUlL]*\s*$'
    match = re.search(pattern, param)
    if not match:
        raise AssertionError(f'missing simple integer macro: {name}')
    return int(match.group(1), 10)

# Regression: D013 legacy defaults use recovery == level-1 trip. This must be
# accepted for SH36735xx or AFE init is rejected before normal sampling starts.
assert macro_int('ODC_recover') == macro_int('ODC_1')
assert macro_int('OCC_recover') == macro_int('OCC_1')
assert '#define BMS_AFE_CURRENT_RECOVERY_INVALID(recover, trip) ((recover) > (trip))' in p
assert 'BMS_AFE_CURRENT_RECOVERY_INVALID(p->ocd_recover_a10, p->ocd1_a10)' in p
assert 'BMS_AFE_CURRENT_RECOVERY_INVALID(p->occ_recover_a10, p->occ1_a10)' in p

print('Independent AFE hardware protection profile + Storage V1 contract: PASS')
