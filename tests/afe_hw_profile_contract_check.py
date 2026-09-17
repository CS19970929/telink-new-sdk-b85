#!/usr/bin/env python3
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001' / 'tc_ble_single_sdk' / 'vendor' / 'ble_sample'

def text(name): return (HERE / name).read_text(encoding='utf-8')

def macro_int(src, name):
    match = re.search(rf'^\s*#define\s+{re.escape(name)}\s+(.+?)\s*$', src, re.MULTILINE)
    assert match, f'missing macro {name}'
    expr = match.group(1).split('//', 1)[0].strip()
    expr = re.sub(r'(?<=\d)[uUlL]+\b', '', expr)
    assert re.fullmatch(r'[0-9()\s+*/-]+', expr), f'unsupported macro expression {name}={expr!r}'
    return int(eval(expr, {'__builtins__': {}}, {}))

p = text('bms_afe_hw_profile.c')
config = text('bms_config_store.c')
d = text('dvc1124.c')
b = text('dvc1124_bms.c')
c = text('dvc1124_config_service.c')
backend = text('dvc1124_config_store.c')
m = text('modbus_rtu.c')
params = text('param.h')

assert 'BMS_CONFIG_AFE_WORDS             35u' in config
assert 'storage_record_save(&g_bms_config_store' in config
assert 'flash_kv32' not in config
assert 'BMS_AFE_HW_MODEL_DVC1124' in p
assert 'sense_uv < 10000u || sense_uv > 630000u' in p

apply = d[d.index('static uint8_t dvc_apply_protection_from_params'):d.index('static void dvc_note_comm_result')]
assert 'g_tParam.protect' not in apply
assert 'bms_afe_hw_profile_get(&hw)' in apply
assert 'hw.sc_a10' in apply
assert 'g_tParam.protect.u16VcellOvp_Rcv' not in b
assert 'hw.cov_recover_mv' in b
assert 'hw.ocd_recover_a10' not in b  # D008 release requires load removal/charging.
assert 'DVC_OCC_RECOVERY_TICKS' in b and 'dvc_recover_current_faults' in b
assert 'DVC1124_ConfigStore' not in c
assert 'DVC1124_CFG_ERR_READ_ONLY' in c
assert 'flash_kv32' not in backend
assert 'ConfigStoreRestore' not in backend
assert 'Only protection parameters are runtime/Flash-owned.' in backend
assert 'DVC1124_ApplyProtectionConfig()' in backend

commit = m[m.index('static u8 commit_protection_update'):m.index('u16 mb_crc16')]
assert 'bms_afe_apply_protection_config' not in commit
assert 'qty != BMS_AFE_HW_PROFILE_WORD_COUNT' in m
assert 'bms_afe_hw_profile_set(&candidate)' in m
assert 'bms_afe_apply_protection_config()' in m

assert 'static void dvc_normalize_default_profile' in p
assert p.count('dvc_normalize_default_profile(p);') == 1
assert 'p->cuv_delay_ms = dvc_clamp_u16_max(p->cuv_delay_ms, 8000u);' in p
assert 'p->ocd_recover_a10 = (u16)(min_trip - 1u);' in p
assert 'p->occ_recover_a10 = (u16)(min_trip - 1u);' in p

profile = {
    'cov_mv': macro_int(params, 'COV_3'),
    'cov_recover_mv': macro_int(params, 'COV_recover'),
    'cov_delay_ms': macro_int(params, 'COV_filter3') * 10,
    'cuv_mv': macro_int(params, 'CUV_3'),
    'cuv_recover_mv': macro_int(params, 'CUV_recover'),
    'cuv_delay_ms': macro_int(params, 'CUV_filter3') * 10,
    'ocd1_a10': macro_int(params, 'ODC_1'),
    'ocd2_a10': macro_int(params, 'ODC_2'),
    'ocd_recover_a10': macro_int(params, 'ODC_recover'),
    'ocd1_delay_ms': macro_int(params, 'ODC_filter3') * 10,
    'ocd2_delay_ms': macro_int(params, 'ODC_filter3') * 10,
    'occ1_a10': macro_int(params, 'OCC_1'),
    'occ2_a10': macro_int(params, 'OCC_2'),
    'occ_recover_a10': macro_int(params, 'OCC_recover'),
    'occ1_delay_ms': macro_int(params, 'OCC_filter3') * 10,
    'occ2_delay_ms': macro_int(params, 'OCC_filter3') * 10,
}
profile['cov_delay_ms'] = min(profile['cov_delay_ms'], 8000)
profile['cuv_delay_ms'] = min(profile['cuv_delay_ms'], 8000)
profile['ocd1_delay_ms'] = min(profile['ocd1_delay_ms'], 2048)
profile['occ1_delay_ms'] = min(profile['occ1_delay_ms'], 2048)
profile['ocd2_delay_ms'] = min(profile['ocd2_delay_ms'], 1024)
profile['occ2_delay_ms'] = min(profile['occ2_delay_ms'], 1024)
profile['ocd_recover_a10'] = min(profile['ocd_recover_a10'], min(profile['ocd1_a10'], profile['ocd2_a10']) - 1)
profile['occ_recover_a10'] = min(profile['occ_recover_a10'], min(profile['occ1_a10'], profile['occ2_a10']) - 1)

assert 501 <= profile['cov_mv'] <= 4595
assert profile['cov_recover_mv'] < profile['cov_mv']
assert 0 < profile['cuv_mv'] <= 4095
assert profile['cuv_recover_mv'] > profile['cuv_mv']
assert profile['cov_delay_ms'] <= 8000
assert profile['cuv_delay_ms'] <= 8000
assert profile['ocd1_delay_ms'] <= 2048 and profile['occ1_delay_ms'] <= 2048
assert profile['ocd2_delay_ms'] <= 1024 and profile['occ2_delay_ms'] <= 1024
assert profile['ocd_recover_a10'] < profile['ocd1_a10']
assert profile['ocd_recover_a10'] < profile['ocd2_a10']
assert profile['occ_recover_a10'] < profile['occ1_a10']
assert profile['occ_recover_a10'] < profile['occ2_a10']
assert profile['cuv_delay_ms'] == 8000
assert profile['ocd_recover_a10'] == 99
assert profile['occ_recover_a10'] == 99
print('D008 independent AFE hardware protection profile contract: PASS')
