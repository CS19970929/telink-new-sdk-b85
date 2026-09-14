#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


DEFS = r'''#pragma once

/* Stable product-facing SOC identifiers. Keep numeric values backward compatible. */
#define BMS_SOC_CHEMISTRY_AUTO 0u
#define BMS_SOC_CHEMISTRY_LFP  1u
#define BMS_SOC_CHEMISTRY_NMC  2u

#define BMS_SOC_PROFILE_AUTO        0u
#define BMS_SOC_PROFILE_GENERIC_LFP 1u
#define BMS_SOC_PROFILE_GENERIC_NMC 2u

#define BMS_SOC_PROFILE_GENERIC_LFP_VERSION 1u
#define BMS_SOC_PROFILE_GENERIC_NMC_VERSION 1u
'''

PROFILE = r'''#pragma once

#include "conf.h"
#include "bms_soc_defs.h"

/*
 * OCV/profile data lives outside the SOC algorithm on purpose. Product-specific
 * cell characterization should replace/add profile data here without rewriting
 * coulomb integration, confidence-window, persistence, or display behavior.
 */
typedef struct
{
    uint16_t mv;
    uint8_t soc;
} soc_ocv_point_t;

typedef struct
{
    uint8_t profile_id;
    uint16_t profile_version;
    uint8_t chemistry;
    const soc_ocv_point_t *ocv;
    uint8_t ocv_count;
    uint16_t valid_min_mv;
    uint16_t valid_max_mv;
    uint16_t full_sync_mv;
    uint16_t full_min_margin_mv;
    uint16_t empty_sync_mv;
    uint16_t empty_max_margin_mv;
    uint16_t terminal_start_offset_mv;
    uint16_t terminal_l1_offset_mv;
    uint16_t terminal_l2_offset_mv;
    uint16_t terminal_l3_offset_mv;
} soc_profile_t;

/* Generic center curves. These are defaults, not a claim of cell-model accuracy. */
static const soc_ocv_point_t g_soc_ocv_lfp[] = {
    {2800u, 0u}, {3000u, 2u}, {3100u, 5u}, {3200u, 10u},
    {3250u, 15u}, {3280u, 25u}, {3300u, 35u}, {3315u, 45u},
    {3330u, 55u}, {3340u, 65u}, {3350u, 75u}, {3370u, 85u},
    {3400u, 95u}, {3450u, 98u}, {3500u, 100u},
};

static const soc_ocv_point_t g_soc_ocv_nmc[] = {
    {3000u, 0u}, {3300u, 5u}, {3450u, 10u}, {3550u, 20u},
    {3650u, 30u}, {3700u, 40u}, {3750u, 50u}, {3800u, 60u},
    {3850u, 70u}, {3900u, 80u}, {4000u, 90u}, {4100u, 96u},
    {4180u, 100u},
};

static const soc_profile_t g_soc_profile_lfp = {
    BMS_SOC_PROFILE_GENERIC_LFP,
    BMS_SOC_PROFILE_GENERIC_LFP_VERSION,
    BMS_SOC_CHEMISTRY_LFP,
    g_soc_ocv_lfp,
    (uint8_t)(sizeof(g_soc_ocv_lfp) / sizeof(g_soc_ocv_lfp[0])),
    2500u, 3800u,
    3500u, 100u,
    3000u, 150u,
    150u, 100u, 50u, 20u,
};

static const soc_profile_t g_soc_profile_nmc = {
    BMS_SOC_PROFILE_GENERIC_NMC,
    BMS_SOC_PROFILE_GENERIC_NMC_VERSION,
    BMS_SOC_CHEMISTRY_NMC,
    g_soc_ocv_nmc,
    (uint8_t)(sizeof(g_soc_ocv_nmc) / sizeof(g_soc_ocv_nmc[0])),
    2600u, 4300u,
    4180u, 200u,
    3000u, 200u,
    300u, 200u, 150u, 50u,
};
'''

write(MOD / "bms_soc_defs.h", DEFS)
write(MOD / "bms_soc_profile.h", PROFILE)

