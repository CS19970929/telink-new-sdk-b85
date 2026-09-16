#!/usr/bin/env python3
"""Keep D008 AFE product documentation aligned with compile-time policy."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"

product = (DOCS / "D008_PRODUCT_REFERENCE.md").read_text(encoding="utf-8")
afe = (DOCS / "AFE_HARDWARE_PROTECTION_V2.md").read_text(encoding="utf-8")
guide = (DOCS / "CONFIGURATION_AND_BUILD_GUIDE.md").read_text(encoding="utf-8")
readme = (ROOT / "README.md").read_text(encoding="utf-8")
agents = (ROOT / "AGENTS.md").read_text(encoding="utf-8")

for token in (
    "GP1 | heater NTC",
    "GP2 | battery NTC #1",
    "GP3 | battery NTC #2",
    "GP4 | power MOS NTC",
    "GP5 | CHG low-side",
    "GP6 | DSG low-side",
    "Rsense | RS1..RS10 = 10 × 2 mΩ",
    "I2C watchdog | **4 s**",
    "Body diode threshold | **80 µV**",
    "每次 AFE reset/init 后重新下发",
):
    if token not in product:
        raise AssertionError(f"D008 product documentation drift: {token}")

truth = "feature/windows-afe-hw-protection-editor-v2"
for name, text in (("AGENTS.md", agents), ("README", readme), ("AFE hardware doc", afe), ("build guide", guide)):
    if truth not in text or "bms-tool-windows/" not in text:
        raise AssertionError(f"Windows source-of-truth missing from {name}")

for name, text in (("README", readme), ("AFE hardware doc", afe), ("build guide", guide)):
    if "tools/BMSAssistantQt" in text:
        raise AssertionError(f"retired Qt client still presented as usable in {name}")

print("D008 documentation contract: PASS")
