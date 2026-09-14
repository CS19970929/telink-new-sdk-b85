#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HERE=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001'/'tc_ble_single_sdk'/'vendor'/'ble_sample'
def text(n): return (HERE/n).read_text(encoding='utf-8')
a=text('bms_afe_hw_access.c'); h=text('bms_afe_hw_access.h'); m=text('modbus_rtu.c'); p=text('bms_afe_hw_profile.c'); c=text('bms_afe_hw_modbus.h')
assert 'BMS_AFE_HW_ACCESS_MODBUS_FUNC       0x42u' in h
assert 'BMS_AFE_HW_ACCESS_UNLOCK_MAGIC      0x41464548UL' in h
assert 'BMS_AFE_HW_ACCESS_TIMEOUT_SECONDS   60u' in h
assert 'bms_afe_hw_access_is_active()' in m
assert 'BMS_AFE_HW_ERROR_AUTH' in m
assert 'BMS_AFE_HW_APPLY_INCONSISTENT' in m
assert 'afe_hw_profile_rollback' in m
assert 'bms_afe_hw_access_close();' in m
assert 'BMS_AFE_HW_EFFECTIVE_REG_BASE            0x2540u' in c
assert 'BMS_AFE_HW_META_INTERFACE_VERSION        0x252Bu' in c
assert 'bms_afe_hw_profile_get_effective' in p
assert 'g_tParam.protect' not in m[m.index('static u8 afe_hw_profile_write_block'):m.index('static int dvc_comm_is_semantic') if 'static int dvc_comm_is_semantic' in m[m.index('static u8 afe_hw_profile_write_block'):] else m.index('static u16 read_fault_history_reg')]
print('AFE hardware access/transaction/effective-profile contract: PASS')