# ---- SocEnhance.h ----
hp = MOD / "SocEnhance.h"
h = hp.read_text(encoding="utf-8")
h = replace_once(
    h,
    '#include "conf.h"\n#include "soc_kv_store.h"\n\n#define BMS_SOC_CHEMISTRY_AUTO 0u\n#define BMS_SOC_CHEMISTRY_LFP  1u\n#define BMS_SOC_CHEMISTRY_NMC  2u\n',
    '#include "conf.h"\n#include "soc_kv_store.h"\n#include "bms_soc_defs.h"\n',
    "SocEnhance include/constants",
)
h = replace_once(
    h,
    '    uint8_t chemistry;                  /* AUTO/LFP/NMC */\n    uint16_t current_deadband_ma;',
    '    uint8_t chemistry;                  /* AUTO/LFP/NMC */\n    uint8_t profile_id;                 /* AUTO/generic LFP/generic NMC */\n    uint16_t current_deadband_ma;',
    "SocEnhance config profile_id",
)
h = replace_once(
    h,
    '    uint8_t chemistry;\n    uint8_t soc_estimate;',
    '    uint8_t chemistry;\n    uint8_t profile_id;\n    uint16_t profile_version;\n    uint8_t soc_estimate;',
    "SocEnhance diag profile fields",
)
h = replace_once(
    h,
    'uint8_t bms_soc_configure(const bms_soc_config_t *config);\nuint8_t bms_soc_get_chemistry(void);',
    'uint8_t bms_soc_configure(const bms_soc_config_t *config);\nuint8_t bms_soc_set_product_config(uint8_t chemistry, uint8_t profile_id);\nuint8_t bms_soc_get_chemistry(void);',
    "SocEnhance product setter API",
)
write(hp, h)

# ---- cold KV header ----
chp = MOD / "bms_cold_kv_store.h"
ch = chp.read_text(encoding="utf-8")
ch = replace_once(
    ch,
    '    BMS_SYS_PARAM_FLAGS,\n    BMS_SYS_PARAM_RSVD0,\n} bms_cold_system_param_id_t;',
    '    BMS_SYS_PARAM_FLAGS,\n    BMS_SYS_PARAM_RSVD0,\n    /* Additive keys: existing 0x2001..0x2008 mappings must never move. */\n    BMS_SYS_PARAM_BATTERY_CHEMISTRY,\n    BMS_SYS_PARAM_SOC_PROFILE_ID,\n} bms_cold_system_param_id_t;',
    "cold enum additive ids",
)
ch = replace_once(
    ch,
    '    u32 flags;\n    u32 reserved0;\n} bms_cold_system_params_t;',
    '    u32 flags;\n    u32 reserved0;\n    u32 battery_chemistry;\n    u32 soc_profile_id;\n} bms_cold_system_params_t;',
    "cold system product fields",
)
ch = replace_once(
    ch,
    'int bms_cold_kv_store_set_protect(const struct PRT_E2ROM_PARAS *protect);\nint bms_cold_kv_store_set_system(const bms_cold_system_params_t *system);',
    'int bms_cold_kv_store_set_protect(const struct PRT_E2ROM_PARAS *protect);\nint bms_cold_kv_store_get_system(bms_cold_system_params_t *system);\nint bms_cold_kv_store_set_system(const bms_cold_system_params_t *system);',
    "cold get_system API",
)
write(chp, ch)

