#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"

PROFILE_H = r'''#ifndef BMS_AFE_HW_PROFILE_H_
#define BMS_AFE_HW_PROFILE_H_

#include "tl_common.h"
#include "param.h"

#define BMS_AFE_HW_PROFILE_SCHEMA_VERSION 1u
#define BMS_AFE_HW_MODEL_DVC1124          0x1124u
#define BMS_AFE_HW_MODEL_SH3673510        0x3510u

#define BMS_AFE_HW_EN_COV   (1u << 0)
#define BMS_AFE_HW_EN_CUV   (1u << 1)
#define BMS_AFE_HW_EN_OCD1  (1u << 2)
#define BMS_AFE_HW_EN_OCD2  (1u << 3)
#define BMS_AFE_HW_EN_OCC1  (1u << 4)
#define BMS_AFE_HW_EN_OCC2  (1u << 5)
#define BMS_AFE_HW_EN_SC    (1u << 6)
#define BMS_AFE_HW_EN_TEMP  (1u << 7)

typedef struct
{
    u16 schema_version;
    u16 afe_model;
    u16 cov_mv;
    u16 cov_delay_ms;
    u16 cov_recover_mv;
    u16 cov_recover_ms;
    u16 cuv_mv;
    u16 cuv_delay_ms;
    u16 cuv_recover_mv;
    u16 cuv_recover_ms;
    u16 ocd1_a10;
    u16 ocd1_delay_ms;
    u16 ocd2_a10;
    u16 ocd2_delay_ms;
    u16 ocd_recover_a10;
    u16 ocd_recover_ms;
    u16 occ1_a10;
    u16 occ1_delay_ms;
    u16 occ2_a10;
    u16 occ2_delay_ms;
    u16 occ_recover_a10;
    u16 occ_recover_ms;
    u16 sc_a10;
    u16 sc_delay_us;
    u16 sc_recover_ms;
    u16 chg_ot_x10;
    u16 chg_ot_recover_x10;
    u16 chg_ut_x10;
    u16 chg_ut_recover_x10;
    u16 dsg_ot_x10;
    u16 dsg_ot_recover_x10;
    u16 dsg_ut_x10;
    u16 dsg_ut_recover_x10;
    u16 temp_recover_ms;
    u16 enable_mask;
} bms_afe_hw_profile_t;

#define BMS_AFE_HW_PROFILE_WORD_COUNT ((u16)(sizeof(bms_afe_hw_profile_t) / sizeof(u16)))

typedef char bms_afe_hw_profile_word_layout_must_be_35[
    (sizeof(bms_afe_hw_profile_t) == (35u * sizeof(u16))) ? 1 : -1];

u16 bms_afe_hw_profile_expected_model(void);
u16 bms_afe_hw_profile_capabilities(void);
u8 bms_afe_hw_profile_validate(const bms_afe_hw_profile_t *profile);
u8 bms_afe_hw_profile_init(void);
u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *profile);
u8 bms_afe_hw_profile_set(const bms_afe_hw_profile_t *profile);
void bms_afe_hw_profile_build_migration_default(bms_afe_hw_profile_t *profile);

#endif /* BMS_AFE_HW_PROFILE_H_ */
'''

