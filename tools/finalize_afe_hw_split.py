#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
MODBUS = HERE / "modbus_rtu.c"

text = MODBUS.read_text(encoding="utf-8")

if '#include "bms_afe_hw_access.h"' not in text:
    text = text.replace('#include "bms_afe_hw_profile.h"\n',
                        '#include "bms_afe_hw_profile.h"\n#include "bms_afe_hw_access.h"\n#include "bms_afe_hw_modbus.h"\n', 1)

start = text.find('static int afe_hw_profile_is_reg(u16 reg)')
if start < 0:
    raise SystemExit('AFE hardware profile helper block not found')
markers = [p for p in (
    text.find('static int dvc_comm_is_semantic', start),
    text.find('static u16 read_fault_history_reg', start),
) if p >= 0]
if not markers:
    raise SystemExit('AFE hardware profile helper end marker not found')
end = min(markers)

block = r'''static u16 s_afe_hw_apply_state = BMS_AFE_HW_APPLY_IDLE;
static u16 s_afe_hw_last_error = BMS_AFE_HW_ERROR_NONE;

static int afe_hw_profile_is_requested_reg(u16 reg)
{
    return (reg >= BMS_AFE_HW_REQUESTED_REG_BASE &&
            reg < (u16)(BMS_AFE_HW_REQUESTED_REG_BASE + BMS_AFE_HW_REQUESTED_REG_COUNT));
}

static int afe_hw_profile_is_effective_reg(u16 reg)
{
    return (reg >= BMS_AFE_HW_EFFECTIVE_REG_BASE &&
            reg < (u16)(BMS_AFE_HW_EFFECTIVE_REG_BASE + BMS_AFE_HW_EFFECTIVE_REG_COUNT));
}

static int afe_hw_profile_is_reg(u16 reg)
{
    return afe_hw_profile_is_requested_reg(reg) || afe_hw_profile_is_effective_reg(reg);
}

static u16 afe_hw_profile_product_shunt_uohm(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    return DVC1124_DEFAULT_SHUNT_UOHM;
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    return SH3673510_D011_SHUNT_UOHM;
#else
    return 0u;
#endif
}

static u16 afe_hw_profile_product_cell_count(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    return DVC1124_DEFAULT_CELL_COUNT;
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    return SH3673510_D011_CELL_COUNT;
#else
    return 0u;
#endif
}

static u16 afe_hw_profile_product_wdt_seconds(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    return DVC1124_I2C_WATCHDOG_SECONDS;
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    return SH3673510_D011_WDT_EN ? 32u : 0u;
#else
    return 0u;
#endif
}

static u16 afe_hw_profile_read_reg(u16 reg)
{
    bms_afe_hw_profile_t p;
    u16 offset;

    if (afe_hw_profile_is_effective_reg(reg))
    {
        offset = (u16)(reg - BMS_AFE_HW_EFFECTIVE_REG_BASE);
        if (offset >= BMS_AFE_HW_EFFECTIVE_REG_COUNT ||
            !bms_afe_hw_profile_get_effective(&p)) return 0xFFFFu;
        return ((const u16 *)&p)[offset];
    }

    if (!afe_hw_profile_is_requested_reg(reg)) return 0xFFFFu;
    offset = (u16)(reg - BMS_AFE_HW_REQUESTED_REG_BASE);
    if (offset < BMS_AFE_HW_PROFILE_WORD_COUNT)
    {
        if (!bms_afe_hw_profile_get(&p)) return 0xFFFFu;
        return ((const u16 *)&p)[offset];
    }

    switch (reg)
    {
    case BMS_AFE_HW_META_CAPABILITIES:      return bms_afe_hw_profile_capabilities();
    case BMS_AFE_HW_META_VALID:             return bms_afe_hw_profile_get(&p) ? 1u : 0u;
    case BMS_AFE_HW_META_SHUNT_UOHM:        return afe_hw_profile_product_shunt_uohm();
    case BMS_AFE_HW_META_CELL_COUNT:        return afe_hw_profile_product_cell_count();
    case BMS_AFE_HW_META_WDT_SECONDS:       return afe_hw_profile_product_wdt_seconds();
    case BMS_AFE_HW_META_ACCESS_ACTIVE:     return bms_afe_hw_access_is_active() ? 1u : 0u;
    case BMS_AFE_HW_META_APPLY_STATE:       return s_afe_hw_apply_state;
    case BMS_AFE_HW_META_LAST_ERROR:        return s_afe_hw_last_error;
    case BMS_AFE_HW_META_INTERFACE_VERSION: return BMS_AFE_HW_INTERFACE_VERSION;
    default: return 0xFFFFu;
    }
}

static u8 afe_hw_profile_words_equal(const bms_afe_hw_profile_t *a,
                                     const bms_afe_hw_profile_t *b)
{
    const u16 *wa = (const u16 *)a;
    const u16 *wb = (const u16 *)b;
    u16 i;
    for (i = 0u; i < BMS_AFE_HW_PROFILE_WORD_COUNT; ++i)
        if (wa[i] != wb[i]) return 0u;
    return 1u;
}

static u8 afe_hw_profile_rollback(const bms_afe_hw_profile_t *before)
{
    bms_afe_hw_profile_t verify;
    bms_afe_hw_profile_t effective;

    if (before == 0 ||
        !bms_afe_hw_profile_set(before) ||
        !bms_afe_apply_protection_config() ||
        !bms_afe_hw_profile_get(&verify) ||
        !afe_hw_profile_words_equal(before, &verify) ||
        !bms_afe_hw_profile_get_effective(&effective))
    {
        s_afe_hw_apply_state = BMS_AFE_HW_APPLY_INCONSISTENT;
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_ROLLBACK;
        bms_afe_hw_access_close();
        return MB_EX_DEVICE_FAILURE;
    }

    s_afe_hw_apply_state = BMS_AFE_HW_APPLY_ROLLBACK_OK;
    return MB_EX_DEVICE_FAILURE;
}

static u8 afe_hw_profile_write_block(const u8 *pdata, u16 qty)
{
    bms_afe_hw_profile_t before;
    bms_afe_hw_profile_t candidate;
    bms_afe_hw_profile_t verify;
    bms_afe_hw_profile_t effective;
    u16 i;

    if (!bms_afe_hw_access_is_active())
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_AUTH;
        return MB_EX_ILLEGAL_ADDRESS;
    }
    if (pdata == 0 || qty != BMS_AFE_HW_PROFILE_WORD_COUNT)
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_VALIDATION;
        return MB_EX_ILLEGAL_VALUE;
    }
    if (!bms_afe_hw_profile_get(&before))
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_STORE;
        return MB_EX_DEVICE_FAILURE;
    }

    candidate = before;
    for (i = 0u; i < qty; ++i)
        ((u16 *)&candidate)[i] = u16be(&pdata[(u32)i * 2u]);

    if (!bms_afe_hw_profile_validate(&candidate))
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_VALIDATION;
        return MB_EX_ILLEGAL_VALUE;
    }

    if (!bms_afe_hw_profile_set(&candidate))
    {
        s_afe_hw_apply_state = BMS_AFE_HW_APPLY_IDLE;
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_STORE;
        return MB_EX_DEVICE_FAILURE;
    }

    if (!bms_afe_apply_protection_config())
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_APPLY_VERIFY;
        return afe_hw_profile_rollback(&before);
    }

    if (!bms_afe_hw_profile_get(&verify) ||
        !afe_hw_profile_words_equal(&candidate, &verify) ||
        !bms_afe_hw_profile_get_effective(&effective))
    {
        s_afe_hw_last_error = BMS_AFE_HW_ERROR_APPLY_VERIFY;
        return afe_hw_profile_rollback(&before);
    }

    s_afe_hw_apply_state = BMS_AFE_HW_APPLY_OK;
    s_afe_hw_last_error = BMS_AFE_HW_ERROR_NONE;
    bms_afe_hw_access_close();
    return 0u;
}

'''
text = text[:start] + block + text[end:]

