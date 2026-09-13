#!/usr/bin/env python3
"""One-shot HS-D011 safety refactor.

This script is intentionally deterministic and branch-specific.  It removes
legacy board GPIO aliases, makes PB5/HT-RF-EN a fail-safe-only heater-fuse
trigger, hardens SH3673510 protection recovery/FET arbitration, and exposes
actual AFE quantized protection values.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
TEST = ROOT / "tests" / "sh3673510_d011_integration_check.py"
DOC_HW = ROOT / "docs" / "D011_HARDWARE_REFERENCE.md"
DOC_STATUS = ROOT / "docs" / "D011_DEVELOPMENT_STATUS.md"
CATALOG = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "docs" / "register_catalog.json"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="strict")


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="\n")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count == 0:
        if new in text:
            return text
        raise RuntimeError(f"{label}: expected source text not found")
    if count != 1:
        raise RuntimeError(f"{label}: expected one source occurrence, got {count}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    out, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count == 0:
        if replacement.strip() in text:
            return text
        raise RuntimeError(f"{label}: regex target not found")
    return out


def patch_project_config() -> None:
    path = VENDOR / "sh3673510_project_config.h"
    text = read(path)
    text = text.replace(
        "#define D011_HEATER_RF_EN_PIN                     GPIO_PB5",
        "#define D011_HEATER_FUSE_TRIGGER_PIN              GPIO_PB5  /* HT-RF-EN: heater-circuit fuse trigger; keep LOW unless a separately validated irreversible fuse state machine authorizes firing. */",
    )
    text = text.replace(
        "#define D011_HEATER_CHG_PIN                       GPIO_PB4\n#define D011_HEATER_FUSE_TRIGGER_PIN",
        "#define D011_HEATER_CHG_PIN                       GPIO_PB4\n#define D011_HEATER_FUSE_SAFE_LEVEL               0u\n#define D011_HEATER_FUSE_TRIGGER_PIN",
    )
    if "D011_HEATER_RF_EN_PIN" in text:
        raise RuntimeError("project config still contains obsolete D011_HEATER_RF_EN_PIN")
    write(path, text)


def patch_conf_and_safe_aliases() -> None:
    safe = {
        "SW_PIN": "D011_SWITCH_PIN",
        "HEATER_EN_PIN": "D011_HEATER_CHG_PIN",
        "OWC_TX_PIN": "D011_SCI1_TX_PIN",
        "OWC_RX_PIN": "D011_SCI1_RX_PIN",
        "RS485_EN_PIN": "D011_RS485_EN_PIN",
        "LED_BLUE_PIN": "D011_DEBUG_LED_PIN",
    }
    conf = VENDOR / "conf.h"

    for path in VENDOR.rglob("*"):
        if path.suffix not in {".c", ".h"} or path == conf:
            continue
        text = read(path)
        original = text
        for old, new in safe.items():
            text = re.sub(rf"\b{re.escape(old)}\b", new, text)
        if text != original:
            write(path, text)

    text = read(conf)
    pattern = r"/\*\n \* HS-D011 physical MCU nets\..*?#define MCU_LDO_PIN\s+D011_CMNT_EN_PIN\n"
    replacement = """/* HS-D011 board code must use only the canonical D011_* nets from
 * sh3673510_project_config.h.  Do not recreate legacy board aliases such as
 * CHG_IN_PIN, RF_EN_PIN, MCU_LDO_PIN or AFE1_PRO_EN_PIN: their old semantics do
 * not exist on D011 and previously caused unsafe cross-board behavior. */
"""
    if any(token in text for token in ("CHG_IN_PIN", "RF_EN_PIN", "AFE1_PRO_EN_PIN", "MCU_LDO_PIN")):
        text = regex_once(text, pattern, replacement, "remove legacy D011 GPIO aliases")
    write(conf, text)


def patch_app() -> None:
    path = VENDOR / "app.c"
    text = read(path)

    text = regex_once(
        text,
        r"UINT8 IsChargerWakeupActive\(void\).*?static u32 app_pm_take_elapsed_seconds",
        """static UINT8 d011_switch_is_on(void)
{
#ifdef _DI_SWITCH_SYS_ONOFF
\treturn gpio_read(D011_SWITCH_PIN) ? 0u : 1u;
#else
\treturn 1u;
#endif
}

static u32 app_pm_take_elapsed_seconds""",
        "remove fake charger/key aliases",
    )

    text = regex_once(
        text,
        r"static int app_deepsleep_pad_wakeup_active\(void\)\n\{.*?\n\}\n\nstatic int app_note_sleep_and_enter_deepsleep",
        """static int app_deepsleep_pad_wakeup_active(void)
{
\t/* Only use D011 schematic-backed wake nets with verified active levels. */
\tif (d011_switch_is_on()) return 1;
\tif (gpio_read(D011_INT_WK_MCU_PIN)) return 1;      /* active high */
\tif (!gpio_read(D011_AFE_ALARM_PIN)) return 1;     /* active low */
\tif (!gpio_read(D011_AFE_RESET_OUT_PIN)) return 1; /* active low */
\treturn 0;
}

static int app_note_sleep_and_enter_deepsleep""",
        "D011 deep-sleep wake precheck",
    )

    text = regex_once(
        text,
        r"void mos_update\(void\)\n\{.*?\n\}\n\n\n#define LENGTH_TBLTEMP_MCU_10K",
        """void mos_update(void)
{
\t/* D011 has no validated dedicated CHG_IN GPIO.  Preserve the branch's
\t * effective behavior: CHG is normally requested and protection decides
\t * whether it may conduct; DSG additionally follows the active-low switch.
\t * The AFE adapter owns final fail-safe arbitration. */
\tuint8_t chg_target = 1u;
\tuint8_t dsg_target = d011_switch_is_on() ? 1u : 0u;

\tg_bms_system_status.bits.b1Status_Cool = 0u;
\t(void)bms_afe_set_fets(chg_target, dsg_target);
}