PROFILE_C = r'''#include "bms_afe_hw_profile.h"
#include "bms_afe_backend.h"
#include "bms_cold_kv_store.h"
#include <string.h>

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "dvc1124_project_config.h"
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
#include "sh3673510_project_config.h"
#endif

static u16 ms10_to_ms(u16 filter_10ms)
{
    u32 ms = (u32)filter_10ms * 10u;
    return (u16)((ms > 65535u) ? 65535u : ms);
}

u16 bms_afe_hw_profile_expected_model(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    return BMS_AFE_HW_MODEL_DVC1124;
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    return BMS_AFE_HW_MODEL_SH3673510;
#else
    return 0u;
#endif
}

u16 bms_afe_hw_profile_capabilities(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    return (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                 BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                 BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_OCC2 |
                 BMS_AFE_HW_EN_SC);
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    return (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                 BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                 BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_SC |
                 BMS_AFE_HW_EN_TEMP);
#else
    return 0u;
#endif
}

static u16 sh_sc_multiplier(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    static const u16 values[4] = {2u, 3u, 4u, 6u};
    u8 code = SH3673510_D011_SC_MULTIPLIER_CODE;
    return (code < 4u) ? values[code] : 2u;
#else
    return 0u;
#endif
}

static u16 sh_sc_delay_us(void)
{
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    static const u16 values[8] = {2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u};
    u8 code = SH3673510_D011_SC_DELAY_CODE;
    return (code < 8u) ? values[code] : 256u;
#else
    return 0u;
#endif
}

void bms_afe_hw_profile_build_migration_default(bms_afe_hw_profile_t *p)
{
    const struct PRT_E2ROM_PARAS *s = &g_tParam.protect;
    if (p == 0) return;
    memset(p, 0, sizeof(*p));
    p->schema_version = BMS_AFE_HW_PROFILE_SCHEMA_VERSION;
    p->afe_model = bms_afe_hw_profile_expected_model();
    p->cov_mv = s->u16VcellOvp_Third;
    p->cov_delay_ms = ms10_to_ms(s->u16VcellOvp_Filter);
    p->cov_recover_mv = s->u16VcellOvp_Rcv;
    p->cov_recover_ms = ms10_to_ms(s->u16VcellOvp_Filter);
    p->cuv_mv = s->u16VcellUvp_Third;
    p->cuv_delay_ms = ms10_to_ms(s->u16VcellUvp_Filter);
    p->cuv_recover_mv = s->u16VcellUvp_Rcv;
    p->cuv_recover_ms = ms10_to_ms(s->u16VcellUvp_Filter);
    p->ocd1_a10 = s->u16IdsgOcp_First;
    p->ocd1_delay_ms = ms10_to_ms(s->u16IdsgOcp_Filter);
    p->ocd2_a10 = s->u16IdsgOcp_Second;
    p->ocd2_delay_ms = ms10_to_ms(s->u16IdsgOcp_Filter);
    p->ocd_recover_a10 = s->u16IdsgOcp_Rcv;
    p->occ1_a10 = s->u16IchgOcp_First;
    p->occ1_delay_ms = ms10_to_ms(s->u16IchgOcp_Filter);
    p->occ2_a10 = s->u16IchgOcp_Second;
    p->occ2_delay_ms = ms10_to_ms(s->u16IchgOcp_Filter);
    p->occ_recover_a10 = s->u16IchgOcp_Rcv;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    p->ocd_recover_ms = 0u; /* preserve legacy one-sample DVC latch release */
    p->occ_recover_ms = 0u;
    if (DVC1124_HW_SCD_THRESHOLD_MV != 0u && DVC1124_DEFAULT_SHUNT_UOHM != 0u)
        p->sc_a10 = (u16)(((u32)DVC1124_HW_SCD_THRESHOLD_MV * 10000u) /
                          DVC1124_DEFAULT_SHUNT_UOHM);
    p->sc_delay_us = DVC1124_HW_SCD_DELAY_US;
    p->sc_recover_ms = 0u; /* SCD remains backend-latched until load release is validated */
    p->enable_mask = (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                           BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                           BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_OCC2);
    if (p->sc_a10 != 0u) p->enable_mask |= BMS_AFE_HW_EN_SC;
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    p->ocd_recover_ms = 2000u;
    p->occ_recover_ms = ms10_to_ms(s->u16IchgOcp_Filter);
    p->sc_a10 = (u16)((u32)p->ocd2_a10 * sh_sc_multiplier());
    p->sc_delay_us = sh_sc_delay_us();
    p->sc_recover_ms = 2000u;
    p->chg_ot_x10 = s->u16TChgOTp_Third;
    p->chg_ot_recover_x10 = s->u16TChgOTp_Rcv;
    p->chg_ut_x10 = s->u16TchgUTp_Third;
    p->chg_ut_recover_x10 = s->u16TchgUTp_Rcv;
    p->dsg_ot_x10 = s->u16TdischgOTp_Third;
    p->dsg_ot_recover_x10 = s->u16TdischgOTp_Rcv;
    p->dsg_ut_x10 = s->u16TdischgUTp_Third;
    p->dsg_ut_recover_x10 = s->u16TdischgUTp_Rcv;
    p->temp_recover_ms = ms10_to_ms(s->u16TChgOTp_Filter);
    p->enable_mask = (u16)(BMS_AFE_HW_EN_COV | BMS_AFE_HW_EN_CUV |
                           BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2 |
                           BMS_AFE_HW_EN_OCC1 | BMS_AFE_HW_EN_SC |
                           BMS_AFE_HW_EN_TEMP);
#endif
}

static u8 validate_hysteresis(const bms_afe_hw_profile_t *p)
{
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) &&
        (p->cov_mv == 0u || p->cov_recover_mv >= p->cov_mv)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) &&
        (p->cuv_mv == 0u || p->cuv_recover_mv <= p->cuv_mv)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) &&
        (p->ocd1_a10 == 0u || p->ocd_recover_a10 >= p->ocd1_a10)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) &&
        (p->ocd2_a10 == 0u || p->ocd_recover_a10 >= p->ocd2_a10)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) &&
        (p->occ1_a10 == 0u || p->occ_recover_a10 >= p->occ1_a10)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC2) &&
        (p->occ2_a10 == 0u || p->occ_recover_a10 >= p->occ2_a10)) return 0u;
    if (p->enable_mask & BMS_AFE_HW_EN_TEMP) {
        if (p->chg_ot_recover_x10 >= p->chg_ot_x10 ||
            p->dsg_ot_recover_x10 >= p->dsg_ot_x10 ||
            p->chg_ut_recover_x10 <= p->chg_ut_x10 ||
            p->dsg_ut_recover_x10 <= p->dsg_ut_x10) return 0u;
    }
    return 1u;
}

u8 bms_afe_hw_profile_validate(const bms_afe_hw_profile_t *p)
{
    u32 sense_uv;
    if (p == 0) return 0u;
    if (p->schema_version != BMS_AFE_HW_PROFILE_SCHEMA_VERSION) return 0u;
    if (p->afe_model != bms_afe_hw_profile_expected_model()) return 0u;
    if ((p->enable_mask & (u16)~bms_afe_hw_profile_capabilities()) != 0u) return 0u;
    if (!validate_hysteresis(p)) return 0u;
    if (p->chg_ot_x10 > 1450u || p->chg_ot_recover_x10 > 1450u ||
        p->chg_ut_x10 > 1450u || p->chg_ut_recover_x10 > 1450u ||
        p->dsg_ot_x10 > 1450u || p->dsg_ot_recover_x10 > 1450u ||
        p->dsg_ut_x10 > 1450u || p->dsg_ut_recover_x10 > 1450u) return 0u;

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) && (p->cov_mv < 501u || p->cov_mv > 4595u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) && p->cuv_mv > 4095u) return 0u;
    if (p->cov_delay_ms > 8000u || p->cuv_delay_ms > 8000u ||
        p->ocd1_delay_ms > 2048u || p->occ1_delay_ms > 2048u ||
        p->ocd2_delay_ms > 1024u || p->occ2_delay_ms > 1024u) return 0u;
    sense_uv = ((u32)p->ocd1_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) && sense_uv > 63750u) return 0u;
    sense_uv = ((u32)p->occ1_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) && sense_uv > 63750u) return 0u;
    sense_uv = ((u32)p->ocd2_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) && sense_uv > 256000u) return 0u;
    sense_uv = ((u32)p->occ2_a10 * DVC1124_DEFAULT_SHUNT_UOHM) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC2) && sense_uv > 256000u) return 0u;
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    if ((p->enable_mask & BMS_AFE_HW_EN_COV) && (p->cov_mv == 0u || p->cov_mv > 5115u)) return 0u;
    if ((p->enable_mask & BMS_AFE_HW_EN_CUV) && (p->cuv_mv == 0u || p->cuv_mv > 5115u)) return 0u;
    if (p->cov_delay_ms > 10010u || p->cuv_delay_ms > 10010u ||
        p->ocd1_delay_ms > 10010u || p->occ1_delay_ms > 10010u ||
        p->ocd2_delay_ms > 400u || p->sc_delay_us > 256u) return 0u;
    sense_uv = ((u32)p->ocd1_a10 * SH3673510_D011_SHUNT_UOHM + 5u) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD1) && sense_uv > 80000u) return 0u;
    sense_uv = ((u32)p->ocd2_a10 * SH3673510_D011_SHUNT_UOHM + 5u) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCD2) && sense_uv > 160000u) return 0u;
    sense_uv = ((u32)p->occ1_a10 * SH3673510_D011_SHUNT_UOHM + 5u) / 10u;
    if ((p->enable_mask & BMS_AFE_HW_EN_OCC1) && sense_uv > 44000u) return 0u;
#endif
    return 1u;
}

u8 bms_afe_hw_profile_init(void)
{
    bms_afe_hw_profile_t p;
    if (!bms_cold_kv_store_get_afe_hw_profile(&p)) return 0u;
    if (p.schema_version == 0u && p.afe_model == 0u) {
        bms_afe_hw_profile_build_migration_default(&p);
        if (!bms_afe_hw_profile_validate(&p)) return 0u;
        return bms_cold_kv_store_set_afe_hw_profile(&p) ? 1u : 0u;
    }
    return bms_afe_hw_profile_validate(&p);
}

u8 bms_afe_hw_profile_get(bms_afe_hw_profile_t *p)
{
    if (p == 0) return 0u;
    if (!bms_afe_hw_profile_init()) return 0u;
    if (!bms_cold_kv_store_get_afe_hw_profile(p)) return 0u;
    return bms_afe_hw_profile_validate(p);
}

u8 bms_afe_hw_profile_set(const bms_afe_hw_profile_t *p)
{
    if (!bms_afe_hw_profile_validate(p)) return 0u;
    return bms_cold_kv_store_set_afe_hw_profile(p) ? 1u : 0u;
}
'''