# ---- cold KV implementation ----
ccp = MOD / "bms_cold_kv_store.c"
cc = ccp.read_text(encoding="utf-8")
cc = replace_once(
    cc,
    '#include "bms_cold_kv_store.h"\n#include "btname_modbus.h"',
    '#include "bms_cold_kv_store.h"\n#include "bms_soc_defs.h"\n#include "btname_modbus.h"',
    "cold include soc defs",
)
cc = replace_once(
    cc,
    '    X(BMS_COLD_SYSTEM_KEY_BASE + 0x07u, flags) \\\n    X(BMS_COLD_SYSTEM_KEY_BASE + 0x08u, reserved0)',
    '    X(BMS_COLD_SYSTEM_KEY_BASE + 0x07u, flags) \\\n    X(BMS_COLD_SYSTEM_KEY_BASE + 0x08u, reserved0) \\\n    X(BMS_COLD_SYSTEM_KEY_BASE + 0x09u, battery_chemistry) \\\n    X(BMS_COLD_SYSTEM_KEY_BASE + 0x0Au, soc_profile_id)',
    "cold additive system keys",
)
cc = replace_once(
    cc,
    'static u32 bms_cold_get_u32_value(const bms_cold_system_params_t *data, u16 offset)\n{\n    const u32 *field = (const u32 *)((const u8 *)data + offset);\n    return *field;\n}\n',
    'static u32 bms_cold_get_u32_value(const bms_cold_system_params_t *data, u16 offset)\n{\n    const u32 *field = (const u32 *)((const u8 *)data + offset);\n    return *field;\n}\n\nstatic void bms_cold_set_u32_value(bms_cold_system_params_t *data, u16 offset, u32 value)\n{\n    u32 *field = (u32 *)((u8 *)data + offset);\n    *field = value;\n}\n',
    "cold set u32 helper",
)
cc = replace_once(
    cc,
    '    system->flags = 0u;\n    system->reserved0 = 0u;\n}',
    '    system->flags = 0u;\n    system->reserved0 = 0u;\n    system->battery_chemistry = BMS_SOC_CHEMISTRY_AUTO;\n    system->soc_profile_id = BMS_SOC_PROFILE_AUTO;\n}',
    "cold product defaults",
)
insert_get = r'''int bms_cold_kv_store_get_system(bms_cold_system_params_t *system)
{
    u32 value;
    u16 i;

    if (system == NULL) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_ensure_ready()) {
        return FLASH_KV32_FAILED;
    }

    bms_cold_kv_store_get_default_system(system);
    for (i = 0; i < BMS_COLD_SYSTEM_COUNT; ++i) {
        value = 0u;
        if (!flash_kv32_get(&g_bms_cold_kv, g_bms_system_fields[i].key, &value)) {
            return FLASH_KV32_FAILED;
        }
        bms_cold_set_u32_value(system, g_bms_system_fields[i].offset, value);
    }

    return FLASH_KV32_SUCCESS;
}

'''
cc = replace_once(
    cc,
    'int bms_cold_kv_store_set_system(const bms_cold_system_params_t *system)\n{',
    insert_get + 'int bms_cold_kv_store_set_system(const bms_cold_system_params_t *system)\n{',
    "cold get_system implementation",
)
cc = replace_once(
    cc,
    '    if (system == NULL) {\n        return FLASH_KV32_FAILED;\n    }\n\n    if (!bms_cold_ensure_ready()) {\n        return FLASH_KV32_FAILED;\n    }\n\n    for (i = 0; i < BMS_COLD_SYSTEM_COUNT; ++i) {',
    '    if (system == NULL) {\n        return FLASH_KV32_FAILED;\n    }\n\n    if ((system->battery_chemistry > BMS_SOC_CHEMISTRY_NMC) ||\n        (system->soc_profile_id > BMS_SOC_PROFILE_GENERIC_NMC) ||\n        ((system->battery_chemistry == BMS_SOC_CHEMISTRY_LFP) &&\n         (system->soc_profile_id == BMS_SOC_PROFILE_GENERIC_NMC)) ||\n        ((system->battery_chemistry == BMS_SOC_CHEMISTRY_NMC) &&\n         (system->soc_profile_id == BMS_SOC_PROFILE_GENERIC_LFP))) {\n        return FLASH_KV32_FAILED;\n    }\n\n    if (!bms_cold_ensure_ready()) {\n        return FLASH_KV32_FAILED;\n    }\n\n    for (i = 0; i < BMS_COLD_SYSTEM_COUNT; ++i) {',
    "cold validate SOC product values",
)
write(ccp, cc)

# ---- SocEnhance.c ----
cp = MOD / "SocEnhance.c"
c = cp.read_text(encoding="utf-8")
c = replace_once(
    c,
    '#include "SocEnhance.h"\n#include "bms_state.h"',
    '#include "SocEnhance.h"\n#include "bms_soc_profile.h"\n#include "bms_cold_kv_store.h"\n#include "bms_state.h"',
    "SOC include profile/cold store",
)
# Remove profile data from the algorithm source. Keep enum/runtime definitions after it.
pat = re.compile(
    r'typedef struct\n\{\n    uint16_t mv;\n    uint8_t soc;\n\} soc_ocv_point_t;\n\n'
    r'typedef struct\n\{.*?\n\} soc_profile_t;\n\n'
    r'(?:static const soc_ocv_point_t g_soc_ocv_lfp\[\] = \{.*?\n\};\n\n)'
    r'(?:static const soc_ocv_point_t g_soc_ocv_nmc\[\] = \{.*?\n\};\n\n)'
    r'(?:static const soc_profile_t g_soc_profile_lfp = \{.*?\n\};\n\n)'
    r'(?:static const soc_profile_t g_soc_profile_nmc = \{.*?\n\};\n\n)',
    re.S,
)
c, n = pat.subn('', c, count=1)
if n != 1:
    raise RuntimeError(f"SOC profile-data extraction: expected 1 block, got {n}")

c = replace_once(
    c,
    'static bms_soc_config_t g_soc_config = {\n    BMS_SOC_CHEMISTRY_AUTO,\n    SOC_CURRENT_DEADBAND_MA_DEFAULT,',
    'static bms_soc_config_t g_soc_config = {\n    BMS_SOC_CHEMISTRY_AUTO,\n    BMS_SOC_PROFILE_AUTO,\n    SOC_CURRENT_DEADBAND_MA_DEFAULT,',
    "SOC config initializer profile",
)
c = replace_once(
    c,
    '    config->chemistry = BMS_SOC_CHEMISTRY_AUTO;\n    config->current_deadband_ma = SOC_CURRENT_DEADBAND_MA_DEFAULT;',
    '    config->chemistry = BMS_SOC_CHEMISTRY_AUTO;\n    config->profile_id = BMS_SOC_PROFILE_AUTO;\n    config->current_deadband_ma = SOC_CURRENT_DEADBAND_MA_DEFAULT;',
    "SOC default config profile",
)

