#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample" / "sh3673510_project_config.h"
text = path.read_text(encoding="utf-8")
text = text.replace("D011_HEATER_RF_EN_PIN", "D011_HEATER_FUSE_TRIGGER_PIN")
needle = "#define D011_HEATER_CHG_PIN                     GPIO_PB4\n"
if "D011_HEATER_FUSE_SAFE_LEVEL" not in text:
    if needle not in text:
        raise RuntimeError("D011 heater GPIO anchor not found")
    text = text.replace(needle, needle + "#define D011_HEATER_FUSE_SAFE_LEVEL               0u\n", 1)
line = "#define D011_HEATER_FUSE_TRIGGER_PIN              GPIO_PB5"
if line in text and "heater-circuit fuse trigger" not in text:
    text = text.replace(line, line + "  /* heater-circuit fuse trigger; keep LOW until a validated irreversible fuse state machine authorizes firing. */", 1)
path.write_text(text, encoding="utf-8", newline="\n")
print("D011 heater fuse net normalized")