(HERE / "bms_afe_hw_profile.h").write_text(PROFILE_H, encoding="utf-8")
(HERE / "bms_afe_hw_profile.c").write_text(PROFILE_C, encoding="utf-8")

# Extend cold KV with a dedicated 0x5000 hardware-profile namespace.
h = HERE / "bms_cold_kv_store.h"
s = h.read_text(encoding="utf-8")
if '#include "bms_afe_hw_profile.h"' not in s:
    s = s.replace('#include "flash_kv32.h"\n', '#include "flash_kv32.h"\n#include "bms_afe_hw_profile.h"\n', 1)
if 'bms_cold_kv_store_get_afe_hw_profile' not in s:
    s = s.replace('int bms_cold_kv_store_set_system(const bms_cold_system_params_t *system);\n',
                  'int bms_cold_kv_store_set_system(const bms_cold_system_params_t *system);\n'
                  'int bms_cold_kv_store_get_afe_hw_profile(bms_afe_hw_profile_t *profile);\n'
                  'int bms_cold_kv_store_set_afe_hw_profile(const bms_afe_hw_profile_t *profile);\n', 1)
h.write_text(s, encoding="utf-8")

c = HERE / "bms_cold_kv_store.c"
s = c.read_text(encoding="utf-8")
s = s.replace('#define BMS_COLD_BTNAME_KEY_BASE   0x4000u\n',
              '#define BMS_COLD_BTNAME_KEY_BASE   0x4000u\n#define BMS_COLD_AFE_HW_KEY_BASE   0x5000u\n', 1)