needle = '    addr = req[0];\n    func = req[1];\n'
if needle not in text:
    raise SystemExit('modbus func dispatch anchor not found')
if 'bms_afe_hw_access_modbus_on_frame(req' not in text:
    text = text.replace(needle, needle + r'''

    if (func == BMS_AFE_HW_ACCESS_MODBUS_FUNC)
    {
        if (addr == 0x00u) return 0;
        return bms_afe_hw_access_modbus_on_frame(req, req_len, rsp, rsp_len);
    }
''', 1)

old = '''        if (reg == BMS_AFE_HW_PROFILE_REG_BASE) {
            if (qty != BMS_AFE_HW_PROFILE_WORDS)
                return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);
            exception = afe_hw_profile_write_block(pdata, qty);'''
new = '''        if (reg == BMS_AFE_HW_REQUESTED_REG_BASE) {
            if (addr == 0x00u) return 0;
            if (qty != BMS_AFE_HW_PROFILE_WORD_COUNT)
                return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);
            exception = afe_hw_profile_write_block(pdata, qty);'''
if old not in text:
    raise SystemExit('AFE hardware block write anchor not found')
text = text.replace(old, new, 1)

MODBUS.write_text(text, encoding='utf-8')

# Keep source ordering deterministic.
source_order = ROOT / 'bms_tools' / 'source_order.txt'
s = source_order.read_text(encoding='utf-8')
entry = 'vendor/ble_sample/bms_afe_hw_access.c\n'
if entry not in s:
    anchor = 'vendor/ble_sample/bms_afe_guard.c\n'
    if anchor not in s:
        raise SystemExit('source order AFE guard anchor not found')
    s = s.replace(anchor, anchor + entry, 1)
    source_order.write_text(s, encoding='utf-8')

# Contract test shared by all three product branches.
test = ROOT / 'tests' / 'afe_hw_access_contract_check.py'
test.write_text(r'''#!/usr/bin/env python3
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
''', encoding='utf-8')

print('final AFE hardware split migration applied')
