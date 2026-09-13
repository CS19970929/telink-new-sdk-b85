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

# D011 has no validated dedicated legacy CHG_IN net.  The old alias was PA0,
# which is the physical active-low switch.  Normalize any residual app-level
# reference to its real schematic net before the main refactor removes the
# obsolete charger abstraction and rebuilds the wake/output policy.
app = VENDOR / "app.c"
text = app.read_text(encoding="utf-8")
text = text.replace("CHG_IN_PIN", "D011_SWITCH_PIN")
app.write_text(text, encoding="utf-8", newline="\n")

print("D011 fuse net and legacy charger alias normalized")
