#!/usr/bin/env python3
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

print("D011 fuse net, legacy charger alias and wake calls normalized")
