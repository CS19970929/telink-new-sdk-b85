#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
CONTROL = VENDOR / "sh3673510_control.c"
REG = VENDOR / "sh3673520_reg.h"
TEST = ROOT / "tests" / "sh3673510_d011_integration_check.py"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        if new in text:
            return text
        raise RuntimeError(f"{label}: expected source block not found")
    return text.replace(old, new, 1)


# Existing host contracts intentionally parse numeric register defines with a
# strict line-oriented regex. Keep comments on their own lines so every field
# macro remains machine-readable as well as human-readable.
reg_text = REG.read_text(encoding="utf-8")
reg_text = re.sub(r"(?m)^(\s*#define\s+SH3673520_[A-Z0-9_]+\s+[^/\n]+?)\s*/\*.*?\*/\s*$", r"\1", reg_text)
REG.write_text(reg_text, encoding="utf-8", newline="\n")

control = CONTROL.read_text(encoding="utf-8")
old_runtime = '''static uint8_t sh3510_configure_runtime(void)
{
    uint8_t ok = 1u;

    ok &= (SH3673520_SetCellCount(SH3673510_D011_CELL_COUNT) == SH3673520_OK);

    ok &= sh3510_update_reg(SH3673520_REG_SCONF2,
                            (uint8_t)(SH3673520_SCONF2_PUMP_EN_MASK |
                                      SH3673520_SCONF2_PDSGMOS_MASK |
                                      SH3673520_SCONF2_FET_MASK),
                            SH3673520_SCONF2_PUMP_EN_MASK);

    ok &= sh3510_update_reg(SH3673520_REG_SCONF3,
                            (uint8_t)(SH3673520_SCONF3_CGR_WK_MASK |
                                      SH3673520_SCONF3_CRLD_EN_MASK |
                                      SH3673520_SCONF3_LD_WK_MASK),
                            (uint8_t)(SH3673520_SCONF3_CGR_WK_MASK |
                                      SH3673520_SCONF3_CRLD_CPLUS));

    ok &= sh3510_update_reg(SH3673520_REG_SCONF5,
                            (uint8_t)(SH3673520_SCONF5_MOS_EN_MASK |
                                      SH3673520_SCONF5_OCC_EN_MASK |
                                      SH3673520_SCONF5_CADC_EN_MASK |
                                      SH3673520_SCONF5_WDT_EN_MASK |
                                      SH3673520_SCONF5_WDT_MASK),
                            (uint8_t)(SH3673520_SCONF5_MOS_EN_MASK |
                                      SH3673520_SCONF5_OCC_EN_MASK |
                                      SH3673520_SCONF5_CADC_EN_MASK |
                                      SH3673520_SCONF5_WDT_EN_MASK |
                                      SH3673510_D011_WDT_CODE));
    return ok;
}
'''
new_runtime = '''typedef struct {
    uint8_t reg;
    uint8_t value;
    uint8_t verify_mask;
} sh3510_static_reg_cfg_t;

/*
 * Deterministic D011 static AFE profile.  Protection thresholds (0x49..0x54)
 * are applied separately from g_tParam.protect, and SCONF6 is written only
 * after those thresholds are valid so hardware protection is never enabled
 * against an unintended reset threshold.
 */
static const sh3510_static_reg_cfg_t s_static_reg_cfg[] = {
    { SH3673520_REG_SCONF1,     SH3673510_D011_SCONF1_BOOT_VALUE, 0xFFu },
    { SH3673520_REG_SCONF2,     SH3673510_D011_SCONF2_VALUE,      SH3673520_SCONF2_ALL_MASK },
    { SH3673520_REG_SCONF3,     SH3673510_D011_SCONF3_VALUE,      SH3673520_SCONF3_CONFIG_MASK },
    { SH3673520_REG_SCONF4,     SH3673510_D011_SCONF4_VALUE,      SH3673520_SCONF4_ALL_MASK },
    { SH3673520_REG_SCONF5,     SH3673510_D011_SCONF5_VALUE,      SH3673520_SCONF5_CONFIG_MASK },
    { SH3673520_REG_SCONF7,     SH3673510_D011_SCONF7_VALUE,      SH3673520_SCONF7_CONFIG_MASK },
    { SH3673520_REG_OWV_ALARMH, SH3673510_D011_OWV_ALARMH_VALUE,  SH3673520_ALARMH_ALL_MASK },
    { SH3673520_REG_ALARML,     SH3673510_D011_ALARML_VALUE,      SH3673520_ALARML_ALL_MASK },
};

static uint8_t sh3510_configure_runtime(void)
{
    uint8_t i;

    for (i = 0u; i < (uint8_t)(sizeof(s_static_reg_cfg) / sizeof(s_static_reg_cfg[0])); ++i)
    {
        if (!sh3510_write_verify(s_static_reg_cfg[i].reg,
                                 s_static_reg_cfg[i].value,
                                 s_static_reg_cfg[i].verify_mask))
            return 0u;
    }

    return (SH3673520_SetBalanceMask(0u, SH3673510_D011_CELL_COUNT) == SH3673520_OK) ? 1u : 0u;
}
'''
control = replace_once(control, old_runtime, new_runtime, "runtime profile")

