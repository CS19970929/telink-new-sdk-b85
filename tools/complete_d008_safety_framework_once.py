#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001' / 'tc_ble_single_sdk' / 'vendor' / 'ble_sample'

def rw(name):
    p = HERE / name
    return p, p.read_text(encoding='utf-8')

def write(p, s):
    p.write_text(s, encoding='utf-8')

def replace_once(s, old, new, label):
    if old not in s:
        raise SystemExit(f'anchor not found: {label}')
    return s.replace(old, new, 1)

# -----------------------------------------------------------------------------
# 1) D008 explicit DVC mask policy: all non-WDT mask bits stay at V1.2 reset
#    semantics; only the already-persisted timeout-close choices may overlay.
# -----------------------------------------------------------------------------
p, s = rw('dvc1124_project_config.h')
anchor = '''#ifndef DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH\n#define DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH    16u\n#endif\n'''
insert = anchor + '''\n/*\n * DVC 0x53/0x54 are entirely documented mask registers.  D008 has no signed-off\n * product policy for the non-watchdog sources yet, therefore make the policy\n * explicit and reset-equivalent instead of inheriting an unknown live value.\n * The timeout bits DWM/CWM are overlaid from the persisted semantic options.\n */\n#ifndef DVC1124_DEFAULT_DSG_MASK_POLICY\n#define DVC1124_DEFAULT_DSG_MASK_POLICY          DVC1124_DSG_MASK_RESET\n#endif\n#ifndef DVC1124_DEFAULT_CHG_MASK_POLICY\n#define DVC1124_DEFAULT_CHG_MASK_POLICY          DVC1124_CHG_MASK_RESET\n#endif\n'''
s = replace_once(s, anchor, insert, 'project mask policy')
write(p, s)

# -----------------------------------------------------------------------------
# 2) Persistent configuration normalization + explicit mask programming.
# -----------------------------------------------------------------------------
p, s = rw('dvc1124_config_store.c')
s = replace_once(s,
'''static uint8_t s_restore_pending = 1u;\nstatic uint8_t s_kv_ready;\n''',
'''static uint8_t s_restore_pending = 1u;\nstatic uint8_t s_kv_ready;\nstatic uint8_t s_last_load_normalized;\n''', 'config store globals')

anchor = '''static int dvc_cfg_gp236_ok(uint8_t value)\n{\n    return ((value <= 2u) || (value == 6u) || (value == 7u)) ? 1 : 0;\n}\n'''
insert = anchor + '''\n/* Preserve unrelated legacy fields while enforcing D008 board invariants. */\nstatic uint8_t dvc_cfg_normalize_product_policy(dvc1124_persistent_config_t *cfg)\n{\n    uint8_t changed = 0u;\n    if (cfg == NULL) return 0u;\n\n    if (cfg->operating.high_side_fet_mask != DVC1124_DEFAULT_HIGH_SIDE_FET_MASK)\n    {\n        cfg->operating.high_side_fet_mask = DVC1124_DEFAULT_HIGH_SIDE_FET_MASK;\n        changed = 1u;\n    }\n    if ((cfg->current_wake_threshold_uv == 0u) && cfg->operating.current_wake_enable)\n    {\n        cfg->operating.current_wake_enable = 0u;\n        changed = 1u;\n    }\n    return changed;\n}\n\nstatic uint8_t dvc_cfg_dsg_mask_policy(const dvc1124_persistent_config_t *cfg)\n{\n    uint8_t value = DVC1124_DEFAULT_DSG_MASK_POLICY;\n    if (cfg->i2c_timeout_close_dsg) value &= (uint8_t)~DVC1124_DSGMASK_DWM_MASK;\n    else value |= DVC1124_DSGMASK_DWM_MASK;\n    return value;\n}\n\nstatic uint8_t dvc_cfg_chg_mask_policy(const dvc1124_persistent_config_t *cfg)\n{\n    uint8_t value = DVC1124_DEFAULT_CHG_MASK_POLICY;\n    if (cfg->i2c_timeout_close_chg) value &= (uint8_t)~DVC1124_CHGMASK_CWM_MASK;\n    else value |= DVC1124_CHGMASK_CWM_MASK;\n    return value;\n}\n'''
s = replace_once(s, anchor, insert, 'normalization helpers')