#define LENGTH_TBLTEMP_MCU_10K""",
        "D011 MOS request policy",
    )

    text = regex_once(
        text,
        r"void app_adc_multi_sample\(void\)\n\{.*?\n\}\n\nstatic void board_init",
        """void app_adc_multi_sample(void)
{
\tbms_afe_aux_measurements_t aux;

\tif (sys_time.low_power_mode) return;
\tif (!bms_afe_get_aux_measurements(&aux)) return;

\t/* Legacy reporting mirror only.  Protection, heater control and any
\t * irreversible fuse action belong to the D011 AFE/safety layer. */
\tg_stCellInfoReport.u16Temperature[8] = bms_lookup_u16(iSheldTemp_10K_mcu,
\t\t\t\t\t\t\t\t\t\t (UINT16)LENGTH_TBLTEMP_MCU_10K,
\t\t\t\t\t\t\t\t\t\t (UINT16)aux.battery_ntc_100ohm);
\tg_stCellInfoReport.u16Temperature[9] = bms_lookup_u16(iSheldTemp_10K_mcu,
\t\t\t\t\t\t\t\t\t\t (UINT16)LENGTH_TBLTEMP_MCU_10K,
\t\t\t\t\t\t\t\t\t\t (UINT16)aux.mos_ntc_100ohm);

#ifdef DISP_VBAT_AND_TEMP_
\tg_stCellInfoReport.u16VCell[29] = aux.battery_ntc_mv;
\tg_stCellInfoReport.u16VCell[30] = aux.mos_ntc_mv;
\tg_stCellInfoReport.u16VCell[31] = (UINT16)aux.pack_voltage_mv;
#endif
}

static void board_init""",
        "remove legacy certification/fuse logic",
    )

    text = regex_once(
        text,
        r"static void board_init\(void\)\n\{.*?\n\}\n\n_attribute_data_retention_ int device_in_connection_state;",
        """static void board_init(void)
{
\tbms_afe_set_output_enabled(0u);

\t/* PB5/HT-RF-EN is an irreversible heater-fuse trigger.  Until its
\t * complete validated firing state machine exists it is forced LOW only. */
\tgpio_set_func(D011_HEATER_FUSE_TRIGGER_PIN, AS_GPIO);
\tgpio_write(D011_HEATER_FUSE_TRIGGER_PIN, D011_HEATER_FUSE_SAFE_LEVEL);
\tgpio_set_input_en(D011_HEATER_FUSE_TRIGGER_PIN, 0);
\tgpio_set_output_en(D011_HEATER_FUSE_TRIGGER_PIN, 1);

\tgpio_set_func(D011_SWITCH_PIN, AS_GPIO);
\tgpio_set_input_en(D011_SWITCH_PIN, 1);
\tgpio_set_output_en(D011_SWITCH_PIN, 0);

\t/* D011 PD4 controls the isolated communication 3V3 rail; it is not an
\t * MCU-LDO/AFE-protection-enable alias.  Keep communications powered while
\t * the normal application is running. */
\tgpio_set_func(D011_CMNT_EN_PIN, AS_GPIO);
\tgpio_write(D011_CMNT_EN_PIN, 1);
\tgpio_set_input_en(D011_CMNT_EN_PIN, 0);
\tgpio_set_output_en(D011_CMNT_EN_PIN, 1);

\t/* PD3 is the schematic CMNT-WK input.  Its active polarity is not yet
\t * hardware-verified, so configure it as input but do not invent a wake
\t * polarity here. */
\tgpio_set_func(D011_CMNT_WK_PIN, AS_GPIO);
\tgpio_set_output_en(D011_CMNT_WK_PIN, 0);
\tgpio_set_input_en(D011_CMNT_WK_PIN, 1);
}

_attribute_data_retention_ int device_in_connection_state;""",
        "D011 board init",
    )

    text = text.replace(
        "cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 1);",
        "cpu_set_gpio_wakeup(D011_SWITCH_PIN, Level_Low, 1);",
    )
    text = re.sub(r"\bSW_PIN\b", "D011_SWITCH_PIN", text)

    forbidden = ["CHG_IN_PIN", "RF_EN_PIN", "IsChargerWakeupActive", "IsKeyWakeupActive"]
    for token in forbidden:
        if re.search(rf"\b{re.escape(token)}\b", text):
            raise RuntimeError(f"app.c still contains obsolete token {token}")
    write(path, text)


def patch_control_header() -> None:
    path = VENDOR / "sh3673510_control.h"
    text = read(path)
    marker = """typedef struct
{
    uint8_t flag1;
    uint8_t flag2;
    uint8_t bstatus1;
    uint8_t bstatus2;
} sh3673510_control_status_t;
"""
    addition = marker + """
typedef struct
{
    uint16_t ov_mv;
    uint16_t uv_mv;
    uint16_t ocd1_a10;
    uint16_t ocd2_a10;
    uint16_t occ_a10;
    uint16_t ov_delay_ms;
    uint16_t uv_delay_ms;
    uint16_t ocd1_delay_ms;
    uint16_t ocd2_delay_ms;
    uint16_t occ_delay_ms;
    uint8_t valid;
} sh3673510_protection_actual_t;
"""
    if "sh3673510_protection_actual_t" not in text:
        text = replace_once(text, marker, addition, "add protection actual type")
    text = text.replace(
        "uint8_t sh3673510_control_apply_protection(void);",
        "uint8_t sh3673510_control_apply_protection(void);\nuint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual);",
    )
    text = text.replace(
        "void sh3673510_board_set_heater(uint8_t enabled);",
        "void sh3673510_board_set_heater(uint8_t enabled);\nvoid sh3673510_board_force_heater_fuse_safe(void);",
    )
    write(path, text)