old_configure = r'''uint8_t bms_soc_configure(const bms_soc_config_t *config)
{
    if (config == 0) return 0u;
    if (config->chemistry > BMS_SOC_CHEMISTRY_NMC) return 0u;
    if ((config->current_deadband_ma > 2000u) ||
        (config->ocv_rest_prepare_s < 60u) ||
        (config->ocv_rest_prepare_s > 3600u) ||
        (config->ocv_error_band_percent == 0u) ||
        (config->ocv_error_band_percent > 20u) ||
        (config->capacity_learning_enable > 1u) ||
        (config->hide_capacity_until_learned > 1u)) return 0u;

    g_soc_config = *config;
    soc_profile_refresh();
    soc_reset_ocv_tracking();
    if (g_soc_initialized) {
        soc_recalc_full_capacity();
        soc_recalc_now_capacity();
    }
    return 1u;
}

static uint8_t soc_resolve_chemistry(void)
{
    uint16_t ovp;
    if (g_soc_config.chemistry == BMS_SOC_CHEMISTRY_LFP ||
        g_soc_config.chemistry == BMS_SOC_CHEMISTRY_NMC) {
        return g_soc_config.chemistry;
    }

    ovp = g_tParam.protect.u16VcellOvp_Third;
    if ((ovp >= 3300u) && (ovp <= SOC_AUTO_LFP_OVP_MAX_MV)) return BMS_SOC_CHEMISTRY_LFP;
    if ((ovp > SOC_AUTO_LFP_OVP_MAX_MV) && (ovp <= 4500u)) return BMS_SOC_CHEMISTRY_NMC;

    /* Invalid/unconfigured protection data: preserve legacy NMC behavior, but
     * OCV validity checks still prevent out-of-range samples from correcting. */
    return BMS_SOC_CHEMISTRY_NMC;
}

static void soc_profile_refresh(void)
{
    uint8_t chemistry = soc_resolve_chemistry();
    const soc_profile_t *next = (chemistry == BMS_SOC_CHEMISTRY_LFP) ?
        &g_soc_profile_lfp : &g_soc_profile_nmc;
    if (g_soc_profile != next) {
        g_soc_profile = next;
        soc_reset_ocv_tracking();
    }
}

void bms_soc_refresh_profile_from_params(void)
{
    soc_profile_refresh();
}
'''
new_configure = r'''static uint8_t soc_product_config_valid(uint8_t chemistry, uint8_t profile_id)
{
    if ((chemistry > BMS_SOC_CHEMISTRY_NMC) ||
        (profile_id > BMS_SOC_PROFILE_GENERIC_NMC)) return 0u;
    if ((chemistry == BMS_SOC_CHEMISTRY_LFP) &&
        (profile_id == BMS_SOC_PROFILE_GENERIC_NMC)) return 0u;
    if ((chemistry == BMS_SOC_CHEMISTRY_NMC) &&
        (profile_id == BMS_SOC_PROFILE_GENERIC_LFP)) return 0u;
    return 1u;
}

static uint8_t soc_config_valid(const bms_soc_config_t *config)
{
    if (config == 0) return 0u;
    if (!soc_product_config_valid(config->chemistry, config->profile_id)) return 0u;
    if ((config->current_deadband_ma > 2000u) ||
        (config->ocv_rest_prepare_s < 60u) ||
        (config->ocv_rest_prepare_s > 3600u) ||
        (config->ocv_error_band_percent == 0u) ||
        (config->ocv_error_band_percent > 20u) ||
        (config->capacity_learning_enable > 1u) ||
        (config->hide_capacity_until_learned > 1u)) return 0u;
    return 1u;
}

uint8_t bms_soc_configure(const bms_soc_config_t *config)
{
    if (!soc_config_valid(config)) return 0u;

    g_soc_config = *config;
    soc_profile_refresh();
    soc_reset_ocv_tracking();
    if (g_soc_initialized) {
        soc_recalc_full_capacity();
        soc_recalc_now_capacity();
    }
    return 1u;
}

static const soc_profile_t *soc_profile_from_id(uint8_t profile_id)
{
    if (profile_id == BMS_SOC_PROFILE_GENERIC_LFP) return &g_soc_profile_lfp;
    if (profile_id == BMS_SOC_PROFILE_GENERIC_NMC) return &g_soc_profile_nmc;
    return 0;
}

static uint8_t soc_resolve_chemistry(void)
{
    const soc_profile_t *selected;
    uint16_t ovp;

    selected = soc_profile_from_id(g_soc_config.profile_id);
    if (selected != 0) return selected->chemistry;

    if (g_soc_config.chemistry == BMS_SOC_CHEMISTRY_LFP ||
        g_soc_config.chemistry == BMS_SOC_CHEMISTRY_NMC) {
        return g_soc_config.chemistry;
    }

    /* AUTO is a backward-compatible fallback for units whose old cold KV does
     * not yet contain chemistry/profile keys. New products should persist the
     * explicit chemistry/profile selection instead of relying on this heuristic. */
    ovp = g_tParam.protect.u16VcellOvp_Third;
    if ((ovp >= 3300u) && (ovp <= SOC_AUTO_LFP_OVP_MAX_MV)) return BMS_SOC_CHEMISTRY_LFP;
    if ((ovp > SOC_AUTO_LFP_OVP_MAX_MV) && (ovp <= 4500u)) return BMS_SOC_CHEMISTRY_NMC;

    return BMS_SOC_CHEMISTRY_NMC;
}

static void soc_profile_refresh(void)
{
    const soc_profile_t *next = soc_profile_from_id(g_soc_config.profile_id);
    if (next == 0) {
        uint8_t chemistry = soc_resolve_chemistry();
        next = (chemistry == BMS_SOC_CHEMISTRY_LFP) ?
            &g_soc_profile_lfp : &g_soc_profile_nmc;
    }
    if (g_soc_profile != next) {
        g_soc_profile = next;
        soc_reset_ocv_tracking();
    }
}

static void soc_load_persisted_product_config(void)
{
    bms_cold_system_params_t system;
    uint8_t chemistry = BMS_SOC_CHEMISTRY_AUTO;
    uint8_t profile_id = BMS_SOC_PROFILE_AUTO;

    if (bms_cold_kv_store_get_system(&system)) {
        if (system.battery_chemistry <= BMS_SOC_CHEMISTRY_NMC)
            chemistry = (uint8_t)system.battery_chemistry;
        if (system.soc_profile_id <= BMS_SOC_PROFILE_GENERIC_NMC)
            profile_id = (uint8_t)system.soc_profile_id;
    }

    if (!soc_product_config_valid(chemistry, profile_id)) {
        chemistry = BMS_SOC_CHEMISTRY_AUTO;
        profile_id = BMS_SOC_PROFILE_AUTO;
    }
    g_soc_config.chemistry = chemistry;
    g_soc_config.profile_id = profile_id;
}

uint8_t bms_soc_set_product_config(uint8_t chemistry, uint8_t profile_id)
{
    bms_cold_system_params_t system;
    bms_soc_config_t next = g_soc_config;

    next.chemistry = chemistry;
    next.profile_id = profile_id;
    if (!soc_config_valid(&next)) return 0u;
    if (!bms_cold_kv_store_get_system(&system)) return 0u;

    system.battery_chemistry = chemistry;
    system.soc_profile_id = profile_id;
    if (!bms_cold_kv_store_set_system(&system)) return 0u;

    return bms_soc_configure(&next);
}

void bms_soc_refresh_profile_from_params(void)
{
    soc_profile_refresh();
}
'''
c = replace_once(c, old_configure, new_configure, "SOC configure/profile selection block")
c = replace_once(
    c,
    '    diag->chemistry = g_soc_profile->chemistry;\n    diag->soc_estimate = SOC_Calculate_Element.u8SOC_Now;',
    '    diag->chemistry = g_soc_profile->chemistry;\n    diag->profile_id = g_soc_profile->profile_id;\n    diag->profile_version = g_soc_profile->profile_version;\n    diag->soc_estimate = SOC_Calculate_Element.u8SOC_Now;',
    "SOC diag profile identity",
)
c = replace_once(
    c,
    '    memset(&g_soc_runtime, 0, sizeof(g_soc_runtime));\n    soc_profile_refresh();',
    '    memset(&g_soc_runtime, 0, sizeof(g_soc_runtime));\n    soc_load_persisted_product_config();\n    soc_profile_refresh();',
    "SOC startup persisted product config",
)
write(cp, c)