AFE_LIST = r'''
#define BMS_COLD_AFE_HW_FIELD_LIST(X) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x01u, schema_version) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x02u, afe_model) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x03u, cov_mv) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x04u, cov_delay_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x05u, cov_recover_mv) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x06u, cov_recover_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x07u, cuv_mv) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x08u, cuv_delay_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x09u, cuv_recover_mv) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x0Au, cuv_recover_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x0Bu, ocd1_a10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x0Cu, ocd1_delay_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x0Du, ocd2_a10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x0Eu, ocd2_delay_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x0Fu, ocd_recover_a10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x10u, ocd_recover_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x11u, occ1_a10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x12u, occ1_delay_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x13u, occ2_a10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x14u, occ2_delay_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x15u, occ_recover_a10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x16u, occ_recover_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x17u, sc_a10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x18u, sc_delay_us) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x19u, sc_recover_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x1Au, chg_ot_x10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x1Bu, chg_ot_recover_x10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x1Cu, chg_ut_x10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x1Du, chg_ut_recover_x10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x1Eu, dsg_ot_x10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x1Fu, dsg_ot_recover_x10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x20u, dsg_ut_x10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x21u, dsg_ut_recover_x10) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x22u, temp_recover_ms) \
    X(BMS_COLD_AFE_HW_KEY_BASE + 0x23u, enable_mask)
'''
if 'BMS_COLD_AFE_HW_FIELD_LIST' not in s:
    marker = '#define BMS_COLD_FIELD_DESC_PROTECT(key, field)'
    s = s.replace(marker, AFE_LIST + '\n' + marker, 1)
    s = s.replace('#define BMS_COLD_FIELD_DESC_SYSTEM(key, field)  { key, BMS_COLD_OFFSETOF(bms_cold_system_params_t, field) },\n',
                  '#define BMS_COLD_FIELD_DESC_SYSTEM(key, field)  { key, BMS_COLD_OFFSETOF(bms_cold_system_params_t, field) },\n'
                  '#define BMS_COLD_FIELD_DESC_AFE_HW(key, field)  { key, BMS_COLD_OFFSETOF(bms_afe_hw_profile_t, field) },\n', 1)
    s = s.replace('static const bms_cold_field_desc_t g_bms_system_fields[] = {\n    BMS_COLD_SYSTEM_FIELD_LIST(BMS_COLD_FIELD_DESC_SYSTEM)\n};\n',
                  'static const bms_cold_field_desc_t g_bms_system_fields[] = {\n    BMS_COLD_SYSTEM_FIELD_LIST(BMS_COLD_FIELD_DESC_SYSTEM)\n};\n\n'
                  'static const bms_cold_field_desc_t g_bms_afe_hw_fields[] = {\n    BMS_COLD_AFE_HW_FIELD_LIST(BMS_COLD_FIELD_DESC_AFE_HW)\n};\n', 1)
    s = s.replace('#define BMS_COLD_BTNAME_COUNT   ((u16)(sizeof(g_bms_btname_keys) / sizeof(g_bms_btname_keys[0])))\n#define BMS_COLD_TOTAL_KEYS     ((u16)(BMS_COLD_PROTECT_COUNT + BMS_COLD_SYSTEM_COUNT + BMS_COLD_CTRL_COUNT + BMS_COLD_BTNAME_COUNT))\n',
                  '#define BMS_COLD_BTNAME_COUNT   ((u16)(sizeof(g_bms_btname_keys) / sizeof(g_bms_btname_keys[0])))\n'
                  '#define BMS_COLD_AFE_HW_COUNT   ((u16)(sizeof(g_bms_afe_hw_fields) / sizeof(g_bms_afe_hw_fields[0])))\n'
                  '#define BMS_COLD_TOTAL_KEYS     ((u16)(BMS_COLD_PROTECT_COUNT + BMS_COLD_SYSTEM_COUNT + BMS_COLD_CTRL_COUNT + BMS_COLD_BTNAME_COUNT + BMS_COLD_AFE_HW_COUNT))\n', 1)
    # Generic u16 accessors for the hardware profile.
    anchor = 'static u32 bms_cold_get_u32_value(const bms_cold_system_params_t *data, u16 offset)\n'
    generic = '''static u32 bms_cold_get_profile_u16(const bms_afe_hw_profile_t *data, u16 offset)\n{\n    const u16 *field = (const u16 *)((const u8 *)data + offset);\n    return *field;\n}\n\nstatic void bms_cold_set_profile_u16(bms_afe_hw_profile_t *data, u16 offset, u32 value)\n{\n    u16 *field = (u16 *)((u8 *)data + offset);\n    *field = (u16)value;\n}\n\n'''
    s = s.replace(anchor, generic + anchor, 1)
    # Append key definitions after btname definitions so existing index partitions stay stable.
    old = '''    for (i = 0; i < BMS_COLD_BTNAME_COUNT; ++i) {\n        g_bms_cold_keys[BMS_COLD_PROTECT_COUNT + BMS_COLD_SYSTEM_COUNT + BMS_COLD_CTRL_COUNT + i].key = g_bms_btname_keys[i];\n        g_bms_cold_keys[BMS_COLD_PROTECT_COUNT + BMS_COLD_SYSTEM_COUNT + BMS_COLD_CTRL_COUNT + i].default_value = 0u;\n    }\n'''
    new = old + '''\n    for (i = 0; i < BMS_COLD_AFE_HW_COUNT; ++i) {\n        u16 index = (u16)(BMS_COLD_PROTECT_COUNT + BMS_COLD_SYSTEM_COUNT +\n                          BMS_COLD_CTRL_COUNT + BMS_COLD_BTNAME_COUNT + i);\n        g_bms_cold_keys[index].key = g_bms_afe_hw_fields[i].key;\n        /* Zero schema/model is the one-time migration marker. */\n        g_bms_cold_keys[index].default_value = 0u;\n    }\n'''
    if old not in s:
        raise SystemExit('cold KV btname key loop anchor missing')
    s = s.replace(old, new, 1)
    # Add persistent profile API before control API.
    anchor = 'int bms_cold_kv_store_get_control_value(bms_cold_control_param_id_t item, u32 *value)\n'
    api = r'''int bms_cold_kv_store_get_afe_hw_profile(bms_afe_hw_profile_t *profile)
{
    u32 value;
    u16 i;
    if (profile == NULL) return FLASH_KV32_FAILED;
    if (!bms_cold_ensure_ready()) return FLASH_KV32_FAILED;
    memset(profile, 0, sizeof(*profile));
    for (i = 0u; i < BMS_COLD_AFE_HW_COUNT; ++i) {
        value = 0u;
        if (!flash_kv32_get(&g_bms_cold_kv, g_bms_afe_hw_fields[i].key, &value))
            return FLASH_KV32_FAILED;
        bms_cold_set_profile_u16(profile, g_bms_afe_hw_fields[i].offset, value);
    }
    return FLASH_KV32_SUCCESS;
}

int bms_cold_kv_store_set_afe_hw_profile(const bms_afe_hw_profile_t *profile)
{
    flash_kv32_pair_t pairs[BMS_COLD_AFE_HW_COUNT];
    u16 i;
    if (profile == NULL) return FLASH_KV32_FAILED;
    if (!bms_cold_ensure_ready()) return FLASH_KV32_FAILED;
    for (i = 0u; i < BMS_COLD_AFE_HW_COUNT; ++i) {
        pairs[i].key = g_bms_afe_hw_fields[i].key;
        pairs[i].value = bms_cold_get_profile_u16(profile, g_bms_afe_hw_fields[i].offset);
    }
    if (bms_cold_pairs_match_current(pairs, BMS_COLD_AFE_HW_COUNT))
        return FLASH_KV32_SUCCESS;
    return flash_kv32_write_pairs(&g_bms_cold_kv, pairs, BMS_COLD_AFE_HW_COUNT);
}

'''
    if anchor not in s:
        raise SystemExit('cold KV control API anchor missing')
    s = s.replace(anchor, api + anchor, 1)
c.write_text(s, encoding="utf-8")

# Add software-only parameter validation to the already-common protection core.
h = HERE / 'bms_sw_protection.h'
s = h.read_text(encoding='utf-8')
if 'bms_sw_protection_validate_params' not in s:
    s = s.replace('void bms_sw_protection_init(void);\n',
                  'uint8_t bms_sw_protection_validate_params(const struct PRT_E2ROM_PARAS *params);\nvoid bms_sw_protection_init(void);\n', 1)
    s = s.replace('#include <stdint.h>\n', '#include <stdint.h>\n#include "param.h"\n', 1)