def patch_control() -> None:
    path = VENDOR / "sh3673510_control.c"
    text = read(path)
    text = text.replace(
        "static uint8_t s_afe_sleeping;",
        "static uint8_t s_afe_sleeping;\nstatic sh3673510_protection_actual_t s_protection_actual;",
    )

    block = r'''static const uint16_t s_ov_delay_ms[8] = {
    140u, 280u, 490u, 980u, 2030u, 3010u, 4970u, 10010u
};
static const uint16_t s_uv_delay_ms[8] = {
    490u, 770u, 980u, 1470u, 2030u, 3010u, 4970u, 10010u
};

static uint8_t sh3510_pick_ceiling_code(const uint16_t *table, uint8_t count,
                                         uint32_t requested)
{
    uint8_t i;
    if ((table == 0) || (count == 0u)) return 0u;
    for (i = 0u; i < count; ++i)
    {
        if (requested <= (uint32_t)table[i]) return i;
    }
    return (uint8_t)(count - 1u);
}

static uint16_t sh3510_temp_to_res100(uint16_t temp_x10)
{
    uint8_t i;
    const uint8_t pairs = (uint8_t)(sizeof(s_ntc_10k_table) / sizeof(s_ntc_10k_table[0]) / 2u);

    if (temp_x10 <= s_ntc_10k_table[1]) return s_ntc_10k_table[0];
    if (temp_x10 >= s_ntc_10k_table[(pairs - 1u) * 2u + 1u])
        return s_ntc_10k_table[(pairs - 1u) * 2u];

    for (i = 0u; i + 1u < pairs; ++i)
    {
        uint16_t r1 = s_ntc_10k_table[i * 2u];
        uint16_t t1 = s_ntc_10k_table[i * 2u + 1u];
        uint16_t r2 = s_ntc_10k_table[(i + 1u) * 2u];
        uint16_t t2 = s_ntc_10k_table[(i + 1u) * 2u + 1u];
        if (temp_x10 >= t1 && temp_x10 <= t2)
        {
            uint32_t dt = (uint32_t)(temp_x10 - t1);
            uint32_t span = (uint32_t)(t2 - t1);
            uint32_t drop = ((uint32_t)(r1 - r2) * dt + span / 2u) / span;
            return (uint16_t)((uint32_t)r1 - drop);
        }
    }
    return 100u;
}

static uint8_t sh3510_high_temp_code(uint16_t temp_x10, uint8_t *code)
{
    int32_t numerator;
    uint32_t denominator;
    uint16_t r100;
    int32_t result;

    if (code == 0) return 0u;
    r100 = sh3510_temp_to_res100(temp_x10);
    denominator = (uint32_t)r100 + 100u;
    numerator = 700L - (3L * (int32_t)r100);
    if (numerator < 0L) return 0u;
    result = (numerator * 512L + (int32_t)(denominator * 5u)) /
             (int32_t)(denominator * 10u);
    if (result < 0L || result > 255L) return 0u;
    *code = (uint8_t)result;
    return 1u;
}

static uint8_t sh3510_low_temp_code(uint16_t temp_x10, uint8_t *code)
{
    int32_t numerator;
    uint32_t denominator;
    uint16_t r100;
    int32_t result;

    if (code == 0) return 0u;
    r100 = sh3510_temp_to_res100(temp_x10);
    if (r100 < 100u) return 0u;
    denominator = (uint32_t)r100 + 100u;
    numerator = (int32_t)r100 - 100L;
    result = (numerator * 256L + (int32_t)(denominator / 2u)) /
             (int32_t)denominator;
    if (result < 0L || result > 255L) return 0u;
    *code = (uint8_t)result;
    return 1u;
}

static uint32_t sh3510_current_a10_to_sense_uv(uint16_t current_a10)
{
    return ((uint32_t)current_a10 * SH3673510_D011_SHUNT_UOHM + 5u) / 10u;
}

static uint16_t sh3510_sense_uv_to_current_a10(uint32_t sense_uv)
{
    uint32_t value = (sense_uv * 10u + SH3673510_D011_SHUNT_UOHM - 1u) /
                     SH3673510_D011_SHUNT_UOHM;
    return (uint16_t)((value > 65535u) ? 65535u : value);
}

static uint8_t sh3510_step_code_ceiling(uint32_t requested_uv,
                                         uint32_t step_uv,
                                         uint8_t max_code)
{
    uint32_t steps;
    if (step_uv == 0u) return 0u;
    steps = (requested_uv + step_uv - 1u) / step_uv;
    if (steps == 0u) steps = 1u;
    if (steps > (uint32_t)max_code + 1u) steps = (uint32_t)max_code + 1u;
    return (uint8_t)(steps - 1u);
}

static uint8_t sh3510_validate_protection(void)
{
    const struct PRT_E2ROM_PARAS *p = &g_tParam.protect;
    if ((p->u16VcellOvp_Third == 0u) || (p->u16VcellOvp_Third > 5115u)) return 0u;
    if ((p->u16VcellUvp_Third == 0u) || (p->u16VcellUvp_Third > 5115u)) return 0u;
    if (p->u16VcellOvp_Rcv >= p->u16VcellOvp_Third) return 0u;
    if (p->u16VcellUvp_Rcv <= p->u16VcellUvp_Third) return 0u;
    if (p->u16IchgOcp_Rcv >= p->u16IchgOcp_Third) return 0u;
    if (p->u16IdsgOcp_Rcv >= p->u16IdsgOcp_Third) return 0u;
    if (p->u16TChgOTp_Rcv >= p->u16TChgOTp_Third) return 0u;
    if (p->u16TdischgOTp_Rcv >= p->u16TdischgOTp_Third) return 0u;
    if (p->u16TchgUTp_Rcv <= p->u16TchgUTp_Third) return 0u;
    if (p->u16TdischgUTp_Rcv <= p->u16TdischgUTp_Third) return 0u;
    if ((p->u16TChgOTp_Third > 1450u) || (p->u16TdischgOTp_Third > 1450u) ||
        (p->u16TchgUTp_Third > 1450u) || (p->u16TdischgUTp_Third > 1450u)) return 0u;
    return 1u;
}

uint8_t sh3673510_control_apply_protection(void)
{
    uint32_t ov_delay_ms = (uint32_t)g_tParam.protect.u16VcellOvp_Filter * 10u;
    uint32_t uv_delay_ms = (uint32_t)g_tParam.protect.u16VcellUvp_Filter * 10u;
    uint32_t ocd_delay_ms = (uint32_t)g_tParam.protect.u16IdsgOcp_Filter * 10u;
    uint32_t occ_delay_ms = (uint32_t)g_tParam.protect.u16IchgOcp_Filter * 10u;
    uint16_t ov_code;
    uint16_t uv_code;
    uint32_t sense_uv;
    uint32_t actual_uv;
    uint8_t ov_dly;
    uint8_t uv_dly;
    uint8_t regv;
    uint8_t high;
    uint8_t low;
    uint8_t code;
    uint8_t ok = 1u;

    s_protection_actual.valid = 0u;
    if (!s_control_ready || !sh3510_validate_protection()) return 0u;

    ov_code = (uint16_t)(((uint32_t)g_tParam.protect.u16VcellOvp_Third + 2u) / 5u);
    uv_code = (uint16_t)(((uint32_t)g_tParam.protect.u16VcellUvp_Third + 2u) / 5u);
    if (ov_code > 0x03FFu) ov_code = 0x03FFu;
    if (uv_code > 0x03FFu) uv_code = 0x03FFu;
    ov_dly = sh3510_pick_ceiling_code(s_ov_delay_ms, 8u, ov_delay_ms);
    uv_dly = sh3510_pick_ceiling_code(s_uv_delay_ms, 8u, uv_delay_ms);

    high = (uint8_t)((ov_dly << 4) | ((ov_code >> 8) & 0x03u));
    low = (uint8_t)(ov_code & 0xFFu);
    ok &= sh3510_write_verify(SH3673520_REG_OVT_OVH, high, 0x73u);
    ok &= sh3510_write_verify(SH3673520_REG_OVL, low, 0xFFu);

    high = (uint8_t)((uv_dly << 4) | ((uv_code >> 8) & 0x03u));
    low = (uint8_t)(uv_code & 0xFFu);
    ok &= sh3510_write_verify(SH3673520_REG_UVT_UVH, high, 0x73u);
    ok &= sh3510_write_verify(SH3673520_REG_UVL, low, 0xFFu);

    sense_uv = sh3510_current_a10_to_sense_uv(g_tParam.protect.u16IdsgOcp_First);
    code = sh3510_step_code_ceiling(sense_uv, 5000u, 15u);
    regv = (uint8_t)((sh3510_pick_ceiling_code(s_ov_delay_ms, 8u, ocd_delay_ms) << 4) | code);
    ok &= sh3510_write_verify(SH3673520_REG_OCD1V_OCD1T, regv, 0x7Fu);
    actual_uv = ((uint32_t)code + 1u) * 5000u;
    s_protection_actual.ocd1_a10 = sh3510_sense_uv_to_current_a10(actual_uv);
    s_protection_actual.ocd1_delay_ms = s_ov_delay_ms[(regv >> 4) & 0x07u];

    sense_uv = sh3510_current_a10_to_sense_uv(g_tParam.protect.u16IdsgOcp_Second);
    code = sh3510_step_code_ceiling(sense_uv, 10000u, 15u);
    {
        uint32_t steps = (ocd_delay_ms + 24u) / 25u;
        uint8_t dly;
        if (steps == 0u) steps = 1u;
        if (steps > 16u) steps = 16u;
        dly = (uint8_t)(steps - 1u);
        regv = (uint8_t)((dly << 4) | code);
    }
    ok &= sh3510_write_verify(SH3673520_REG_OCD2V_OCD2T, regv, 0xFFu);
    actual_uv = ((uint32_t)code + 1u) * 10000u;
    s_protection_actual.ocd2_a10 = sh3510_sense_uv_to_current_a10(actual_uv);
    s_protection_actual.ocd2_delay_ms = (uint16_t)((((regv >> 4) & 0x0Fu) + 1u) * 25u);

    regv = (uint8_t)((SH3673510_D011_SC_MULTIPLIER_CODE << 4) |
                     SH3673510_D011_SC_DELAY_CODE);
    ok &= sh3510_write_verify(SH3673520_REG_SCV_SCT, regv, 0x3Fu);

    sense_uv = sh3510_current_a10_to_sense_uv(g_tParam.protect.u16IchgOcp_First);
    code = sh3510_step_code_ceiling(sense_uv, 1375u, 31u);
    regv = (uint8_t)((sh3510_pick_ceiling_code(s_ov_delay_ms, 8u, occ_delay_ms) << 5) | code);
    ok &= sh3510_write_verify(SH3673520_REG_OCCV_OCCT, regv, 0xFFu);
    actual_uv = ((uint32_t)code + 1u) * 1375u;
    s_protection_actual.occ_a10 = sh3510_sense_uv_to_current_a10(actual_uv);
    s_protection_actual.occ_delay_ms = s_ov_delay_ms[(regv >> 5) & 0x07u];

    if (!sh3510_high_temp_code(g_tParam.protect.u16TChgOTp_Third, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_OTC, code, 0xFFu);
    if (!sh3510_high_temp_code(g_tParam.protect.u16TdischgOTp_Third, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_OTD, code, 0xFFu);
    if (!sh3510_low_temp_code(g_tParam.protect.u16TchgUTp_Third, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_UTC, code, 0xFFu);
    if (!sh3510_low_temp_code(g_tParam.protect.u16TdischgUTp_Third, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_UTD, code, 0xFFu);

    ok &= sh3510_update_reg(SH3673520_REG_SCONF6, 0xFFu,
                            (uint8_t)(SH3673520_SCONF6_TS2_EN_MASK |
                                      SH3673520_SCONF6_TS1_EN_MASK |
                                      SH3673520_SCONF6_ALL_PROTECT_MASK));

    s_protection_actual.ov_mv = (uint16_t)(ov_code * 5u);
    s_protection_actual.uv_mv = (uint16_t)(uv_code * 5u);
    s_protection_actual.ov_delay_ms = s_ov_delay_ms[ov_dly];
    s_protection_actual.uv_delay_ms = s_uv_delay_ms[uv_dly];
    s_protection_actual.valid = ok ? 1u : 0u;
    return ok ? 1u : 0u;
}

uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual)
{
    if (actual == 0) return 0u;
    *actual = s_protection_actual;
    return s_protection_actual.valid;
}

'''
    text = regex_once(
        text,
        r"static uint8_t sh3510_delay_code\(.*?\nstatic uint8_t sh3510_configure_runtime",
        block + "static uint8_t sh3510_configure_runtime",
        "replace protection quantization layer",
    )

    text = text.replace(
        "    sh3510_gpio_output_low(D011_HEATER_CHG_PIN);\n    sh3510_gpio_output_low(D011_HEATER_RF_EN_PIN);\n    sh3510_gpio_output_low(D011_CMNT_EN_PIN);",
        "    sh3510_gpio_output_low(D011_HEATER_CHG_PIN);\n    sh3673510_board_force_heater_fuse_safe();",
    )

    text = regex_once(
        text,
        r"void sh3673510_board_set_heater\(uint8_t enabled\)\n\{.*?\n\}\n\nuint8_t sh3673510_board_wake_active",
        """void sh3673510_board_force_heater_fuse_safe(void)
{
    gpio_set_func(D011_HEATER_FUSE_TRIGGER_PIN, AS_GPIO);
    gpio_write(D011_HEATER_FUSE_TRIGGER_PIN, D011_HEATER_FUSE_SAFE_LEVEL);
    gpio_set_input_en(D011_HEATER_FUSE_TRIGGER_PIN, 0);
    gpio_set_output_en(D011_HEATER_FUSE_TRIGGER_PIN, 1);
}

void sh3673510_board_set_heater(uint8_t enabled)
{
    /* PB4 is the reversible heater command. PB5 is NOT a heater enable. */
    sh3673510_board_force_heater_fuse_safe();
    gpio_write(D011_HEATER_CHG_PIN, enabled ? 1u : 0u);
}

uint8_t sh3673510_board_wake_active""",
        "separate heater and irreversible fuse output",
    )

    text = regex_once(
        text,
        r"void sh3673510_control_sleep\(void\)\n\{.*?\n\}\n\nuint8_t sh3673510_control_wake\(void\)\n\{.*?\n\}\n",
        """void sh3673510_control_sleep(void)
{
    if (!s_control_ready) return;
    if (sh3673510_board_wake_active()) return;

    if (!sh3673510_control_set_balance(0u)) return;
    sh3673510_board_set_heater(0u);
    sh3673510_board_force_heater_fuse_safe();
    if (!sh3673510_control_set_fets(0u, 0u)) return;

    if (!sh3510_update_reg(SH3673520_REG_SCONF3,
                            SH3673520_SCONF3_CGR_WK_MASK,
                            SH3673520_SCONF3_CGR_WK_MASK)) return;
    if (SH3673520_WriteReg(SH3673520_REG_SCONF1, SH3673520_SCONF1_SLEEP) == SH3673520_OK)
        s_afe_sleeping = 1u;
}

uint8_t sh3673510_control_wake(void)
{
    if (!s_control_ready) return 0u;
    if (!s_afe_sleeping) return 1u;
    if (SH3673520_WriteReg(SH3673520_REG_SCONF1, SH3673520_SCONF1_NORMAL) != SH3673520_OK)
        return 0u;
    sh3673520_port_delay_ms(10u);
    if (!sh3510_configure_runtime()) return 0u;
    if (!sh3673510_control_apply_protection()) return 0u;
    if (!sh3673510_control_set_fets(0u, 0u)) return 0u;
    s_afe_sleeping = 0u;
    return 1u;
}
""",
        "fail-safe AFE sleep/wake state",
    )

    if "D011_HEATER_RF_EN_PIN" in text or "D011_CMNT_EN_PIN" in text:
        raise RuntimeError("AFE control still owns obsolete heater-RF or communication power net")
    write(path, text)