# ---- permanent contract test ----
TEST = r'''#!/usr/bin/env python3
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
C = (MOD / "SocEnhance.c").read_text(encoding="utf-8", errors="ignore")
H = (MOD / "SocEnhance.h").read_text(encoding="utf-8", errors="ignore")
PROFILE = (MOD / "bms_soc_profile.h").read_text(encoding="utf-8", errors="ignore")
DEFS = (MOD / "bms_soc_defs.h").read_text(encoding="utf-8", errors="ignore")
COLD_C = (MOD / "bms_cold_kv_store.c").read_text(encoding="utf-8", errors="ignore")
COLD_H = (MOD / "bms_cold_kv_store.h").read_text(encoding="utf-8", errors="ignore")
KV_C = (MOD / "soc_kv_store.c").read_text(encoding="utf-8", errors="ignore")
KV_H = (MOD / "soc_kv_store.h").read_text(encoding="utf-8", errors="ignore")


class SocContract(unittest.TestCase):
    def test_dual_chemistry_profiles_are_data_not_algorithm(self):
        self.assertIn("BMS_SOC_CHEMISTRY_LFP", DEFS)
        self.assertIn("BMS_SOC_CHEMISTRY_NMC", DEFS)
        self.assertIn("g_soc_ocv_lfp", PROFILE)
        self.assertIn("g_soc_ocv_nmc", PROFILE)
        self.assertIn("BMS_SOC_PROFILE_GENERIC_LFP_VERSION", PROFILE)
        self.assertIn("BMS_SOC_PROFILE_GENERIC_NMC_VERSION", PROFILE)
        self.assertNotIn("static const soc_ocv_point_t g_soc_ocv_lfp", C)
        self.assertNotIn("static const soc_ocv_point_t g_soc_ocv_nmc", C)
        self.assertIn("SOC_AUTO_LFP_OVP_MAX_MV              3900u", C)

    def test_product_chemistry_and_profile_are_persistent_additive_keys(self):
        self.assertIn("BMS_SYS_PARAM_BATTERY_CHEMISTRY", COLD_H)
        self.assertIn("BMS_SYS_PARAM_SOC_PROFILE_ID", COLD_H)
        self.assertIn("BMS_COLD_SYSTEM_KEY_BASE + 0x09u, battery_chemistry", COLD_C)
        self.assertIn("BMS_COLD_SYSTEM_KEY_BASE + 0x0Au, soc_profile_id", COLD_C)
        self.assertIn("BMS_COLD_SYSTEM_KEY_BASE + 0x08u, reserved0", COLD_C)
        self.assertIn("system->battery_chemistry = BMS_SOC_CHEMISTRY_AUTO", COLD_C)
        self.assertIn("system->soc_profile_id = BMS_SOC_PROFILE_AUTO", COLD_C)
        self.assertIn("bms_cold_kv_store_get_system", COLD_C)
        self.assertIn("bms_soc_set_product_config", C)
        self.assertIn("soc_load_persisted_product_config", C)

    def test_explicit_profile_wins_and_mismatches_are_rejected(self):
        self.assertIn("soc_profile_from_id(g_soc_config.profile_id)", C)
        self.assertIn("profile_id == BMS_SOC_PROFILE_GENERIC_NMC", C)
        self.assertIn("profile_id == BMS_SOC_PROFILE_GENERIC_LFP", C)
        self.assertIn("soc_product_config_valid", C)
        self.assertIn("New products should persist the", C)
        self.assertIn("explicit chemistry/profile selection", C)

    def test_coulomb_integration_and_deadband(self):
        self.assertIn("SOC_INTEGRAL_PERIOD_MS              200u", C)
        self.assertIn("SOC_CURRENT_DEADBAND_MA_DEFAULT     200u", C)
        self.assertIn("soc_current_direction", C)
        self.assertIn("g_soc_integral_ms_remainder", C)

    def test_ocv_requires_ten_minutes_and_uses_band(self):
        self.assertIn("SOC_OCV_REST_PREPARE_SECONDS        600u", C)
        self.assertIn("SOC_OCV_ERROR_BAND_PERCENT          5u", C)
        self.assertIn("g_soc_runtime.ocv_low", C)
        self.assertIn("g_soc_runtime.ocv_high", C)
        self.assertIn("return soc_step_down_to(g_soc_runtime.ocv_high);", C)
        ocv_fn = C[C.index("static uint8_t soc_idle_ocv_tracking"):C.index("static uint16_t soc_discharge_natural_1pct_ticks")]
        self.assertNotIn("soc_step_up_to", ocv_fn)

    def test_display_soc_is_separate(self):
        self.assertIn("static uint8_t g_soc_display_soc", C)
        self.assertIn("SOC_DISPLAY_STEP_TICKS              SOC_TICKS_PER_SECOND", C)
        self.assertIn("g_stCellInfoReport.SocElement.u16Soc = get_soc_display();", C)

    def test_endpoints_and_lfp_terminal_knee_are_chemistry_specific(self):
        self.assertIn("g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp", C)
        self.assertIn("g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp", C)
        self.assertIn("150u, 100u, 50u, 20u", PROFILE)
        self.assertIn("300u, 200u, 150u, 50u", PROFILE)

    def test_upward_calibration_requires_confirmed_charging_full_anchor(self):
        start = C.index("static uint8_t soc_apply_full_anchor(void)")
        end = C.index("static uint8_t soc_apply_forced_empty_anchor(void)", start)
        full_fn = C[start:end]
        self.assertIn("(VCELLMAX >= full_mv) && (VCELLMIN >= full_min) && isCHG()", full_fn)
        self.assertIn("if (isCHG() && g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp)", full_fn)
        self.assertNotIn("&& !isDSG()", full_fn)
        self.assertNotIn("soc_step_up_to", C[C.index("static uint8_t soc_idle_ocv_tracking"):C.index("static uint16_t soc_discharge_natural_1pct_ticks")])

    def test_soc_low_faults_are_implemented_without_mos_policy(self):
        self.assertIn("soc_update_low_faults", C)
        self.assertIn("fault->bits.b1SocLow", C)
        self.assertIn("u16SocUp_First", C)
        self.assertNotIn("b1SocLow ||", C)

    def test_learning_is_present_but_default_disabled(self):
        self.assertIn("BMS_SOC_CAPACITY_LEARNING_ENABLE_DEFAULT 0u", C)
        self.assertIn("BMS_SOC_LEARNING_EMPTY_TO_FULL", H)
        self.assertIn("BMS_SOC_LEARNING_FULL_TO_EMPTY", H)
        self.assertIn("soc_learning_on_full_anchor", C)
        self.assertIn("soc_learning_on_empty_anchor", C)
        self.assertIn("SOC_KV_FLAG_CAPACITY_LEARNED", KV_H)
        self.assertIn("SOC_KV_KEY_LEARNED_CAPACITY", KV_C)
        self.assertIn("soc_kv_store_write_learning", KV_C)

    def test_diag_reports_profile_identity_and_version(self):
        self.assertIn("bms_soc_diag_t", H)
        self.assertIn("profile_id", H)
        self.assertIn("profile_version", H)
        self.assertIn("diag->profile_id = g_soc_profile->profile_id", C)
        self.assertIn("diag->profile_version = g_soc_profile->profile_version", C)


if __name__ == "__main__":
    unittest.main(verbosity=2)
'''
write(ROOT / "tests" / "soc_contract_check.py", TEST)