s = replace_once(s,
'''    if (!((cfg->operating.i2c_watchdog == DVC1124_I2C_WDT_OFF) ||\n''',
'''    /* D008 never uses the unconnected high-side FET path.  Also reject the\n     * incoherent CAES=1/CWT=0 state instead of silently enabling wake logic. */\n    if (cfg->operating.high_side_fet_mask != DVC1124_DEFAULT_HIGH_SIDE_FET_MASK) return 0;\n    if ((cfg->current_wake_threshold_uv == 0u) && cfg->operating.current_wake_enable) return 0;\n\n    if (!((cfg->operating.i2c_watchdog == DVC1124_I2C_WDT_OFF) ||\n''', 'validate product invariants')

s = replace_once(s,
'''    memset(cfg, 0, sizeof(*cfg));\n    dvc_cfg_unpack_operating(operating, cfg);\n''',
'''    s_last_load_normalized = 0u;\n    memset(cfg, 0, sizeof(*cfg));\n    dvc_cfg_unpack_operating(operating, cfg);\n''', 'load normalization reset')

s = replace_once(s,
'''    dvc_cfg_unpack_hw_misc(hw_misc, cfg);\n    dvc_cfg_unpack_scd(scd, cfg);\n\n    return DVC1124_ConfigStoreValidate(cfg);\n''',
'''    dvc_cfg_unpack_hw_misc(hw_misc, cfg);\n    dvc_cfg_unpack_scd(scd, cfg);\n    s_last_load_normalized = dvc_cfg_normalize_product_policy(cfg);\n\n    return DVC1124_ConfigStoreValidate(cfg);\n''', 'load normalization apply')

s = replace_once(s,
'''int DVC1124_ConfigStoreApply(const dvc1124_persistent_config_t *cfg)\n{\n    uint8_t cwt;\n    uint8_t bdpt;\n    uint8_t ok = 1u;\n''',
'''int DVC1124_ConfigStoreApply(const dvc1124_persistent_config_t *cfg)\n{\n    uint8_t cwt;\n    uint8_t bdpt;\n    uint8_t dsg_mask;\n    uint8_t chg_mask;\n    uint8_t ok = 1u;\n''', 'apply mask locals')

old = '''    /* Configure timeout behavior before a stored non-zero I2C watchdog is enabled. */\n    ok &= DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CHG_MASK,\n                                          DVC1124_CHGMASK_CWM_MASK,\n                                          7u,\n                                          cfg->i2c_timeout_close_chg ? 0u : 1u);\n    ok &= DVC1124_WriteRegisterFieldSafe(DVC1124_REG_DSG_MASK,\n                                          DVC1124_DSGMASK_DWM_MASK,\n                                          3u,\n                                          cfg->i2c_timeout_close_dsg ? 0u : 1u);\n'''
new = '''    /* Program the full documented 0x53/0x54 mask policy deterministically.\n     * Non-watchdog bits remain reset-equivalent until product review signs off\n     * a different policy; DWM/CWM alone follow the persisted semantic options. */\n    dsg_mask = dvc_cfg_dsg_mask_policy(cfg);\n    chg_mask = dvc_cfg_chg_mask_policy(cfg);\n    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_DSG_MASK, dsg_mask);\n    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_CHG_MASK, chg_mask);\n'''
s = replace_once(s, old, new, 'apply explicit mask policy')

s = replace_once(s,
'''    return DVC1124_ConfigStoreValidate(cfg);\n}\n\nint DVC1124_ConfigStoreCaptureAndSave(void)\n''',
'''    (void)dvc_cfg_normalize_product_policy(cfg);\n    return DVC1124_ConfigStoreValidate(cfg);\n}\n\nint DVC1124_ConfigStoreCaptureAndSave(void)\n''', 'capture normalization')