h.write_text(s, encoding='utf-8')

c = HERE / 'bms_sw_protection.c'
s = c.read_text(encoding='utf-8')
if 'uint8_t bms_sw_protection_validate_params' not in s:
    insert = r'''uint8_t bms_sw_protection_validate_params(const struct PRT_E2ROM_PARAS *p)
{
    if (p == 0) return 0u;
    if ((p->u16VcellOvp_First > p->u16VcellOvp_Second) ||
        (p->u16VcellOvp_Second > p->u16VcellOvp_Third) ||
        (p->u16VbusOvp_First > p->u16VbusOvp_Second) ||
        (p->u16VbusOvp_Second > p->u16VbusOvp_Third) ||
        (p->u16IchgOcp_First > p->u16IchgOcp_Second) ||
        (p->u16IchgOcp_Second > p->u16IchgOcp_Third) ||
        (p->u16IdsgOcp_First > p->u16IdsgOcp_Second) ||
        (p->u16IdsgOcp_Second > p->u16IdsgOcp_Third) ||
        (p->u16TChgOTp_First > p->u16TChgOTp_Second) ||
        (p->u16TChgOTp_Second > p->u16TChgOTp_Third) ||
        (p->u16TdischgOTp_First > p->u16TdischgOTp_Second) ||
        (p->u16TdischgOTp_Second > p->u16TdischgOTp_Third) ||
        (p->u16TmosOTp_First > p->u16TmosOTp_Second) ||
        (p->u16TmosOTp_Second > p->u16TmosOTp_Third) ||
        (p->u16VdeltaOvp_First > p->u16VdeltaOvp_Second) ||
        (p->u16VdeltaOvp_Second > p->u16VdeltaOvp_Third)) return 0u;
    if ((p->u16VcellUvp_First < p->u16VcellUvp_Second) ||
        (p->u16VcellUvp_Second < p->u16VcellUvp_Third) ||
        (p->u16VbusUvp_First < p->u16VbusUvp_Second) ||
        (p->u16VbusUvp_Second < p->u16VbusUvp_Third) ||
        (p->u16TchgUTp_First < p->u16TchgUTp_Second) ||
        (p->u16TchgUTp_Second < p->u16TchgUTp_Third) ||
        (p->u16TdischgUTp_First < p->u16TdischgUTp_Second) ||
        (p->u16TdischgUTp_Second < p->u16TdischgUTp_Third)) return 0u;
    if ((p->u16VcellOvp_Third && p->u16VcellOvp_Rcv >= p->u16VcellOvp_Third) ||
        (p->u16VbusOvp_Third && p->u16VbusOvp_Rcv >= p->u16VbusOvp_Third) ||
        (p->u16IchgOcp_Third && p->u16IchgOcp_Rcv >= p->u16IchgOcp_Third) ||
        (p->u16IdsgOcp_Third && p->u16IdsgOcp_Rcv >= p->u16IdsgOcp_Third) ||
        (p->u16TChgOTp_Third && p->u16TChgOTp_Rcv >= p->u16TChgOTp_Third) ||
        (p->u16TdischgOTp_Third && p->u16TdischgOTp_Rcv >= p->u16TdischgOTp_Third) ||
        (p->u16TmosOTp_Third && p->u16TmosOTp_Rcv >= p->u16TmosOTp_Third) ||
        (p->u16VdeltaOvp_Third && p->u16VdeltaOvp_Rcv >= p->u16VdeltaOvp_Third)) return 0u;
    if ((p->u16VcellUvp_Third && p->u16VcellUvp_Rcv <= p->u16VcellUvp_Third) ||
        (p->u16VbusUvp_Third && p->u16VbusUvp_Rcv <= p->u16VbusUvp_Third) ||
        (p->u16TchgUTp_Third && p->u16TchgUTp_Rcv <= p->u16TchgUTp_Third) ||
        (p->u16TdischgUTp_Third && p->u16TdischgUTp_Rcv <= p->u16TdischgUTp_Third)) return 0u;
    if (p->u16TChgOTp_Third > 1450u || p->u16TChgOTp_Rcv > 1450u ||
        p->u16TchgUTp_Third > 1450u || p->u16TchgUTp_Rcv > 1450u ||
        p->u16TdischgOTp_Third > 1450u || p->u16TdischgOTp_Rcv > 1450u ||
        p->u16TdischgUTp_Third > 1450u || p->u16TdischgUTp_Rcv > 1450u ||
        p->u16TmosOTp_Third > 1450u || p->u16TmosOTp_Rcv > 1450u) return 0u;
    return 1u;
}

'''
    pos = s.find('void bms_sw_protection_clear(void)')
    if pos < 0: raise SystemExit('software protection clear anchor missing')
    s = s[:pos] + insert + s[pos:]
c.write_text(s, encoding='utf-8')

# SH control: hardware register programming must use the independent profile.
p = HERE / 'sh3673510_control.c'
s = p.read_text(encoding='utf-8')
if '#include "bms_afe_hw_profile.h"' not in s:
    s = s.replace('#include "param.h"\n', '#include "param.h"\n#include "bms_afe_hw_profile.h"\n', 1)
