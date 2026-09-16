#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001' / 'tc_ble_single_sdk' / 'vendor' / 'ble_sample'

def text(name):
    return (HERE / name).read_text(encoding='utf-8')

p = text('bms_afe_hw_profile.c')
m = text('modbus_rtu.c')
c = text('sh3673510_control.c')
b = text('sh3673510_bms.c')
config = text('bms_config_store.c')

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

print('Independent AFE hardware protection profile + Storage V1 contract: PASS')