s = replace_once(s,
'''    if (!DVC1124_ConfigStoreApply(&cfg)) return 0;\n    s_restore_pending = 0u;\n    return 1;\n}\n''',
'''    if (!DVC1124_ConfigStoreApply(&cfg)) return 0;\n    if (s_last_load_normalized)\n    {\n        /* Best-effort one-time persistence of normalized legacy fields.  A\n         * later semantic write will persist the same normalized values again. */\n        (void)DVC1124_ConfigStoreSave(&cfg);\n    }\n    s_restore_pending = 0u;\n    return 1;\n}\n''', 'restore persist normalized')
write(p, s)

# -----------------------------------------------------------------------------
# 3) DVC semantic config transaction: verified rollback failure has an explicit
#    CONFIG_INCONSISTENT diagnostic instead of being silently discarded.
# -----------------------------------------------------------------------------
p, s = rw('dvc1124_config_service.h')
s = replace_once(s,
'''    DVC1124_CFG_CORE_OT_EVENT_LATCHED      = 0x08, /* sticky software copy of COTF */\n''',
'''    DVC1124_CFG_CORE_OT_EVENT_LATCHED      = 0x08, /* sticky software copy of COTF */\n    DVC1124_CFG_CONFIG_INCONSISTENT         = 0x09, /* previous rollback failed */\n''', 'service diagnostic field')
s = replace_once(s,
'''    DVC1124_CFG_ERR_STORE,\n    DVC1124_CFG_ERR_FORBIDDEN,\n''',
'''    DVC1124_CFG_ERR_STORE,\n    DVC1124_CFG_ERR_FORBIDDEN,\n    DVC1124_CFG_ERR_INCONSISTENT,\n''', 'service inconsistent enum')
write(p, s)

p, s = rw('dvc1124_config_service.c')
s = replace_once(s,
'''#include <string.h>\n\nstatic dvc1124_config_result_t dvc_cfg_load''',
'''#include <string.h>\n\nstatic u8 s_config_inconsistent;\n\nstatic dvc1124_config_result_t dvc_cfg_load''', 'service state')

old = '''static dvc1124_config_result_t dvc_cfg_apply_store_transaction(\n    const dvc1124_persistent_config_t *before,\n    const dvc1124_persistent_config_t *after)\n{\n    if ((before == NULL) || (after == NULL)) return DVC1124_CFG_ERR_VALUE;\n    if (!DVC1124_ConfigStoreValidate(after)) return DVC1124_CFG_ERR_VALUE;\n\n    if (!DVC1124_ConfigStoreApply(after)) return DVC1124_CFG_ERR_AFE_IO;\n\n    if (!DVC1124_ConfigStoreSave(after))\n    {\n        /* Do not leave live AFE and persistent source-of-truth divergent. */\n        (void)DVC1124_ConfigStoreApply(before);\n        return DVC1124_CFG_ERR_STORE;\n    }\n\n    return DVC1124_CFG_OK;\n}\n'''
new = '''static dvc1124_config_result_t dvc_cfg_apply_store_transaction(\n    const dvc1124_persistent_config_t *before,\n    const dvc1124_persistent_config_t *after)\n{\n    if ((before == NULL) || (after == NULL)) return DVC1124_CFG_ERR_VALUE;\n    if (!DVC1124_ConfigStoreValidate(after)) return DVC1124_CFG_ERR_VALUE;\n\n    if (!DVC1124_ConfigStoreApply(after))\n    {\n        /* Apply may have changed a subset of registers before failing. */\n        if (!DVC1124_ConfigStoreApply(before))\n        {\n            s_config_inconsistent = 1u;\n            return DVC1124_CFG_ERR_INCONSISTENT;\n        }\n        return DVC1124_CFG_ERR_AFE_IO;\n    }\n\n    if (!DVC1124_ConfigStoreSave(after))\n    {\n        uint8_t live_ok = DVC1124_ConfigStoreApply(before) ? 1u : 0u;\n        uint8_t store_ok = DVC1124_ConfigStoreSave(before) ? 1u : 0u;\n        if (!live_ok || !store_ok)\n        {\n            s_config_inconsistent = 1u;\n            return DVC1124_CFG_ERR_INCONSISTENT;\n        }\n        return DVC1124_CFG_ERR_STORE;\n    }\n\n    s_config_inconsistent = 0u;\n    return DVC1124_CFG_OK;\n}\n'''
s = replace_once(s, old, new, 'service transaction rollback')