def patch_bms() -> None:
    path = VENDOR / "sh3673510_bms.c"
    text = read(path)
    text = text.replace(
        "#define SH3510_REINIT_COOLDOWN        25u /* 5 s at 200 ms */",
        "#define SH3510_REINIT_COOLDOWN        25u /* 5 s at 200 ms */\n#define SH3510_VALID_SNAPSHOT_RELEASE_COUNT 3u\n#define SH3510_SHORT_RELEASE_SAMPLES    10u /* 2 s stable LOADOFF at 200 ms */",
    )
    text = text.replace(
        "typedef struct {\n    uint16_t count;\n    uint8_t active;\n} sh3510_filter_t;",
        "typedef struct {\n    uint16_t trip_count;\n    uint16_t recover_count;\n    uint8_t active;\n} sh3510_filter_t;",
    )
    text = text.replace(
        "static uint16_t s_balance_mask;",
        """static uint16_t s_balance_mask;
static uint8_t s_requested_charge_on;
static uint8_t s_requested_discharge_on;
static uint8_t s_output_inhibit;
static uint8_t s_valid_snapshot_streak;
static uint8_t s_short_latched;
static uint8_t s_short_clear_pending;
static uint16_t s_short_release_count;""",
    )

    text = regex_once(
        text,
        r"static uint8_t filter_update\(sh3510_filter_t \*f,.*?\n\}\n\nstatic uint16_t ntc_temp",
        """static uint8_t filter_update(sh3510_filter_t *f, uint16_t value,
                             uint16_t trip, uint16_t recover,
                             uint16_t filter_10ms, uint8_t high)
{
    uint8_t violated;
    uint8_t recovered;
    uint16_t needed;
    if (f == 0) return 0u;
    if (trip == 0u) {
        f->active = 0u; f->trip_count = 0u; f->recover_count = 0u;
        return 0u;
    }

    needed = filter_samples(filter_10ms);
    if (f->active) {
        recovered = high ? (value <= recover) : (value >= recover);
        if (recovered) {
            if (f->recover_count < needed) ++f->recover_count;
            if (f->recover_count >= needed) {
                f->active = 0u;
                f->trip_count = 0u;
                f->recover_count = 0u;
            }
        } else {
            f->recover_count = 0u;
        }
        return f->active;
    }

    violated = high ? (value >= trip) : (value <= trip);
    if (violated) {
        if (f->trip_count < needed) ++f->trip_count;
        if (f->trip_count >= needed) {
            f->active = 1u;
            f->trip_count = 0u;
            f->recover_count = 0u;
        }
    } else if (f->trip_count != 0u) {
        --f->trip_count;
    }
    return f->active;
}

static uint16_t ntc_temp""",
        "add protected recovery debounce",
    )

    text = text.replace(
        "static void note_comm_error(void)\n{\n    sh3673520_comm_stats_t stats;",
        "static void note_comm_error(void)\n{\n    sh3673520_comm_stats_t stats;\n    s_output_inhibit = 1u;\n    s_valid_snapshot_streak = 0u;",
    )

    marker = """static uint8_t discharge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    return (f->b1CellUvp || f->b1BatUvp || f->b1IdischgOcp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp || f->b1TmosOtp ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}
"""
    replacement = """static uint8_t discharge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    return (f->b1CellUvp || f->b1BatUvp || f->b1IdischgOcp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp || f->b1TmosOtp ||
            s_short_latched ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}

static uint8_t sh3510_outputs_healthy(void)
{
    return (s_snapshot_valid && !s_output_inhibit && !s_hw_afe_error &&
            !bms_error_get(BMS_ERROR_AFE1) && !bms_error_get(BMS_ERROR_SPI)) ? 1u : 0u;
}

static uint8_t sh3510_apply_requested_fets(void)
{
    uint8_t charge_on = 0u;
    uint8_t discharge_on = 0u;
    uint8_t key_on;

    if (!sh3673510_control_ready()) return 0u;
    key_on = gpio_read(D011_SWITCH_PIN) ? 0u : 1u;

    if (s_output_enabled && sh3510_outputs_healthy()) {
        charge_on = s_requested_charge_on ? 1u : 0u;
        discharge_on = (s_requested_discharge_on && key_on) ? 1u : 0u;
        if (charge_blocked()) charge_on = 0u;
        if (discharge_blocked()) discharge_on = 0u;
    }

    if (!sh3673510_control_set_fets(charge_on, discharge_on)) {
        note_comm_error();
        return 0u;
    }
    return 1u;
}
"""
    text = replace_once(text, marker, replacement, "add FET safety arbitration")

    text = regex_once(
        text,
        r"static void publish_hw_status\(const sh3673510_control_status_t \*s\)\n\{.*?\n\}\n\nstatic void clear_recovered_flags",
        """static void publish_hw_status(const sh3673510_control_status_t *s)
{
    if (s == 0) return;
    g_bms_system_status.bits.b1Status_MOS_CHG =
        (s->bstatus1 & SH3673520_BSTATUS1_CHG_FET_MASK) ? 1u : 0u;
    g_bms_system_status.bits.b1Status_MOS_DSG =
        (s->bstatus1 & SH3673520_BSTATUS1_DSG_FET_MASK) ? 1u : 0u;
    s_hw_afe_error = (s->bstatus1 & SH3673520_BSTATUS1_E2P_ERR_MASK) ? 1u : 0u;
    if (s_hw_afe_error && !bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);

    if (s->flag1 & SH3673520_FLAG1_SC_MASK) {
        s_short_latched = 1u;
        s_short_clear_pending = 0u;
        s_short_release_count = 0u;
        if (!bms_error_get(BMS_ERROR_DSG_SHORT)) bms_error_raise(BMS_ERROR_DSG_SHORT);
        if (!bms_error_get(BMS_ERROR_CBC_DSG)) bms_error_raise(BMS_ERROR_CBC_DSG);
    }
}

static void service_short_recovery(const sh3673510_control_status_t *s)
{
    if ((s == 0) || !s_short_latched) return;

    if (s_short_clear_pending) {
        if (((s->flag1 & SH3673520_FLAG1_SC_MASK) == 0u) &&
            (s->bstatus2 & SH3673520_BSTATUS2_LOADOFF_MASK)) {
            s_short_latched = 0u;
            s_short_clear_pending = 0u;
            s_short_release_count = 0u;
            bms_error_clear(BMS_ERROR_DSG_SHORT);
            bms_error_clear(BMS_ERROR_CBC_DSG);
            return;
        }
        if (s->flag1 & SH3673520_FLAG1_SC_MASK) {
            s_short_clear_pending = 0u;
        }
    }

    if (s->bstatus2 & SH3673520_BSTATUS2_LOADOFF_MASK) {
        if (s_short_release_count < SH3510_SHORT_RELEASE_SAMPLES) ++s_short_release_count;
    } else {
        s_short_release_count = 0u;
    }

    if (!s_short_clear_pending && s_short_release_count >= SH3510_SHORT_RELEASE_SAMPLES) {
        if (sh3673510_control_clear_flag1(SH3673520_FLAG1_SC_MASK)) {
            s_short_clear_pending = 1u;
            s_short_release_count = 0u;
        }
    }
}

static void clear_recovered_flags""",
        "short-circuit load-release state machine",
    )

    text = regex_once(
        text,
        r"static void clear_recovered_flags\(const sh3673510_control_status_t \*s\)\n\{.*?\n\}\n\nstatic uint8_t publish_measurements",
        """static void clear_recovered_flags(const sh3673510_control_status_t *s)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;
    uint8_t c1 = 0u, c2 = 0u;
    if (s == 0) return;

    c1 |= (uint8_t)(s->flag1 & (SH3673520_FLAG1_RST1_MASK | SH3673520_FLAG1_WK_MASK));
    if ((s->flag1 & SH3673520_FLAG1_OV_MASK) && !f->b1CellOvp) c1 |= SH3673520_FLAG1_OV_MASK;
    if ((s->flag1 & SH3673520_FLAG1_UV_MASK) && !f->b1CellUvp) c1 |= SH3673520_FLAG1_UV_MASK;
    if ((s->flag1 & (SH3673520_FLAG1_OCD1_MASK | SH3673520_FLAG1_OCD2_MASK)) && !f->b1IdischgOcp)
        c1 |= (uint8_t)(s->flag1 & (SH3673520_FLAG1_OCD1_MASK | SH3673520_FLAG1_OCD2_MASK));
    if ((s->flag1 & SH3673520_FLAG1_OCC_MASK) && !f->b1IchgOcp) c1 |= SH3673520_FLAG1_OCC_MASK;
    /* SC is deliberately excluded. It is released only by service_short_recovery(). */

    c2 |= (uint8_t)(s->flag2 & SH3673520_FLAG2_RST2_MASK);
    if ((s->flag2 & SH3673520_FLAG2_OTC_MASK) && !f->b1CellChgOtp) c2 |= SH3673520_FLAG2_OTC_MASK;
    if ((s->flag2 & SH3673520_FLAG2_OTD_MASK) && !f->b1CellDischgOtp) c2 |= SH3673520_FLAG2_OTD_MASK;
    if ((s->flag2 & SH3673520_FLAG2_UTC_MASK) && !f->b1CellChgUtp) c2 |= SH3673520_FLAG2_UTC_MASK;
    if ((s->flag2 & SH3673520_FLAG2_UTD_MASK) && !f->b1CellDischgUtp) c2 |= SH3673520_FLAG2_UTD_MASK;
    if (s->flag2 & SH3673520_FLAG2_WDT_MASK) c2 |= SH3673520_FLAG2_WDT_MASK;

    if (c1) (void)sh3673510_control_clear_flag1(c1);
    if (c2) (void)sh3673510_control_clear_flag2(c2);
}

static uint8_t publish_measurements""",
        "AFE flag recovery follows debounced software state",
    )

    text = text.replace(
        "    publish_hw_status(&status);\n    update_faults();\n    clear_recovered_flags(&status);",
        "    publish_hw_status(&status);\n    update_faults();\n    service_short_recovery(&status);\n    clear_recovered_flags(&status);",
    )

    text = text.replace(
        "    s_hw_afe_error = 0u;\n    sh3673510_board_set_heater(0u);",
        """    s_hw_afe_error = 0u;
    s_requested_charge_on = 0u;
    s_requested_discharge_on = 0u;
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
    s_short_latched = 0u;
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    sh3673510_board_force_heater_fuse_safe();
    sh3673510_board_set_heater(0u);""",
    )

    text = regex_once(
        text,
        r"void sh3673510_bms_afe_sample\(void\)\n\{.*?\n\}\n\nuint8_t sh3673510_bms_afe_apply_protection_config",
        """void sh3673510_bms_afe_sample(void)
{
    sh3673510_board_force_heater_fuse_safe();
    if (s_reinit_cooldown) --s_reinit_cooldown;
    if (!publish_measurements()) {
        s_snapshot_valid = 0u;
        s_output_inhibit = 1u;
        s_valid_snapshot_streak = 0u;
        s_heater_on = 0u;
        sh3673510_board_set_heater(0u);
        g_bms_system_status.bits.b1Status_Heat = 0u;
        (void)sh3673510_control_set_balance(0u);
        s_balance_mask = 0u;
        (void)sh3673510_control_set_fets(0u, 0u);
        note_comm_error();
        if (s_comm_failures != 0xFFu) ++s_comm_failures;
        if (s_comm_failures >= SH3510_REINIT_TRIGGER && s_reinit_cooldown == 0u) {
            s_reinit_cooldown = SH3510_REINIT_COOLDOWN;
            if (sh3673510_control_init()) note_comm_ok();
        }
        return;
    }

    s_snapshot_valid = 1u;
    if (s_valid_snapshot_streak < SH3510_VALID_SNAPSHOT_RELEASE_COUNT) ++s_valid_snapshot_streak;
    if (s_valid_snapshot_streak >= SH3510_VALID_SNAPSHOT_RELEASE_COUNT) s_output_inhibit = 0u;
    note_comm_ok();
    apply_heater();
    apply_balance();
    (void)sh3510_apply_requested_fets();
}

uint8_t sh3673510_bms_afe_apply_protection_config""",
        "fail-safe sampling recovery",
    )

    text = regex_once(
        text,
        r"uint8_t sh3673510_bms_afe_set_fets\(uint8_t requested_charge_on,\n                                   uint8_t requested_discharge_on\)\n\{.*?\n\}\n\nvoid sh3673510_bms_afe_set_output_enabled",
        """uint8_t sh3673510_bms_afe_set_fets(uint8_t requested_charge_on,
                                   uint8_t requested_discharge_on)
{
    s_requested_charge_on = requested_charge_on ? 1u : 0u;
    s_requested_discharge_on = requested_discharge_on ? 1u : 0u;
    return sh3510_apply_requested_fets();
}

void sh3673510_bms_afe_set_output_enabled""",
        "respect FET requests",
    )

    text = regex_once(
        text,
        r"void sh3673510_bms_afe_set_output_enabled\(uint8_t enabled\)\n\{.*?\n\}\n\nuint8_t sh3673510_bms_afe_get_aux_measurements",
        """void sh3673510_bms_afe_set_output_enabled(uint8_t enabled)
{
    s_output_enabled = enabled ? 1u : 0u;
    if (!s_output_enabled) {
        s_heater_on = 0u;
        sh3673510_board_set_heater(0u);
        if (sh3673510_control_ready()) {
            (void)sh3673510_control_set_balance(0u);
            (void)sh3673510_control_set_fets(0u, 0u);
        }
    } else if (s_snapshot_valid) {
        (void)sh3510_apply_requested_fets();
    }
}

uint8_t sh3673510_bms_afe_get_aux_measurements""",
        "output enable uses safety gate",
    )

    text = text.replace(
        "void sh3673510_bms_afe_sleep(void)\n{\n    s_heater_on = 0u;",
        "void sh3673510_bms_afe_sleep(void)\n{\n    s_output_inhibit = 1u;\n    s_valid_snapshot_streak = 0u;\n    s_heater_on = 0u;\n    sh3673510_board_force_heater_fuse_safe();",
    )

    if "(void)requested_charge_on" in text or "(void)requested_discharge_on" in text:
        raise RuntimeError("FET request parameters are still discarded")
    if "u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv" in text and "FLAG1_SC_MASK" in text:
        raise RuntimeError("current-based short-circuit recovery still present")
    write(path, text)


