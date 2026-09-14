#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
TEST = ROOT / "tests" / "sh3673510_d011_integration_check.py"
text = TEST.read_text(encoding="utf-8")

short_block = '''# Short-circuit recovery must remain a distinct LOADOFF-qualified path even
# though normal OCD1/OCD2 FLAG recovery legitimately uses the OCP recovery current.
short_start = bms.find("static void service_short_recovery")
short_end = bms.find("static uint8_t hw_recovery_stable", short_start)
if short_start < 0 or short_end <= short_start:
    raise AssertionError("missing service_short_recovery")
short_text = bms[short_start:short_end]
if "u16IDischg" in short_text:
    raise AssertionError("short-circuit recovery must not use discharge current as load-release proof")
require(bms, "service_short_recovery")
'''
text, n = re.subn(
    r'# Short-circuit recovery must remain a distinct LOADOFF-qualified path even.*?require\(bms, "service_short_recovery"\)\n',
    short_block,
    text,
    count=1,
    flags=re.S,
)
if n != 1:
    raise RuntimeError("failed to repair short-recovery contract block")

extra_block = '''

# Hardware FLAG recovery must be based on physical recovery windows and the
# actual quantized AFE threshold, not software Third-level activity.
hw_start = bms.find("static void service_hw_flag_recovery")
hw_end = bms.find("static uint8_t service_afe_reconfiguration", hw_start)
if hw_start < 0 or hw_end <= hw_start:
    raise AssertionError("missing hardware FLAG recovery state machine")
hw_text = bms[hw_start:hw_end]
for needle in (
    "sh3673510_control_get_protection_actual",
    "u16VCellMax <= g_tParam.protect.u16VcellOvp_Rcv",
    "u16VCellMax < actual.ov_mv",
    "u16VCellMin >= g_tParam.protect.u16VcellUvp_Rcv",
    "u16VCellMin > actual.uv_mv",
    "u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv",
    "u16IDischg < actual.ocd1_a10",
    "u16IDischg < actual.ocd2_a10",
    "u16Ichg <= g_tParam.protect.u16IchgOcp_Rcv",
    "u16Ichg < actual.occ_a10",
    "bat_max <= g_tParam.protect.u16TChgOTp_Rcv",
    "bat_min >= g_tParam.protect.u16TchgUTp_Rcv",
):
    require(hw_text, needle)
if "unMdlFault_Third" in hw_text:
    raise AssertionError("hardware FLAG recovery must be independent from software Third-level activity")

heater_start = bms.find("static void apply_heater")
heater_end = bms.find("static void apply_balance", heater_start)
if heater_start < 0 or heater_end <= heater_start:
    raise AssertionError("missing heater control")
heater_text = bms[heater_start:heater_end]
for needle in (
    "SH3673510_D011_HEATER_NTC_INDEX",
    "u16TmosOTp_Third",
    "u16TmosOTp_Rcv",
    "BMS_ERROR_HEAT",
):
    require(heater_text, needle)
if "D011_HEATER_FUSE_TRIGGER_PIN" in heater_text:
    raise AssertionError("reversible heater safety must never actuate the irreversible fuse trigger")
'''
text, n = re.subn(
    r'\n\n# Hardware FLAG recovery must be based on physical recovery windows.*?(?=\nprint\("HS-D011 SH3673510 integration contract: PASS"\))',
    extra_block,
    text,
    count=1,
    flags=re.S,
)
if n != 1:
    raise RuntimeError("failed to repair hardware/heater contract block")

TEST.write_text(text, encoding="utf-8")
print("Repaired D011 protection contract generation")
