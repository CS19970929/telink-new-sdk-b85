#!/usr/bin/env python3
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"

cfg = VENDOR / "sh3673510_project_config.h"
text = cfg.read_text(encoding="utf-8")
text = text.replace("D011_HEATER_RF_EN_PIN", "D011_HEATER_FUSE_TRIGGER_PIN")
needle = "#define D011_HEATER_CHG_PIN                     GPIO_PB4\n"
if "D011_HEATER_FUSE_SAFE_LEVEL" not in text:
    if needle not in text:
        raise RuntimeError("D011 heater GPIO anchor not found")
    text = text.replace(needle, needle + "#define D011_HEATER_FUSE_SAFE_LEVEL               0u\n", 1)
line = "#define D011_HEATER_FUSE_TRIGGER_PIN              GPIO_PB5"
if line in text and "heater-circuit fuse trigger" not in text:
    text = text.replace(line, line + "  /* heater-circuit fuse trigger; keep LOW until a validated irreversible fuse state machine authorizes firing. */", 1)
cfg.write_text(text, encoding="utf-8", newline="\n")

# Remove the D011 compatibility-alias block explicitly. These aliases encode
# old-board semantics that do not exist on the D011 schematic.
conf = VENDOR / "conf.h"
text = conf.read_text(encoding="utf-8")
start = text.find("/*\n * HS-D011 physical MCU nets.")
end_marker = "#define MCU_LDO_PIN            D011_CMNT_EN_PIN\n"
if start >= 0:
    end = text.find(end_marker, start)
    if end < 0:
        raise RuntimeError("legacy D011 alias block end not found")
    end += len(end_marker)
    text = text[:start] + """/* D011 board code uses only canonical D011_* schematic nets from
 * sh3673510_project_config.h. Legacy cross-board GPIO aliases are forbidden. */
""" + text[end:]
conf.write_text(text, encoding="utf-8", newline="\n")

app = VENDOR / "app.c"
text = app.read_text(encoding="utf-8")
# The old CHG_IN alias is physically PA0/DI1, not a charger-detect net.
text = text.replace("CHG_IN_PIN", "D011_SWITCH_PIN")

# Remove the last behavioral dependency on the old charger/key abstractions.
# For sleep eligibility use only schematic-backed facts: the front switch must
# be off and the active-high PB1 external wake input must not be asserted.
old = """#ifdef _DI_SWITCH_SYS_ONOFF
		if (!IsChargerWakeupActive())
		{
			if (!IsKeyWakeupActive())
			{
				sleep_cnt = (u16)(sleep_cnt + sleep_elapsed_sec);
				if (sleep_cnt >= 3u)
				{
					sleep_cnt = 0;
					cpu_set_gpio_wakeup(SW_PIN, Level_Low, 1);
					app_note_sleep_and_enter_deepsleep(1u); // deepsleep
				}
			}
			else
			{
				sleep_cnt = 0;
			}
		}
		else
		{
			sleep_cnt = 0;
		}
#endif
"""
new = """#ifdef _DI_SWITCH_SYS_ONOFF
		if (!d011_switch_is_on() && !gpio_read(D011_INT_WK_MCU_PIN))
		{
			sleep_cnt = (u16)(sleep_cnt + sleep_elapsed_sec);
			if (sleep_cnt >= 3u)
			{
				sleep_cnt = 0;
				cpu_set_gpio_wakeup(D011_SWITCH_PIN, Level_Low, 1);
				app_note_sleep_and_enter_deepsleep(1u); // deepsleep
			}
		}
		else
		{
			sleep_cnt = 0;
		}
#endif
"""
if old in text:
    text = text.replace(old, new, 1)
app.write_text(text, encoding="utf-8", newline="\n")

# The inactive legacy DVC backend must not depend on removed board GPIO aliases
# either. Current latches may recover from measured current; short-circuit stays
# latched instead of being cleared from an ambiguous GPIO/current-to-zero test.
dvc = VENDOR / "dvc1124_bms.c"
text = dvc.read_text(encoding="utf-8")
old = """    /* Require removal of the source before clearing current/short latches. */
    if (gpio_read(CHG_IN_PIN))
        clear_mask |= (uint8_t)(alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK));

    if (gpio_read(SW_PIN))
    {
        clear_mask |= (uint8_t)(alarm & (DVC1124_ALARM_OCD1_MASK |
                                         DVC1124_ALARM_OCD2_MASK |
                                         DVC1124_ALARM_SCD_MASK));
    }
"""
new = """    /* Do not inherit board-specific charger/switch GPIO assumptions here.
     * Recover current latches only after measured current is below the configured
     * recovery threshold. Keep SCD latched until that backend gets its own
     * hardware-verified load-release policy. */
    if (g_stCellInfoReport.u16Ichg <= g_tParam.protect.u16IchgOcp_Rcv)
        clear_mask |= (uint8_t)(alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK));

    if (g_stCellInfoReport.u16IDischg <= g_tParam.protect.u16IdsgOcp_Rcv)
        clear_mask |= (uint8_t)(alarm & (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK));
"""
if old in text:
    text = text.replace(old, new, 1)
dvc.write_text(text, encoding="utf-8", newline="\n")

# The main one-shot script originally expected to own alias-block deletion.
# Because this pre-pass now removes that block first, make the main step
# idempotent instead of weakening its final zero-legacy-token verification.
main_tool = ROOT / "tools" / "apply_d011_safety_refactor.py"
text = main_tool.read_text(encoding="utf-8")
old = '    text = regex_once(text, pattern, replacement, "remove legacy D011 GPIO aliases")\n    write(conf, text)'
new = '''    if any(token in text for token in ("CHG_IN_PIN", "RF_EN_PIN", "AFE1_PRO_EN_PIN", "MCU_LDO_PIN")):
        text = regex_once(text, pattern, replacement, "remove legacy D011 GPIO aliases")
    write(conf, text)'''
if old in text:
    text = text.replace(old, new, 1)
main_tool.write_text(text, encoding="utf-8", newline="\n")

print("D011 fuse net, legacy aliases, wake calls and inactive DVC GPIO dependencies normalized")