# Replace old software/Afe-coupled validator with profile validation.
s, n = re.subn(r'static uint8_t sh3510_validate_protection\(void\)\s*\{.*?\n\}\n\nuint8_t sh3673510_control_apply_protection',
'''static uint8_t sh3510_validate_protection(bms_afe_hw_profile_t *profile)\n{\n    return bms_afe_hw_profile_get(profile);\n}\n\nuint8_t sh3673510_control_apply_protection''', s, count=1, flags=re.S)
if n != 1: raise SystemExit(f'SH validator replacement count={n}')
# Inject profile and redirect hardware values.
s = s.replace('uint8_t sh3673510_control_apply_protection(void)\n{\n    uint32_t ov_delay_ms = (uint32_t)g_tParam.protect.u16VcellOvp_Filter * 10u;\n    uint32_t uv_delay_ms = (uint32_t)g_tParam.protect.u16VcellUvp_Filter * 10u;\n    uint32_t ocd_delay_ms = (uint32_t)g_tParam.protect.u16IdsgOcp_Filter * 10u;\n    uint32_t occ_delay_ms = (uint32_t)g_tParam.protect.u16IchgOcp_Filter * 10u;\n',
'''uint8_t sh3673510_control_apply_protection(void)\n{\n    bms_afe_hw_profile_t hw;\n    uint32_t ov_delay_ms;\n    uint32_t uv_delay_ms;\n    uint32_t ocd1_delay_ms;\n    uint32_t ocd2_delay_ms;\n    uint32_t occ_delay_ms;\n''', 1)
s = s.replace('    s_protection_actual.valid = 0u;\n    if (!s_control_ready || !sh3510_validate_protection()) return 0u;\n\n    ov_code = (uint16_t)(((uint32_t)g_tParam.protect.u16VcellOvp_Third + 2u) / 5u);\n    uv_code = (uint16_t)(((uint32_t)g_tParam.protect.u16VcellUvp_Third + 2u) / 5u);\n',
'''    s_protection_actual.valid = 0u;\n    if (!s_control_ready || !sh3510_validate_protection(&hw)) return 0u;\n    ov_delay_ms = hw.cov_delay_ms;\n    uv_delay_ms = hw.cuv_delay_ms;\n    ocd1_delay_ms = hw.ocd1_delay_ms;\n    ocd2_delay_ms = hw.ocd2_delay_ms;\n    occ_delay_ms = hw.occ1_delay_ms;\n\n    ov_code = (uint16_t)(((uint32_t)hw.cov_mv + 2u) / 5u);\n    uv_code = (uint16_t)(((uint32_t)hw.cuv_mv + 2u) / 5u);\n''', 1)
s = s.replace('sh3510_current_a10_to_sense_uv(g_tParam.protect.u16IdsgOcp_First)', 'sh3510_current_a10_to_sense_uv(hw.ocd1_a10)')
s = s.replace('sh3510_pick_ceiling_code(s_ov_delay_ms, 8u, ocd_delay_ms)', 'sh3510_pick_ceiling_code(s_ov_delay_ms, 8u, ocd1_delay_ms)', 1)
s = s.replace('sh3510_current_a10_to_sense_uv(g_tParam.protect.u16IdsgOcp_Second)', 'sh3510_current_a10_to_sense_uv(hw.ocd2_a10)')
s = s.replace('(ocd_delay_ms + 24u) / 25u', '(ocd2_delay_ms + 24u) / 25u')
# Dynamic SC multiplier/delay from physical requested profile.
old = '''    regv = (uint8_t)((SH3673510_D011_SC_MULTIPLIER_CODE << 4) |\n                     SH3673510_D011_SC_DELAY_CODE);\n    ok &= sh3510_write_verify(SH3673520_REG_SCV_SCT, regv, 0x3Fu);\n'''
new = '''    {\n        static const uint8_t sc_mult[4] = {2u, 3u, 4u, 6u};\n        static const uint16_t sc_delay[8] = {2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u};\n        uint8_t mult_code = 0u;\n        uint8_t delay_code = 0u;\n        uint32_t base = s_protection_actual.ocd2_a10;\n        if ((hw.enable_mask & BMS_AFE_HW_EN_SC) && base != 0u) {\n            while (mult_code < 3u && (u32)base * sc_mult[mult_code] < hw.sc_a10) ++mult_code;\n            while (delay_code < 7u && sc_delay[delay_code] < hw.sc_delay_us) ++delay_code;\n        }\n        regv = (uint8_t)((mult_code << 4) | delay_code);\n        ok &= sh3510_write_verify(SH3673520_REG_SCV_SCT, regv, 0x3Fu);\n    }\n'''
if old not in s: raise SystemExit('SH SC anchor missing')
s = s.replace(old, new, 1)
s = s.replace('sh3510_current_a10_to_sense_uv(g_tParam.protect.u16IchgOcp_First)', 'sh3510_current_a10_to_sense_uv(hw.occ1_a10)')
s = s.replace('sh3510_high_temp_code(g_tParam.protect.u16TChgOTp_Third, &code)', 'sh3510_high_temp_code(hw.chg_ot_x10, &code)')
s = s.replace('sh3510_high_temp_code(g_tParam.protect.u16TdischgOTp_Third, &code)', 'sh3510_high_temp_code(hw.dsg_ot_x10, &code)')
s = s.replace('sh3510_low_temp_code(g_tParam.protect.u16TchgUTp_Third, &code)', 'sh3510_low_temp_code(hw.chg_ut_x10, &code)')
s = s.replace('sh3510_low_temp_code(g_tParam.protect.u16TdischgUTp_Third, &code)', 'sh3510_low_temp_code(hw.dsg_ut_x10, &code)')
# Dynamic hardware enables: OCC in SCONF5, the rest in SCONF6.
old = '''    /* Enable the selected hardware protections only after all thresholds are valid. */\n    ok &= sh3510_write_verify(SH3673520_REG_SCONF6,\n                              SH3673510_D011_SCONF6_VALUE,\n                              SH3673520_SCONF6_ALL_MASK);\n'''
new = '''    /* Hardware protection enables belong to the independent AFE profile. */\n    ok &= sh3510_update_reg(SH3673520_REG_SCONF5,\n                            SH3673520_SCONF5_OCC_EN_MASK,\n                            (hw.enable_mask & BMS_AFE_HW_EN_OCC1) ? SH3673520_SCONF5_OCC_EN_MASK : 0u);\n    regv = 0u;\n    if (hw.enable_mask & BMS_AFE_HW_EN_COV)  regv |= SH3673520_SCONF6_OV_EN_MASK;\n    if (hw.enable_mask & BMS_AFE_HW_EN_CUV)  regv |= SH3673520_SCONF6_UV_EN_MASK;\n    if (hw.enable_mask & (BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2)) regv |= SH3673520_SCONF6_OCD_EN_MASK;\n    if (hw.enable_mask & BMS_AFE_HW_EN_SC)   regv |= SH3673520_SCONF6_SC_EN_MASK;\n    if (hw.enable_mask & BMS_AFE_HW_EN_TEMP) regv |= (SH3673520_SCONF6_TS1_EN_MASK | SH3673520_SCONF6_TS2_EN_MASK);\n    ok &= sh3510_write_verify(SH3673520_REG_SCONF6, regv, SH3673520_SCONF6_ALL_MASK);\n'''
if old not in s: raise SystemExit('SH SCONF6 anchor missing')
s = s.replace(old, new, 1)
p.write_text(s, encoding='utf-8')