s = replace_once(s,
'''    case DVC1124_CFG_CORE_OT_EVENT_LATCHED:\n        *value = DVC1124_GetCoreOtEventLatched();\n        return DVC1124_CFG_OK;\n''',
'''    case DVC1124_CFG_CORE_OT_EVENT_LATCHED:\n        *value = DVC1124_GetCoreOtEventLatched();\n        return DVC1124_CFG_OK;\n    case DVC1124_CFG_CONFIG_INCONSISTENT:\n        *value = s_config_inconsistent;\n        return DVC1124_CFG_OK;\n''', 'service read inconsistent')

# Lock the physically unused high-side path at the semantic API boundary.
s = replace_once(s,
'''    case DVC1124_CFG_HS_FET_MASK: if (value > 1u) return DVC1124_CFG_ERR_VALUE; after.operating.high_side_fet_mask = (u8)value; break;\n''',
'''    case DVC1124_CFG_HS_FET_MASK:\n        if (value != DVC1124_DEFAULT_HIGH_SIDE_FET_MASK) return DVC1124_CFG_ERR_FORBIDDEN;\n        after.operating.high_side_fet_mask = (u8)value;\n        break;\n''', 'service lock high-side mask')
write(p, s)

p, s = rw('modbus_rtu.c')
s = replace_once(s,
'''    case DVC1124_CFG_ERR_AFE_IO:\n    case DVC1124_CFG_ERR_STORE:\n    default:\n''',
'''    case DVC1124_CFG_ERR_AFE_IO:\n    case DVC1124_CFG_ERR_STORE:\n    case DVC1124_CFG_ERR_INCONSISTENT:\n    default:\n''', 'modbus inconsistent mapping')
write(p, s)

# -----------------------------------------------------------------------------
# 4) DVC driver: product cell count is authoritative, deterministic CADC/masks,
#    a non-interpreting open-wire FSM, and bounded balance refresh.
# -----------------------------------------------------------------------------
p, s = rw('dvc1124.h')
s = replace_once(s,
'''} dvc1124_snapshot_t;\n\n/*\n * Named operating configuration.''',
'''} dvc1124_snapshot_t;\n\ntypedef enum\n{\n    DVC1124_OPENWIRE_IDLE = 0u,\n    DVC1124_OPENWIRE_WAITING = 1u,\n    DVC1124_OPENWIRE_READY = 2u,\n    DVC1124_OPENWIRE_ERROR = 3u,\n} dvc1124_openwire_state_t;\n\ntypedef struct\n{\n    dvc1124_openwire_state_t state;\n    uint8_t valid;\n    uint8_t cell_count;\n    uint16_t cell_mv[DVC1124_MAX_CELLS];\n    uint32_t pack_mv;\n} dvc1124_openwire_result_t;\n\n/*\n * Named operating configuration.''', 'openwire types')

s = replace_once(s,
'''uint8_t DVC1124_SetBalanceMask(uint32_t cell_mask);\nuint8_t DVC1124_StartOpenWireCheck(void);\n''',
'''uint8_t DVC1124_SetBalanceMask(uint32_t cell_mask);\nvoid DVC1124_BalanceService(uint8_t allow_refresh);\nuint8_t DVC1124_StartOpenWireCheck(void);\nuint8_t DVC1124_OpenWireBegin(void);\nvoid DVC1124_OpenWirePoll(void);\nvoid DVC1124_OpenWireGetResult(dvc1124_openwire_result_t *result);\nvoid DVC1124_OpenWireReset(void);\n''', 'openwire/balance APIs')
write(p, s)