def patch_modbus() -> None:
    path = VENDOR / "modbus_rtu.c"
    text = read(path)
    if '#include "sh3673510_control.h"' not in text:
        text = text.replace('#include "runtime.h"', '#include "runtime.h"\n#include "sh3673510_control.h"')
    text = text.replace(
        "#define BMS_REALTIME_REG_VERSION 0x0001u",
        "#define BMS_REALTIME_REG_VERSION 0x0001u\n\n#define BMS_AFE_ACTUAL_REG_BASE  0x2180u\n#define BMS_AFE_ACTUAL_REG_COUNT 11u",
    )
    text = text.replace(
        "static u16 read_realtime_status_reg(u16 reg);",
        "static u16 read_realtime_status_reg(u16 reg);\nstatic u16 read_afe_actual_reg(u16 reg);",
    )
    text = text.replace(
        "    if (reg >= BMS_REALTIME_REG_BASE &&\n        reg < (BMS_REALTIME_REG_BASE + BMS_REALTIME_REG_COUNT))\n        return read_realtime_status_reg(reg);\n\n    return 0u;",
        """    if (reg >= BMS_REALTIME_REG_BASE &&
        reg < (BMS_REALTIME_REG_BASE + BMS_REALTIME_REG_COUNT))
        return read_realtime_status_reg(reg);

    if (reg >= BMS_AFE_ACTUAL_REG_BASE &&
        reg < (BMS_AFE_ACTUAL_REG_BASE + BMS_AFE_ACTUAL_REG_COUNT))
        return read_afe_actual_reg(reg);

    return 0u;""",
    )
    text = text.replace(
        "    if (reg == BMS_EVENT_LOG_RESET_REG)",
        """    if (reg >= BMS_AFE_ACTUAL_REG_BASE &&
        reg < (BMS_AFE_ACTUAL_REG_BASE + BMS_AFE_ACTUAL_REG_COUNT))
        return MB_EX_ILLEGAL_ADDRESS;

    if (reg == BMS_EVENT_LOG_RESET_REG)""",
    )
    marker = """static u16 read_ascii_string_reg(const u8 *str, u16 max_len, u16 reg_offset)
"""
    function = """static u16 read_afe_actual_reg(u16 reg)
{
    sh3673510_protection_actual_t a;
    uint16_t offset = (uint16_t)(reg - BMS_AFE_ACTUAL_REG_BASE);
    uint8_t valid = sh3673510_control_get_protection_actual(&a);

    if (offset == 0u) return valid ? 1u : 0u;
    if (!valid) return 0xFFFFu;
    switch (offset) {
    case 1u:  return a.ov_mv;
    case 2u:  return a.uv_mv;
    case 3u:  return a.ocd1_a10;
    case 4u:  return a.ocd2_a10;
    case 5u:  return a.occ_a10;
    case 6u:  return a.ov_delay_ms;
    case 7u:  return a.uv_delay_ms;
    case 8u:  return a.ocd1_delay_ms;
    case 9u:  return a.ocd2_delay_ms;
    case 10u: return a.occ_delay_ms;
    default:  return 0xFFFFu;
    }
}

""" + marker
    if "static u16 read_afe_actual_reg(u16 reg)\n{" not in text:
        text = replace_once(text, marker, function, "add AFE actual Modbus block")
    write(path, text)