# SH hardware latch recovery also uses the independent recovery parameters.
p = HERE / 'sh3673510_bms.c'
s = p.read_text(encoding='utf-8')
if '#include "bms_afe_hw_profile.h"' not in s:
    s = s.replace('#include "bms_sw_protection.h"\n', '#include "bms_sw_protection.h"\n#include "bms_afe_hw_profile.h"\n', 1)
# Add profile load in service function.
s = s.replace('static void service_hw_flag_recovery(const sh3673510_control_status_t *s)\n{\n    sh3673510_protection_actual_t actual;\n',
'''static void service_hw_flag_recovery(const sh3673510_control_status_t *s)\n{\n    sh3673510_protection_actual_t actual;\n    bms_afe_hw_profile_t hw;\n''', 1)
s = s.replace('    if (s == 0) return;\n\n    actual_ok = sh3673510_control_get_protection_actual(&actual);\n',
'''    if (s == 0) return;\n    if (!bms_afe_hw_profile_get(&hw)) return;\n\n    actual_ok = sh3673510_control_get_protection_actual(&actual);\n''', 1)
repls = {
'g_tParam.protect.u16VcellOvp_Rcv':'hw.cov_recover_mv',
'g_tParam.protect.u16VcellOvp_Filter':'(u16)((hw.cov_recover_ms + 5u) / 10u)',
'g_tParam.protect.u16VcellUvp_Rcv':'hw.cuv_recover_mv',
'g_tParam.protect.u16VcellUvp_Filter':'(u16)((hw.cuv_recover_ms + 5u) / 10u)',
'g_tParam.protect.u16IdsgOcp_Rcv':'hw.ocd_recover_a10',
'SH3510_OCD_RELEASE_FILTER_10MS':'(u16)((hw.ocd_recover_ms + 5u) / 10u)',
'g_tParam.protect.u16IchgOcp_Rcv':'hw.occ_recover_a10',
'g_tParam.protect.u16IchgOcp_Filter':'(u16)((hw.occ_recover_ms + 5u) / 10u)',
'g_tParam.protect.u16TChgOTp_Rcv':'hw.chg_ot_recover_x10',
'g_tParam.protect.u16TChgOTp_Filter':'(u16)((hw.temp_recover_ms + 5u) / 10u)',
'g_tParam.protect.u16TdischgOTp_Rcv':'hw.dsg_ot_recover_x10',
'g_tParam.protect.u16TdischgOTp_Filter':'(u16)((hw.temp_recover_ms + 5u) / 10u)',
'g_tParam.protect.u16TchgUTp_Rcv':'hw.chg_ut_recover_x10',
'g_tParam.protect.u16TchgUTp_Filter':'(u16)((hw.temp_recover_ms + 5u) / 10u)',
'g_tParam.protect.u16TdischgUTp_Rcv':'hw.dsg_ut_recover_x10',
'g_tParam.protect.u16TdischgUTp_Filter':'(u16)((hw.temp_recover_ms + 5u) / 10u)',
}
# Only replace inside hardware recovery function to leave heater/software behavior unchanged.
start = s.find('static void service_hw_flag_recovery')
end = s.find('static uint8_t service_afe_reconfiguration', start)
if start < 0 or end <= start: raise SystemExit('SH recovery function bounds missing')
block = s[start:end]
for a,b in repls.items(): block = block.replace(a,b)
s = s[:start] + block + s[end:]
p.write_text(s, encoding='utf-8')

# Modbus: add common 0x2500 atomic hardware-profile window and stop software writes from reprogramming AFE HW.
p = HERE / 'modbus_rtu.h'
s = p.read_text(encoding='utf-8')
if 'BMS_AFE_HW_PROFILE_REG_BASE' not in s:
    s += '\n#define BMS_AFE_HW_PROFILE_REG_BASE  0x2500u\n#define BMS_AFE_HW_PROFILE_WORDS     35u\n#define BMS_AFE_HW_PROFILE_REG_COUNT 40u\n'
p.write_text(s, encoding='utf-8')

p = HERE / 'modbus_rtu.c'
s = p.read_text(encoding='utf-8')
if '#include "bms_afe_hw_profile.h"' not in s:
    s = s.replace('#include "bms_state.h"\n', '#include "bms_state.h"\n#include "bms_sw_protection.h"\n#include "bms_afe_hw_profile.h"\n', 1)
