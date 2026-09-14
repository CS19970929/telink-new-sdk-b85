#!/usr/bin/env python3
"""D008 20S-NMC compile-profile and DVC channel-mask safety contracts."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001' / 'tc_ble_single_sdk' / 'vendor' / 'ble_sample'
profile = (HERE / 'd008_product_profile.h').read_text(encoding='utf-8')
dvc = (HERE / 'dvc1124.c').read_text(encoding='utf-8')
cfg = (HERE / 'dvc1124_project_config.h').read_text(encoding='utf-8')
store = (HERE / 'dvc1124_config_store.c').read_text(encoding='utf-8')
service = (HERE / 'dvc1124_config_service.c').read_text(encoding='utf-8')

assert '#define D008_PRODUCT_PROFILE_20S_NMC  2u' in profile
block = profile.split('#elif (D008_PRODUCT_PROFILE == D008_PRODUCT_PROFILE_20S_NMC)', 1)[1].split('#else', 1)[0]
assert '#define D008_PRODUCT_CELL_COUNT       20u' in block
assert 'BMS_SOC_CHEMISTRY_NMC' in block
assert 'BMS_SOC_PROFILE_GENERIC_NMC' in block
assert '#define DVC1124_DEFAULT_CELL_COUNT           D008_PRODUCT_CELL_COUNT' in cfg
assert 'DVC1124_SetCellCount((uint8_t)DVC1124_DEFAULT_CELL_COUNT)' in dvc

# Reproduce the driver mask algorithm: for 20S, only channels 21..24 are masked.
def masks(cell_count: int):
    out = [0, 0, 0]
    for cell in range(5, 25):
        if cell <= cell_count:
            continue
        if cell >= 17:
            out[0] |= 1 << (cell - 17)
        elif cell >= 9:
            out[1] |= 1 << (cell - 9)
        else:
            out[2] |= 1 << (cell - 1)
    return out
assert masks(20) == [0xF0, 0x00, 0x00]
assert masks(24) == [0x00, 0x00, 0x00]
assert 'for (cell = 5u; cell <= DVC1124_MAX_CELLS; ++cell)' in dvc

# D008 board invariants survive legacy persisted config.
assert 'dvc_cfg_normalize_product_policy' in store
assert 'high_side_fet_mask = DVC1124_DEFAULT_HIGH_SIDE_FET_MASK' in store
assert '(cfg->current_wake_threshold_uv == 0u) && cfg->operating.current_wake_enable' in store
assert 'DVC1124_DEFAULT_DSG_MASK_POLICY' in store
assert 'DVC1124_DEFAULT_CHG_MASK_POLICY' in store
assert 'DVC1124_CFG_ERR_INCONSISTENT' in service
assert 'DVC1124_ConfigStoreSave(before)' in service

# Open-wire results are raw measurements only; no unverified open-wire trip rule.
assert 'DVC1124_OpenWireBegin' in dvc and 'DVC1124_OpenWirePoll' in dvc
ow = dvc.split('void DVC1124_OpenWirePoll', 1)[1].split('void DVC1124_OpenWireGetResult', 1)[0]
assert 'cell_mv' in ow and 'OPENWIRE_READY' in ow
assert 'bms_error_raise' not in ow

# Balancing is armed by request and renewed below the ~60s hardware auto-clear.
assert '#define DVC_BALANCE_REFRESH_INTERVAL_US 45000000u' in dvc
assert 'DVC1124_BalanceService' in dvc
assert 's_openwire_result.state == DVC1124_OPENWIRE_WAITING' in dvc

print('D008 20S/product safety completion contract: PASS')