def patch_catalog() -> None:
    data = json.loads(read(CATALOG))
    blocks = data.get("register_blocks", [])
    for block in blocks:
        if block.get("id") == "protect_preview":
            block["word_count"] = 65
            block["access"] = "read_write"
            block["description_zh"] = "完整保护参数窗口：13组×5项（一级、二级、三级、恢复、滤波）。写入后固件先校验并重新量化/下发AFE，失败则回滚。"
            break
    else:
        raise RuntimeError("protect_preview block missing")

    if not any(b.get("id") == "afe_protection_actual" for b in blocks):
        blocks.append({
            "id": "afe_protection_actual",
            "category": "diagnostics",
            "start_address": "0x2180",
            "word_count": 11,
            "access": "read",
            "description_zh": "SH3673510 实际量化后的硬件保护值。0=valid，1/2=OV/UV(mV)，3/4/5=OCD1/OCD2/OCC(0.1A)，6~10=对应延时(ms)。不可写。",
        })
    write(CATALOG, json.dumps(data, ensure_ascii=False, indent=2) + "\n")


def patch_test() -> None:
    text = read(TEST)
    text = text.replace(
        '"D011_HEATER_RF_EN_PIN                   GPIO_PB5",',
        '"D011_HEATER_FUSE_TRIGGER_PIN              GPIO_PB5",',
    )
    text = text.replace(
        'require(bms, "sh3673510_board_set_heater")',
        'require(bms, "sh3673510_board_set_heater")\nrequire(bms, "SH3510_SHORT_RELEASE_SAMPLES")\nrequire(bms, "SH3673520_BSTATUS2_LOADOFF_MASK")\nrequire(bms, "s_output_inhibit")\nrequire(bms, "s_requested_charge_on")',
    )
    text = text.replace(
        'require(uart, "RS485_EN_PIN")',
        'require(uart, "D011_RS485_EN_PIN")',
    )
    extra = r'''

# D011 safety invariants: old board aliases and accidental irreversible-fuse
# actuation must not re-enter production code.
production = "\n".join(text(name) for name in (
    "conf.h", "app.c", "modbus_uart.c", "sh3673510_control.c",
    "sh3673510_bms.c", "sh3673510_project_config.h",
))
for forbidden in ("CHG_IN_PIN", "RF_EN_PIN", "AFE1_PRO_EN_PIN", "MCU_LDO_PIN",
                  "D011_HEATER_RF_EN_PIN"):
    if re.search(rf"\b{re.escape(forbidden)}\b", production):
        raise AssertionError(f"obsolete/unsafe D011 alias remains: {forbidden}")

require(control, "sh3673510_board_force_heater_fuse_safe")
require(control, "D011_HEATER_FUSE_SAFE_LEVEL")
if re.search(r"gpio_write\s*\(\s*D011_HEATER_FUSE_TRIGGER_PIN\s*,\s*1", production):
    raise AssertionError("PB5 heater-fuse trigger must never be driven high before fuse logic is validated")
if "(void)requested_charge_on" in bms or "(void)requested_discharge_on" in bms:
    raise AssertionError("AFE FET API must honor caller requests")
if "FLAG1_SC_MASK" in bms and "u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv" in bms:
    raise AssertionError("short-circuit recovery must not use current-to-zero as load-release proof")
require(bms, "service_short_recovery")
require(bms, "SH3510_VALID_SNAPSHOT_RELEASE_COUNT")
require(control, "sh3673510_control_get_protection_actual")
'''
    marker = 'print("HS-D011 SH3673510 integration contract: PASS")'
    if "D011 safety invariants" not in text:
        text = replace_once(text, marker, extra + "\n" + marker, "extend D011 contract checks")
    write(TEST, text)