p, s = rw('dvc1124.c')
s = replace_once(s,
'''static uint8_t s_output_enabled;\nstatic uint32_t s_balance_requested_mask;\n''',
'''static uint8_t s_output_enabled;\nstatic uint32_t s_balance_requested_mask;\nstatic uint32_t s_balance_last_refresh_tick;\nstatic uint8_t s_balance_suspended;\nstatic uint32_t s_snapshot_generation;\nstatic uint32_t s_openwire_start_generation;\nstatic uint32_t s_openwire_start_tick;\nstatic dvc1124_openwire_result_t s_openwire_result;\n\n#define DVC_BALANCE_REFRESH_INTERVAL_US 45000000u\n#define DVC_OPENWIRE_SETTLE_US           1200000u\n''', 'driver safety state')

s = replace_once(s,
'''    uint8_t watchdog_code;\n    uint8_t cpvs_bits;\n''',
'''    uint8_t watchdog_code;\n    uint8_t cpvs_bits;\n    uint8_t cadc_bits = 0u;\n    uint8_t dsg_mask = DVC1124_DEFAULT_DSG_MASK_POLICY;\n    uint8_t chg_mask = DVC1124_DEFAULT_CHG_MASK_POLICY;\n''', 'basic config locals')

old = '''    /* HS-D008 uses GP5/GP6 low-side CHG/DSG. Enable CADC in work and sleep. */\n    ok &= dvc_update_reg(DVC1124_REG_CADC_CTRL,\n                         (uint8_t)(DVC1124_CADC_HSFM_MASK |\n                                   DVC1124_CADC_CAEW_MASK |\n                                   DVC1124_CADC_CAES_MASK),\n                         (uint8_t)(DVC1124_CADC_CAEW_MASK |\n                                   DVC1124_CADC_CAES_MASK));\n'''
new = '''    /* HS-D008 uses GP5/GP6 low-side CHG/DSG.  Apply the reviewed defaults\n     * literally so reset-time configuration cannot transiently unmask the\n     * unused high-side path or enable CAES while CWT is zero. */\n    if (DVC1124_DEFAULT_HIGH_SIDE_FET_MASK) cadc_bits |= DVC1124_CADC_HSFM_MASK;\n    if (DVC1124_DEFAULT_CADC_WORK_ENABLE) cadc_bits |= DVC1124_CADC_CAEW_MASK;\n    if (DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE) cadc_bits |= DVC1124_CADC_CAES_MASK;\n    ok &= dvc_update_reg(DVC1124_REG_CADC_CTRL,\n                         (uint8_t)(DVC1124_CADC_HSFM_MASK |\n                                   DVC1124_CADC_CAEW_MASK |\n                                   DVC1124_CADC_CAES_MASK),\n                         cadc_bits);\n'''
s = replace_once(s, old, new, 'deterministic CADC defaults')

old = '''#if DVC1124_I2C_TIMEOUT_CLOSE_DSG\n    ok &= dvc_update_reg(DVC1124_REG_DSG_MASK, DVC1124_DSGMASK_DWM_MASK, 0u);\n#else\n    ok &= dvc_update_reg(DVC1124_REG_DSG_MASK,\n                         DVC1124_DSGMASK_DWM_MASK,\n                         DVC1124_DSGMASK_DWM_MASK);\n#endif\n#if DVC1124_I2C_TIMEOUT_CLOSE_CHG\n    ok &= dvc_update_reg(DVC1124_REG_CHG_MASK, DVC1124_CHGMASK_CWM_MASK, 0u);\n#else\n    ok &= dvc_update_reg(DVC1124_REG_CHG_MASK,\n                         DVC1124_CHGMASK_CWM_MASK,\n                         DVC1124_CHGMASK_CWM_MASK);\n#endif\n'''
new = '''#if DVC1124_I2C_TIMEOUT_CLOSE_DSG\n    dsg_mask &= (uint8_t)~DVC1124_DSGMASK_DWM_MASK;\n#else\n    dsg_mask |= DVC1124_DSGMASK_DWM_MASK;\n#endif\n#if DVC1124_I2C_TIMEOUT_CLOSE_CHG\n    chg_mask &= (uint8_t)~DVC1124_CHGMASK_CWM_MASK;\n#else\n    chg_mask |= DVC1124_CHGMASK_CWM_MASK;\n#endif\n    ok &= dvc_write_verified(DVC1124_REG_DSG_MASK, dsg_mask);\n    ok &= dvc_write_verified(DVC1124_REG_CHG_MASK, chg_mask);\n'''
s = replace_once(s, old, new, 'basic config full mask policy')