DOC = r'''# SOC 模块（当前实现）

本文件记录 `SocEnhance.c/.h`、`bms_soc_profile.h`、`bms_soc_defs.h` 与 SOC/Cold KV 的真实行为。旧文件名保留用于工程兼容，但算法、产品配置、OCV 数据和持久化已经分层。

## 1. 核心模型

- `SOC estimate`：库仑积分主线，固定 200 ms 积分周期。
- 电流报告单位为 0.1 A；默认 **< 200 mA 不积分**，视为静置候选。
- `SOC display`：与 estimate 分离，每 1 s 最多变化 1%，避免对外跳变。
- SOC hot KV 保存 estimate / 等效放电百分比 / cycle / learned capacity；显示 SOC 不保存。
- OCV 只用于长期纠偏，不作为运行中的主 SOC。
- 普通 OCV、开机、静置、电压回弹 **永远不得向上校准**；只有“确认正在充电 + 满电条件成立”可以主动向上校准到 100%。

## 2. 三元 / 铁锂产品参数

稳定 ID 定义位于 `bms_soc_defs.h`：

```text
chemistry:
0 = AUTO
1 = LFP
2 = NMC

profile_id:
0 = AUTO
1 = GENERIC_LFP
2 = GENERIC_NMC
```

产品选择现在是正式 Cold KV 参数，而不是只能根据 OVP 猜：

- `0x2009`：`battery_chemistry`
- `0x200A`：`soc_profile_id`

历史 `0x2001..0x2008` key 保持不变；这是**只追加 key**的兼容升级。旧设备 Flash 中没有 `0x2009/0x200A` 时，`flash_kv32` 使用新 key 的默认值 `AUTO/AUTO`，然后才回退到历史 OVP 推断。因此不依赖旧 `PARAM_VER`，也不会覆盖其它已出货参数。

新产品推荐显式持久化：

```c
bms_soc_set_product_config(BMS_SOC_CHEMISTRY_LFP,
                           BMS_SOC_PROFILE_GENERIC_LFP);
```

或 NMC。API 先校验 chemistry/profile 一致性，再原子保存 system KV，最后切换运行 profile。`LFP + NMC profile`、`NMC + LFP profile` 这类组合直接拒绝。

`AUTO` 只用于兼容/通用固件：两项均 AUTO 时继续根据已加载的三级单体 OVP 回退判断（`<=3900 mV` LFP，`>3900 mV` NMC）。量产产品不建议长期依赖该启发式。

## 3. OCV Profile 数据层

OCV 曲线和端点数据已经从 `SocEnhance.c` 算法中移到 `bms_soc_profile.h`。算法只消费 `soc_profile_t`：

- profile ID / version；
- chemistry；
- OCV 点表；
- 有效电压范围；
- full/empty anchor 参数；
- 放电末端 knee 参数。

当前内置：

- `GENERIC_LFP`, version 1；
- `GENERIC_NMC`, version 1。

它们是**通用中心表**，不是某一型号电芯的实验标定曲线。以后拿到 32700 LFP、21700 NMC 等实际静置数据时，应新增/替换 profile 数据并提升 profile version，不改库仑积分、置信区间、Flash、显示 SOC 或保护策略。

## 4. OCV 置信区间

OCV 使用保守加权单体电压：

```text
V_ocv = (3 * Vcell_min + Vcell_max) / 4
```

静置校准条件：

- 充/放电有效电流均低于 200 mA；
- 单体压差 <= 100 mV；
- 相邻 200 ms 样本变化 <= 8 mV；
- 连续稳定 **>= 10 min**。

达到条件后，由当前 profile 得到 `center SOC`，形成默认 `center ± 5 percentage points` 的 `[low, high]`：

- estimate 在区间内：不修正；
- estimate 低于 `low`：不动，绝不向上；
- estimate 高于 `high`：每 30 min 最多下降 1%，长期回归到 `high` 边界。

开机恢复 Flash SOC 后重新进入静置资格判定，不会用一次开机电压覆盖 SOC。

## 5. 满 / 空锚点与低端体验

满电：

- **仅在确认充电方向时**，三级单体 OVP 已触发才允许强锚定 100%；或
- **仅在确认充电方向时**，满足当前 profile 的满电电压条件稳定 60 s，之后约 2 s/1% 向 100% 收敛。
- 静置、高电压回弹、开机高电压、非充电状态即使达到满电区也不得向上校准。

空电：三级单体 UVP 可强锚定 0%；放电低端分段向 0 收敛。LFP/NMC 使用不同 knee，高电流压降有 sag hold，避免电摩起步时按瞬时端电压误杀 SOC。

## 6. SOC Low 告警

`g_tParam.protect.u16SocUp_First/Second/Third` 沿用历史字段名，实际按低 SOC 阈值处理，并写 First/Second/Third `b1SocLow`。当前只告警，不默认关闭 DSG；真正的安全欠压截止仍由 MCU Cell UV + AFE UV 完成。

## 7. 容量学习

框架已实现但默认关闭：支持 0->100 和 100->0 完整路径，中途反向电流或 MCU 重启作废；成功值限定在 nominal 50%~130%。开启 `hide_capacity_until_learned` 后首次有效学习前容量字段隐藏，SOC 百分比仍可用。

## 8. 诊断接口

`bms_soc_get_diag()` 当前返回：

- 实际 chemistry；
- 实际 profile ID + profile version；
- estimate / display SOC；
- OCV state / center / low / high / confidence；
- OCV cell voltage、静置秒数；
- 容量学习状态、learned capacity。

因此现场可以明确区分“LFP/NMC 选错”“曲线版本不同”“OCV 未达到静置条件”和“库仑累计偏差”。协议寄存器地址应由产品协议统一分配，不在 SOC 模块里硬编码。

## 9. 必测场景

1. 老固件 Cold KV 升级：原 `0x2001..0x2008` 不变，新 key 缺失时 AUTO/AUTO 正常 fallback。
2. 显式 LFP/NMC 保存、掉电重启后仍使用相同 chemistry/profile/version。
3. chemistry/profile 冲突配置必须拒绝且不能污染 Flash。
4. 0.1 A 不积分、0.2 A 开始积分边界。
5. 静置 9 min 59 s 不校准；10 min 后普通 OCV 只能向下。
6. 非充电的开机高电压/回弹绝不能使 SOC 上升；确认充满可到 100%。
7. 充电满锚点、放电 UVP、LFP 3.30 V 平台、大电流 sag hold。
8. SOC Low 三级告警、Flash 恢复、容量学习成功/中断/复位作废。
'''
write(ROOT / "docs" / "SOC.md", DOC)

sp = ROOT / "docs" / "STORAGE.md"
s = sp.read_text(encoding="utf-8")
s = replace_once(
    s,
    '- `bms_cold_kv_store.*`：保护参数、系统参数、升级/reset epoch 和 BT name suffix。',
    '- `bms_cold_kv_store.*`：保护参数、系统参数、SOC 产品 chemistry/profile、升级/reset epoch 和 BT name suffix。',
    "storage cold KV responsibility",
)
s = replace_once(
    s,
    '## 3. 一致性规则\n',
    '### SOC 产品参数的兼容 key\n\nCold KV 的 system key `0x2001..0x2008` 保持原映射；SOC 只在尾部新增 `0x2009=battery_chemistry`、`0x200A=soc_profile_id`。旧 Flash 无新 key 时由 `flash_kv32` key default 得到 `AUTO/AUTO`，不得因为新增 SOC 产品字段重置其它 system/protect 参数。\n\n## 3. 一致性规则\n',
    "storage SOC additive keys",
)
write(sp, s)

print("SOC product configuration/profile refactor applied")
