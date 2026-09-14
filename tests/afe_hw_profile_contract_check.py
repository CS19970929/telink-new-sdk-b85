#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HERE=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001'/'tc_ble_single_sdk'/'vendor'/'ble_sample'
def text(n): return (HERE/n).read_text(encoding='utf-8')
p=text('bms_afe_hw_profile.c'); kv=text('bms_cold_kv_store.c'); d=text('dvc1124.c'); b=text('dvc1124_bms.c'); c=text('dvc1124_config_service.c'); store=text('dvc1124_config_store.c'); m=text('modbus_rtu.c')
assert 'BMS_COLD_AFE_HW_KEY_BASE   0x5000u' in kv
assert 'BMS_AFE_HW_MODEL_DVC1124' in p
assert 'sense_uv < 10000u || sense_uv > 630000u' in p
apply=d[d.index('static uint8_t dvc_apply_protection_from_params'):d.index('static void dvc_note_comm_result')]
assert 'g_tParam.protect' not in apply
assert 'bms_afe_hw_profile_get(&hw)' in apply
assert 'hw.sc_a10' in apply
assert 'g_tParam.protect.u16VcellOvp_Rcv' not in b
assert 'hw.cov_recover_mv' in b and 'hw.ocd_recover_a10' in b
assert 'return DVC1124_CFG_ERR_READ_ONLY;' in c
assert 'SCD is owned by bms_afe_hw_profile' in store
commit=m[m.index('static u8 commit_protection_update'):m.index('u16 mb_crc16')]
assert 'bms_afe_apply_protection_config' not in commit
assert 'qty != BMS_AFE_HW_PROFILE_WORD_COUNT' in m
print('D008 independent AFE hardware protection profile contract: PASS')