s = replace_once(s,
'''    if (!DVC1124_SetCellCount((uint8_t)SeriesNum))\n''',
'''    /* Physical D008 assembly profile is authoritative for AFE channel use. */\n    if (!DVC1124_SetCellCount((uint8_t)DVC1124_DEFAULT_CELL_COUNT))\n''', 'product cell count runtime')

# Replace balance/open-wire block with safe service + raw-result FSM.
old_start = s.index('static uint8_t dvc_refresh_balance_state(void)')
old_end = s.index('uint8_t DVC1124_SetShortCircuitProtection', old_start)
new_block = r'''static uint32_t dvc_valid_cell_mask(void)
{
    return (s_cfg.cell_count >= 24u)
               ? 0x00FFFFFFu
               : ((1uL << s_cfg.cell_count) - 1uL);
}

static uint8_t dvc_refresh_balance_state(void)
{
    uint8_t data[3];
    uint32_t actual;

    if (!DVC1124_ReadRegisters(DVC1124_REG_BAL_24_17, data, 3u)) return 0u;
    actual = (((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2]) & dvc_valid_cell_mask();
    g_stCellInfoReport.u16BalanceFlag1 = (uint16_t)(actual & 0xFFFFu);
    g_stCellInfoReport.u16BalanceFlag2 = (uint16_t)((actual >> 16) & 0x00FFu);
    return 1u;
}

static uint8_t dvc_write_balance_hw(uint32_t cell_mask)
{
    uint8_t data[3];
    cell_mask &= dvc_valid_cell_mask();
    data[0] = (uint8_t)(cell_mask >> 16);
    data[1] = (uint8_t)(cell_mask >> 8);
    data[2] = (uint8_t)cell_mask;
    if (!dvc_write_verified_block(DVC1124_REG_BAL_24_17, data, 3u)) return 0u;
    return dvc_refresh_balance_state();
}

uint8_t DVC1124_SetBalanceMask(uint32_t cell_mask)
{
    s_balance_requested_mask = cell_mask & dvc_valid_cell_mask();
    if (s_balance_requested_mask == 0u)
    {
        s_balance_suspended = 0u;
        s_balance_last_refresh_tick = clock_time();
        return dvc_write_balance_hw(0u);
    }

    /* A non-zero request is armed, not immediately energized.  The BMS-side
     * service applies/renews it only while charge/fault conditions allow. */
    s_balance_suspended = 1u;
    return dvc_refresh_balance_state();
}

void DVC1124_BalanceService(uint8_t allow_refresh)
{
    if (s_balance_requested_mask == 0u)
    {
        (void)dvc_refresh_balance_state();
        return;
    }

    if (!allow_refresh || (s_openwire_result.state == DVC1124_OPENWIRE_WAITING))
    {
        if (!s_balance_suspended)
        {
            if (dvc_write_balance_hw(0u)) s_balance_suspended = 1u;
        }
        return;
    }

    if (s_balance_suspended ||
        clock_time_exceed(s_balance_last_refresh_tick, DVC_BALANCE_REFRESH_INTERVAL_US))
    {
        if (dvc_write_balance_hw(s_balance_requested_mask))
        {
            s_balance_suspended = 0u;
            s_balance_last_refresh_tick = clock_time();
        }
    }
    else
    {
        (void)dvc_refresh_balance_state();
    }
}

uint8_t DVC1124_StartOpenWireCheck(void)
{
    uint8_t reg;

    /* COW is self-clearing after about 1 s, so do not require persistent readback=1. */
    if (!DVC1124_ReadRegisters(DVC1124_REG_CP_CTRL, &reg, 1u)) return 0u;
    reg |= DVC1124_COW_MASK;
    return DVC1124_WriteRegisters(DVC1124_REG_CP_CTRL, &reg, 1u);
}

void DVC1124_OpenWireReset(void)
{
    memset(&s_openwire_result, 0, sizeof(s_openwire_result));
    s_openwire_result.state = DVC1124_OPENWIRE_IDLE;
    s_openwire_start_tick = 0u;
    s_openwire_start_generation = s_snapshot_generation;
}

uint8_t DVC1124_OpenWireBegin(void)
{
    if (s_openwire_result.state == DVC1124_OPENWIRE_WAITING) return 0u;

    if (!s_balance_suspended && s_balance_requested_mask != 0u)
    {
        if (!dvc_write_balance_hw(0u)) return 0u;
        s_balance_suspended = 1u;
    }

    memset(&s_openwire_result, 0, sizeof(s_openwire_result));
    s_openwire_result.state = DVC1124_OPENWIRE_WAITING;
    s_openwire_start_generation = s_snapshot_generation;
    s_openwire_start_tick = clock_time();
    if (!DVC1124_StartOpenWireCheck())
    {
        s_openwire_result.state = DVC1124_OPENWIRE_ERROR;
        return 0u;
    }
    return 1u;
}

void DVC1124_OpenWirePoll(void)
{
    uint8_t cp;

    if (s_openwire_result.state != DVC1124_OPENWIRE_WAITING) return;
    if (!clock_time_exceed(s_openwire_start_tick, DVC_OPENWIRE_SETTLE_US)) return;
    if (!DVC1124_ReadRegisters(DVC1124_REG_CP_CTRL, &cp, 1u))
    {
        s_openwire_result.state = DVC1124_OPENWIRE_ERROR;
        return;
    }
    if (cp & DVC1124_COW_MASK) return;
    if (!s_snapshot.valid || s_snapshot_generation == s_openwire_start_generation) return;

    s_openwire_result.valid = 1u;
    s_openwire_result.cell_count = s_snapshot.cell_count;
    memcpy(s_openwire_result.cell_mv, s_snapshot.cell_mv, sizeof(s_openwire_result.cell_mv));
    s_openwire_result.pack_mv = s_snapshot.pack_mv;
    s_openwire_result.state = DVC1124_OPENWIRE_READY;
}

void DVC1124_OpenWireGetResult(dvc1124_openwire_result_t *result)
{
    if (result != NULL) *result = s_openwire_result;
}

'''
s = s[:old_start] + new_block + s[old_end:]