old_sconf6 = '''    ok &= sh3510_update_reg(SH3673520_REG_SCONF6, 0xFFu,
                            (uint8_t)(SH3673520_SCONF6_TS2_EN_MASK |
                                      SH3673520_SCONF6_TS1_EN_MASK |
                                      SH3673520_SCONF6_ALL_PROTECT_MASK));
'''
new_sconf6 = '''    /* Enable the selected hardware protections only after all thresholds are valid. */
    ok &= sh3510_write_verify(SH3673520_REG_SCONF6,
                              SH3673510_D011_SCONF6_VALUE,
                              SH3673520_SCONF6_ALL_MASK);
'''
control = replace_once(control, old_sconf6, new_sconf6, "SCONF6 profile")
CONTROL.write_text(control, encoding="utf-8", newline="\n")

# Strengthen the existing integration contract so later cleanup cannot silently
# re-introduce reset-value dependencies or hide individual profile bits.
test = TEST.read_text(encoding="utf-8")
anchor = '''assert literal(cfg, "SH3673510_D011_NTC_NOMINAL_OHM") == 10000
require(cfg, "SH3673520_SPI_GROUP_B6_B7_D2_D7")
'''
insert = '''assert literal(cfg, "SH3673510_D011_NTC_NOMINAL_OHM") == 10000
require(cfg, "SH3673520_SPI_GROUP_B6_B7_D2_D7")
for field in (
    "SH3673510_D011_PD_EN",
    "SH3673510_D011_PUMP_EN",
    "SH3673510_D011_CGR_WK",
    "SH3673510_D011_PDSGT_CODE",
    "SH3673510_D011_MOS_EN",
    "SH3673510_D011_OCC_EN",
    "SH3673510_D011_CADC_EN",
    "SH3673510_D011_WDT_EN",
    "SH3673510_D011_TS4_HW_PROTECT_EN",
    "SH3673510_D011_TS3_HW_PROTECT_EN",
    "SH3673510_D011_TS2_HW_PROTECT_EN",
    "SH3673510_D011_TS1_HW_PROTECT_EN",
    "SH3673510_D011_SC_HW_PROTECT_EN",
    "SH3673510_D011_OCD_HW_PROTECT_EN",
    "SH3673510_D011_UV_HW_PROTECT_EN",
    "SH3673510_D011_OV_HW_PROTECT_EN",
    "SH3673510_D011_RLD",
    "SH3673510_D011_CDV_CODE",
    "SH3673510_D011_OWV_CODE",
    "SH3673510_D011_LOADON_INT",
    "SH3673510_D011_LOADOFF_INT",
    "SH3673510_D011_VADC_INT",
    "SH3673510_D011_CADC_INT",
    "SH3673510_D011_WK_INT",
    "SH3673510_D011_WDT_INT",
    "SH3673510_D011_OWD_INT",
    "SH3673510_D011_TEMP_INT",
    "SH3673510_D011_OCC_INT",
    "SH3673510_D011_OCD_INT",
    "SH3673510_D011_UV_INT",
    "SH3673510_D011_OV_INT",
):
    require(cfg, field)
'''
test = replace_once(test, anchor, insert, "profile test fields")

old_needles = '''    "SH3673510_D011_CELL_COUNT",
    "SH3673520_SetCellCount",
    "SH3673520_SetBalanceMask",
'''
new_needles = '''    "SH3673510_D011_CELL_COUNT",
    "s_static_reg_cfg",
    "SH3673510_D011_SCONF6_VALUE",
    "SH3673520_SetBalanceMask",
'''
test = replace_once(test, old_needles, new_needles, "control test needles")

reg_anchor = '''assert literal(reg, "SH3673520_FLAG1_SC_MASK") == 0x10
'''
reg_insert = '''assert literal(reg, "SH3673520_FLAG1_SC_MASK") == 0x10
assert literal(reg, "SH3673520_RESET_SCONF2") == 0x50
assert literal(reg, "SH3673520_RESET_SCONF7") == 0x04
assert literal(reg, "SH3673520_RESET_OWV_ALARMH") == 0x57
assert literal(reg, "SH3673520_RESET_ALARML") == 0xFF
assert literal(reg, "SH3673520_SCONF7_CONFIG_MASK") == 0x77
assert literal(reg, "SH3673520_ALARML_ALL_MASK") == 0xFF
'''
test = replace_once(test, reg_anchor, reg_insert, "register truth tests")
TEST.write_text(test, encoding="utf-8", newline="\n")

print("Applied deterministic SH36735xx/D011 register profile")