helpers = r'''
static int afe_hw_profile_is_reg(u16 reg)
{
    return (reg >= BMS_AFE_HW_PROFILE_REG_BASE &&
            reg < (u16)(BMS_AFE_HW_PROFILE_REG_BASE + BMS_AFE_HW_PROFILE_REG_COUNT));
}

static u16 afe_hw_profile_read_reg(u16 reg)
{
    bms_afe_hw_profile_t p;
    u16 offset = (u16)(reg - BMS_AFE_HW_PROFILE_REG_BASE);
    if (offset < BMS_AFE_HW_PROFILE_WORDS) {
        if (!bms_afe_hw_profile_get(&p)) return 0xFFFFu;
        return ((const u16 *)&p)[offset];
    }
    switch (offset) {
    case 35u: return bms_afe_hw_profile_capabilities();
    case 36u: return bms_afe_hw_profile_get(&p) ? 1u : 0u;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    case 37u: return SH3673510_D011_SHUNT_UOHM;
    case 38u: return SH3673510_D011_CELL_COUNT;
    case 39u: return SH3673510_D011_WDT_EN ? 32u : 0u;
#elif BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    case 37u: return DVC1124_DEFAULT_SHUNT_UOHM;
    case 38u: return DVC1124_DEFAULT_CELL_COUNT;
    case 39u: return DVC1124_I2C_WATCHDOG_SECONDS;
#endif
    default: return 0xFFFFu;
    }
}

static u8 afe_hw_profile_write_block(const u8 *pdata, u16 qty)
{
    bms_afe_hw_profile_t before;
    bms_afe_hw_profile_t candidate;
    u16 i;
    if (pdata == 0 || qty != BMS_AFE_HW_PROFILE_WORDS) return MB_EX_ILLEGAL_VALUE;
    if (!bms_afe_hw_profile_get(&before)) return MB_EX_DEVICE_FAILURE;
    candidate = before;
    for (i = 0u; i < qty; ++i)
        ((u16 *)&candidate)[i] = u16be(&pdata[(u32)i * 2u]);
    if (!bms_afe_hw_profile_validate(&candidate)) return MB_EX_ILLEGAL_VALUE;
    if (!bms_afe_hw_profile_set(&candidate)) return MB_EX_DEVICE_FAILURE;
    if (!bms_afe_apply_protection_config()) {
        (void)bms_afe_hw_profile_set(&before);
        (void)bms_afe_apply_protection_config();
        return MB_EX_DEVICE_FAILURE;
    }
    return 0u;
}
'''
if 'static int afe_hw_profile_is_reg' not in s:
    pos = s.find('static u16 read_fault_history_reg')
    if pos < 0: raise SystemExit('modbus helper anchor missing')
    s = s[:pos] + helpers + '\n' + s[pos:]
# Read profile before legacy ranges.
s = s.replace('static u16 read_reg(u16 reg)\n{\n    u16 val;\n\n    if (dvc_comm_is_semantic(reg) || dvc_comm_is_raw(reg))\n',
'''static u16 read_reg(u16 reg)\n{\n    u16 val;\n\n    if (afe_hw_profile_is_reg(reg)) return afe_hw_profile_read_reg(reg);\n    if (dvc_comm_is_semantic(reg) || dvc_comm_is_raw(reg))\n''', 1)
# Reject one-word writes to profile; only full 0x10 transaction is allowed.
s = s.replace('static u8 write_reg(u16 reg, u16 val)\n{\n    if (dvc_comm_is_semantic(reg) || dvc_comm_is_raw(reg))\n',
'''static u8 write_reg(u16 reg, u16 val)\n{\n    if (afe_hw_profile_is_reg(reg)) return MB_EX_ILLEGAL_ADDRESS;\n    if (dvc_comm_is_semantic(reg) || dvc_comm_is_raw(reg))\n''', 1)
# Software parameter commit is software-only after decoupling.
s, n = re.subn(r'static u8 commit_protection_update\(const struct PRT_E2ROM_PARAS \*previous\)\s*\{.*?\n\}',
'''static u8 commit_protection_update(const struct PRT_E2ROM_PARAS *previous)\n{\n    if (!bms_sw_protection_validate_params(&g_tParam.protect)) {\n        g_tParam.protect = *previous;\n        return MB_EX_ILLEGAL_VALUE;\n    }\n    if (!SaveParam()) {\n        g_tParam.protect = *previous;\n        return MB_EX_DEVICE_FAILURE;\n    }\n    return 0u;\n}''', s, count=1, flags=re.S)
if n != 1: raise SystemExit(f'commit protection replacement count={n}')
# Insert exact atomic profile write before generic multi-register loop.
needle = '        previous_protect = g_tParam.protect;\n        pdata = &req[7];\n'
replacement = '''        pdata = &req[7];\n        if (reg == BMS_AFE_HW_PROFILE_REG_BASE) {\n            if (qty != BMS_AFE_HW_PROFILE_WORDS)\n                return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);\n            exception = afe_hw_profile_write_block(pdata, qty);\n            if (exception != 0u)\n                return modbus_exception(addr, func, exception, rsp, rsp_len);\n            if (addr == 0x00u) return 0;\n            rsp[0] = addr; rsp[1] = func; put_u16be(&rsp[2], reg); put_u16be(&rsp[4], qty);\n            crc = mb_crc16(rsp, 6u); rsp[6] = (u8)(crc & 0xFFu); rsp[7] = (u8)(crc >> 8); *rsp_len = 8u;\n            return 1;\n        }\n        if (afe_hw_profile_is_reg(reg) || afe_hw_profile_is_reg((u16)(reg + qty - 1u)))\n            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);\n\n        previous_protect = g_tParam.protect;\n'''
if needle not in s: raise SystemExit('modbus 0x10 anchor missing')
s = s.replace(needle, replacement, 1)
p.write_text(s, encoding='utf-8')

# Static contract to prevent accidental re-coupling.
test = ROOT / 'tests' / 'afe_hw_profile_contract_check.py'
test.write_text(r'''#!/usr/bin/env python3
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HERE=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001'/'tc_ble_single_sdk'/'vendor'/'ble_sample'
def text(name): return (HERE/name).read_text(encoding='utf-8')
p=text('bms_afe_hw_profile.c'); m=text('modbus_rtu.c'); c=text('sh3673510_control.c'); b=text('sh3673510_bms.c'); kv=text('bms_cold_kv_store.c')
assert 'BMS_COLD_AFE_HW_KEY_BASE   0x5000u' in kv
assert 'BMS_AFE_HW_PROFILE_SCHEMA_VERSION' in p
assert 'g_tParam.protect' in p  # one-time migration source only
assert 'qty != BMS_AFE_HW_PROFILE_WORDS' in m
assert 'bms_afe_hw_profile_set(&candidate)' in m
commit=m[m.index('static u8 commit_protection_update'):m.index('u16 mb_crc16')]
assert 'bms_afe_apply_protection_config' not in commit
apply=c[c.index('uint8_t sh3673510_control_apply_protection'):c.index('uint8_t sh3673510_control_get_protection_actual')]
assert 'g_tParam.protect' not in apply
assert 'bms_afe_hw_profile_get(&hw)' in b
print('Independent AFE hardware protection profile contract: PASS')
''', encoding='utf-8')

print('AFE hardware protection split staged')