def patch_docs() -> None:
    hw = read(DOC_HW)
    hw = hw.replace(
        "| 15 | PB5 | HT-RF-EN | 输出至 R75/Q11，加热/保护功率网络 | D011_HEATER_RF_EN_PIN |",
        "| 15 | PB5 | HT-RF-EN | **加热回路保险丝熔断触发输出**；正常/故障诊断阶段必须保持低，只有经验证的不可逆熔断状态机才允许拉高 | D011_HEATER_FUSE_TRIGGER_PIN |",
    )
    hw = hw.replace(
        "- TODO_VERIFY_HW：PB4/PB5 四种组合的真实功能、QD7 栅极电压、F1 三端器件用途、加热和熔断/保护动作之间的关系。两个 IO 同时拉高并非图纸自动证明的安全加热序列。",
        "- 产品语义确认：PB4/HT-CHG 是可逆加热控制；PB5/HT-RF-EN 是加热回路保险丝熔断触发。当前固件对 PB5 只允许输出低电平；在加热 MOS 温度通道、失控判据、动作持续时间、F1 动作特性及不可逆锁存流程全部验证前，禁止任何自动拉高路径。",
    )
    if "D011_HEATER_RF_EN_PIN" in hw:
        hw = hw.replace("D011_HEATER_RF_EN_PIN", "D011_HEATER_FUSE_TRIGGER_PIN")
    write(DOC_HW, hw)

    status = read(DOC_STATUS)
    status = status.replace(
        "| D011-003 | P1 | conf.h 的 RF_EN_PIN 映射 PB5/HT-RF-EN；app.c 旧 _UL_RENZHENG_ENABLE_ 路径与 D011 加热函数都会写该脚 | 根据 D011 F1/加热网络梳理唯一写入所有者，消除旧保护路径与加热路径冲突，并测试四种 PB4/PB5 组合 |",
        "| D011-003 | P0→已修复(待实板) | PB5/HT-RF-EN 已确认是加热回路保险丝熔断触发；旧 RF_EN 路径和普通 heater 同写会造成不可逆误熔断风险 | 已删除旧 RF_EN/CHG_IN 等板级别名；普通 heater 只控制 PB4；PB5 运行路径只强制 LOW。真正熔断状态机在硬件判据完整验证前保持禁用 |",
    )
    status = status.replace(
        "| D011-001 | P1 | sh3673510_bms_afe_set_fets() 将两个 requested 参数转 void，实际仅按输出许可/按键重算。app.c::mos_update() 的关断/方向请求因此可能不被执行 | 明确 MOS 状态所有者；请求与保护许可相与；覆盖显式关断、仅充/仅放、保护禁止、重复调用 |",
        "| D011-001 | P1→已修复(待实板) | FET 请求曾被丢弃 | 已改为“上层请求 × 输出许可 × 有效快照 × AFE/通信健康 × 方向保护”的统一仲裁；显式关断不再被底层重算覆盖 |",
    )
    status = status.replace(
        "| D011-002 | P1 | MOS 许可函数未直接检查 s_snapshot_valid、AFE/SPI 错误；采样失败虽清快照/关加热，但该函数仍可被其他调用链请求输出 | 陈旧/无效采样、CRC/超时/初始化失败均不得允许重新开启；建立故障恢复后有效快照门槛 |",
        "| D011-002 | P1→已修复(待实板) | 旧代码通信失败后仍可能被其他调用链重新开 MOS | 已增加 output inhibit；SPI/CRC/采样失败立即关 FET，重新初始化后需连续 3 个完整有效快照才解除 inhibit |",
    )
    status = status.replace(
        "| D011-008 | P1 | 当前 clear_recovered_flags() 可按电流降至恢复阈值请求清 OCD/SC 锁存；关断后电流本就可能为零 | 按 SH3673510 手册与产品流程确认故障移除条件，禁止形成持续短路下反复开 MOS 的循环 |",
        "| D011-008 | P1→代码修复/待实板 | 旧代码按关 MOS 后的零电流清 SC，会形成持续短路下反复重开风险 | SC 改为独立锁存；必须观察 AFE LOADOFF 连续约 2 s 才尝试清 SC，并在后续读回确认 SC 已消失后才解除放电禁止 |",
    )
    write(DOC_STATUS, status)


def verify_no_legacy_gpio_aliases() -> None:
    forbidden = ("CHG_IN_PIN", "RF_EN_PIN", "AFE1_PRO_EN_PIN", "MCU_LDO_PIN")
    hits = []
    for path in VENDOR.rglob("*"):
        if path.suffix not in {".c", ".h"}:
            continue
        text = read(path)
        for token in forbidden:
            if re.search(rf"\b{re.escape(token)}\b", text):
                hits.append(f"{path.relative_to(ROOT)}:{token}")
    if hits:
        raise RuntimeError("legacy D011 GPIO aliases remain:\n" + "\n".join(hits))


def main() -> None:
    patch_project_config()
    patch_conf_and_safe_aliases()
    patch_app()
    patch_control_header()
    patch_control()
    patch_bms()
    patch_modbus()
    patch_catalog()
    patch_test()
    patch_docs()
    verify_no_legacy_gpio_aliases()
    print("D011 safety refactor applied")


if __name__ == "__main__":
    main()