s = replace_once(s,
'''    memset(&s_snapshot, 0, sizeof(s_snapshot));\n    memset(&s_applied, 0, sizeof(s_applied));\n    s_need_config = 1u;\n''',
'''    memset(&s_snapshot, 0, sizeof(s_snapshot));\n    memset(&s_applied, 0, sizeof(s_applied));\n    s_balance_requested_mask = 0u;\n    s_balance_suspended = 0u;\n    s_balance_last_refresh_tick = clock_time();\n    s_snapshot_generation = 0u;\n    DVC1124_OpenWireReset();\n    s_need_config = 1u;\n''', 'reset safety states')

s = replace_once(s,
'''    /* 0x67..0x69 auto-clear after 60 s; report actual AFE state, not cached request. */\n    if (!dvc_refresh_balance_state())\n''',
'''    ++s_snapshot_generation;\n    DVC1124_OpenWirePoll();\n\n    /* 0x67..0x69 auto-clear after 60 s; report actual AFE state, not cached request. */\n    if (!dvc_refresh_balance_state())\n''', 'sample generation/openwire poll')
write(p, s)

# BMS-side balance gate: only renew an already-requested mask on a valid sample,
# during charging, with no charge/discharge fault block active.
p, s = rw('dvc1124_bms.c')
s = replace_once(s,
'''    dvc_merge_hw_faults(alarm);\n    bms_sw_protection_record_fault_edges();\n\n    /*\n''',
'''    dvc_merge_hw_faults(alarm);\n    bms_sw_protection_record_fault_edges();\n    DVC1124_BalanceService((uint8_t)((g_stCellInfoReport.u16Ichg > 0u) &&\n                                     !dvc_charge_blocked() &&\n                                     !dvc_discharge_blocked()));\n\n    /*\n''', 'BMS balance safety gate')
write(p, s)

