#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOC_C = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample" / "SocEnhance.c"
SOC_TEST = ROOT / "tests" / "soc_contract_check.py"
SOC_DOC = ROOT / "docs" / "SOC.md"

text = SOC_C.read_text(encoding="utf-8")
old_voltage = "uint8_t voltage_ready = (VCELLMAX >= full_mv) && (VCELLMIN >= full_min) && !isDSG();"
new_voltage = "uint8_t voltage_ready = (VCELLMAX >= full_mv) && (VCELLMIN >= full_min) && isCHG();"
if old_voltage in text:
    text = text.replace(old_voltage, new_voltage, 1)
elif new_voltage not in text:
    raise SystemExit("unexpected full-anchor voltage_ready expression")

old_ovp = "if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp) {"
new_ovp = "if (isCHG() && g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp) {"
if old_ovp in text:
    text = text.replace(old_ovp, new_ovp, 1)
elif new_ovp not in text:
    raise SystemExit("unexpected full-anchor OVP expression")

marker = "static uint8_t soc_apply_full_anchor(void)\n{"
if marker not in text:
    raise SystemExit("full-anchor function not found")
comment = "static uint8_t soc_apply_full_anchor(void)\n{\n    /* Upward calibration is legal only while a real charging direction is\n     * confirmed. Idle/high-voltage boot states and rebound must never raise SOC. */"
if "Upward calibration is legal only while a real charging direction" not in text:
    text = text.replace(marker, comment, 1)
SOC_C.write_text(text, encoding="utf-8")

test = SOC_TEST.read_text(encoding="utf-8")
needle = "    def test_soc_low_faults_are_implemented_without_mos_policy(self):\n"
contract = '''    def test_upward_calibration_requires_confirmed_charging_full_anchor(self):\n        start = C.index("static uint8_t soc_apply_full_anchor(void)")\n        end = C.index("static uint8_t soc_apply_forced_empty_anchor(void)", start)\n        full_fn = C[start:end]\n        self.assertIn("(VCELLMAX >= full_mv) && (VCELLMIN >= full_min) && isCHG()", full_fn)\n        self.assertIn("if (isCHG() && g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp)", full_fn)\n        self.assertNotIn("&& !isDSG()", full_fn)\n        self.assertNotIn("soc_step_up_to", C[C.index("static uint8_t soc_idle_ocv_tracking"):C.index("static uint16_t soc_discharge_natural_1pct_ticks")])\n\n'''
if "def test_upward_calibration_requires_confirmed_charging_full_anchor" not in test:
    if needle not in test:
        raise SystemExit("SOC contract insertion point not found")
    test = test.replace(needle, contract + needle, 1)
SOC_TEST.write_text(test, encoding="utf-8")

doc = SOC_DOC.read_text(encoding="utf-8")
doc = doc.replace("- 三级单体 OVP 已触发时，estimate 强锚定 100%；或\n- 根据 chemistry profile 的满电电压条件稳定 60 s，之后约 2 s/1% 向 100% 收敛。",
                  "- **仅在确认充电方向时**，三级单体 OVP 已触发才允许 estimate 强锚定 100%；或\n- **仅在确认充电方向时**，满足 chemistry profile 的满电电压条件稳定 60 s，之后约 2 s/1% 向 100% 收敛。\n- 静置、高电压回弹、开机高电压、非充电状态即使电压达到满电区，也不得向上校准。")
SOC_DOC.write_text(doc, encoding="utf-8")