# -----------------------------------------------------------------------------
# 5) Contract tests, including explicit 20S/NMC channel-mask proof.
# -----------------------------------------------------------------------------
test = ROOT / 'tests' / 'd008_20s_profile_contract_check.py'
test.write_text(r'''#!/usr/bin/env python3
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
''', encoding='utf-8')

# Extend existing architecture contract with the newly fixed reset-time policy.
p = ROOT / 'tests' / 'd008_framework_contract_check.py'
t = p.read_text(encoding='utf-8')
t = t.replace(
'''        cls.dvc_bms = read(HERE / "dvc1124_bms.c")\n''',
'''        cls.dvc_bms = read(HERE / "dvc1124_bms.c")\n        cls.dvc = read(HERE / "dvc1124.c")\n        cls.store = read(HERE / "dvc1124_config_store.c")\n        cls.service = read(HERE / "dvc1124_config_service.c")\n''', 1)
needle = '''    def test_unverified_safety_features_remain_explicitly_disabled(self):\n'''
addition = '''    def test_d008_runtime_enforces_product_cell_count_and_mask_policy(self):\n        self.assertIn("DVC1124_SetCellCount((uint8_t)DVC1124_DEFAULT_CELL_COUNT)", self.dvc)\n        self.assertIn("DVC1124_DEFAULT_DSG_MASK_POLICY", self.cfg)\n        self.assertIn("DVC1124_DEFAULT_CHG_MASK_POLICY", self.cfg)\n        self.assertIn("dvc_cfg_normalize_product_policy", self.store)\n        self.assertIn("DVC1124_CFG_ERR_INCONSISTENT", self.service)\n\n    def test_openwire_is_raw_fsm_and_balance_refresh_is_safety_gated(self):\n        self.assertIn("DVC1124_OpenWireBegin", self.dvc)\n        self.assertIn("DVC1124_OpenWirePoll", self.dvc)\n        self.assertIn("DVC_BALANCE_REFRESH_INTERVAL_US 45000000u", self.dvc)\n        self.assertIn("DVC1124_BalanceService", self.dvc_bms)\n        self.assertIn("g_stCellInfoReport.u16Ichg > 0u", self.dvc_bms)\n\n'''
if needle not in t:
    raise SystemExit('d008 framework insertion anchor missing')
t = t.replace(needle, addition + needle, 1)
p.write_text(t, encoding='utf-8')

# CI must run the 20S contract on both host and TC32 jobs.
p = ROOT / '.github' / 'workflows' / 'afe-hw-split-ci.yml'
y = p.read_text(encoding='utf-8')
y = y.replace(
'      - run: python tests/d008_framework_contract_check.py\n',
'      - run: python tests/d008_framework_contract_check.py\n      - run: python tests/d008_20s_profile_contract_check.py\n', 1)
y = y.replace(
'          python tests/d008_framework_contract_check.py\n          python tests/sw_protection_contract_check.py\n',
'          python tests/d008_framework_contract_check.py\n          python tests/d008_20s_profile_contract_check.py\n          python tests/sw_protection_contract_check.py\n', 1)
p.write_text(y, encoding='utf-8')

print('D008 safety framework completion applied')
