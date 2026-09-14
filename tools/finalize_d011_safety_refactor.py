#!/usr/bin/env python3
"""Finalize the D011 safety refactor without reapplying the migration."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
TEST = ROOT / "tests" / "sh3673510_d011_integration_check.py"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="\n")


def keep_one(text: str, needle: str, label: str) -> str:
    count = text.count(needle)
    if count == 0:
        raise RuntimeError(f"{label}: required text missing")
    if count == 1:
        return text
    first = text.find(needle)
    return text[: first + len(needle)] + text[first + len(needle):].replace(needle, "")


def main() -> None:
    control = VENDOR / "sh3673510_control.c"
    text = read(control)
    text = keep_one(
        text,
        "static sh3673510_protection_actual_t s_protection_actual;\n",
        "protection actual storage",
    )
    fuse_fn = """void sh3673510_board_force_heater_fuse_safe(void)
{
    gpio_set_func(D011_HEATER_FUSE_TRIGGER_PIN, AS_GPIO);
    gpio_write(D011_HEATER_FUSE_TRIGGER_PIN, D011_HEATER_FUSE_SAFE_LEVEL);
    gpio_set_input_en(D011_HEATER_FUSE_TRIGGER_PIN, 0);
    gpio_set_output_en(D011_HEATER_FUSE_TRIGGER_PIN, 1);
}

"""
    text = keep_one(text, fuse_fn, "heater fuse safe function")
    write(control, text)

    control_h = VENDOR / "sh3673510_control.h"
    text = read(control_h)
    text = keep_one(
        text,
        "uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual);\n",
        "protection actual getter prototype",
    )
    text = keep_one(
        text,
        "void sh3673510_board_force_heater_fuse_safe(void);\n",
        "heater fuse safe prototype",
    )
    write(control_h, text)

    bms = VENDOR / "sh3673510_bms.c"
    text = read(bms)
    text = keep_one(text, "#define SH3510_VALID_SNAPSHOT_RELEASE_COUNT 3u\n", "snapshot release macro")
    text = keep_one(text, "#define SH3510_SHORT_RELEASE_SAMPLES    10u /* 2 s stable LOADOFF at 200 ms */\n", "short release macro")
    for decl in (
        "static uint8_t s_requested_charge_on;\n",
        "static uint8_t s_requested_discharge_on;\n",
        "static uint8_t s_output_inhibit;\n",
        "static uint8_t s_valid_snapshot_streak;\n",
        "static uint8_t s_short_latched;\n",
        "static uint8_t s_short_clear_pending;\n",
        "static uint16_t s_short_release_count;\n",
    ):
        text = keep_one(text, decl, decl.strip())

    # Reinitialization must preserve a short-circuit latch. Static/BSS startup
    # already initializes it to zero; clearing it inside bms_afe_init() would
    # allow an AFE reset after a communication fault to forget a real short.
    old_init = """    s_requested_charge_on = 0u;
    s_requested_discharge_on = 0u;
    s_short_latched = 0u;
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
"""
    new_init = """    s_requested_charge_on = 0u;
    s_requested_discharge_on = 0u;
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
    /* Preserve s_short_latched across AFE communication reinitialization. */
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
"""
    if old_init in text:
        text = text.replace(old_init, new_init, 1)
    elif new_init not in text:
        raise RuntimeError("AFE init safety-state anchor missing")

    old_sleep = """void sh3673510_bms_afe_sleep(void)
{
    s_heater_on = 0u;
"""
    new_sleep = """void sh3673510_bms_afe_sleep(void)
{
    s_output_inhibit = 1u;
    s_valid_snapshot_streak = 0u;
    s_heater_on = 0u;
"""
    if old_sleep in text:
        text = text.replace(old_sleep, new_sleep, 1)
    elif new_sleep not in text:
        raise RuntimeError("AFE sleep safety-state anchor missing")
    write(bms, text)

    modbus = VENDOR / "modbus_rtu.c"
    text = read(modbus)
    for item, label in (
        ("#define BMS_AFE_ACTUAL_REG_BASE  0x2180u\n", "AFE actual base"),
        ("#define BMS_AFE_ACTUAL_REG_COUNT 11u\n", "AFE actual count"),
        ("static u16 read_afe_actual_reg(u16 reg);\n", "AFE actual prototype"),
    ):
        text = keep_one(text, item, label)
    guard = """    if (reg >= BMS_AFE_ACTUAL_REG_BASE &&
        reg < (BMS_AFE_ACTUAL_REG_BASE + BMS_AFE_ACTUAL_REG_COUNT))
        return MB_EX_ILLEGAL_ADDRESS;

"""
    text = keep_one(text, guard, "AFE actual write guard")
    write(modbus, text)

    test = read(TEST)
    for item in (
        'require(bms, "SH3510_SHORT_RELEASE_SAMPLES")\n',
        'require(bms, "SH3673520_BSTATUS2_LOADOFF_MASK")\n',
        'require(bms, "s_output_inhibit")\n',
        'require(bms, "s_requested_charge_on")\n',
    ):
        test = keep_one(test, item, item.strip())

    if "Migration idempotency / compilation-safety guards" not in test:
        marker = 'require(control, "sh3673510_control_get_protection_actual")\n'
        checks = r'''

# Migration idempotency / compilation-safety guards.
def require_count(src: str, needle: str, expected: int = 1) -> None:
    actual = src.count(needle)
    if actual != expected:
        raise AssertionError(f"expected {expected} occurrence(s) of {needle!r}, got {actual}")

control_h = text("sh3673510_control.h")
modbus = text("modbus_rtu.c")
require_count(control, "static sh3673510_protection_actual_t s_protection_actual;")
require_count(control_h, "sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual);")
require_count(control_h, "sh3673510_board_force_heater_fuse_safe(void);")
require_count(bms, "#define SH3510_VALID_SNAPSHOT_RELEASE_COUNT 3u")
require_count(bms, "#define SH3510_SHORT_RELEASE_SAMPLES    10u")
for symbol in (
    "static uint8_t s_requested_charge_on;",
    "static uint8_t s_requested_discharge_on;",
    "static uint8_t s_output_inhibit;",
    "static uint8_t s_valid_snapshot_streak;",
    "static uint8_t s_short_latched;",
    "static uint8_t s_short_clear_pending;",
    "static uint16_t s_short_release_count;",
):
    require_count(bms, symbol)
require_count(modbus, "#define BMS_AFE_ACTUAL_REG_BASE  0x2180u")
require_count(modbus, "#define BMS_AFE_ACTUAL_REG_COUNT 11u")
require_count(modbus, "static u16 read_afe_actual_reg(u16 reg);")
'''
        if marker not in test:
            raise RuntimeError("integration test insertion marker missing")
        test = test.replace(marker, marker + checks, 1)

    extra = r'''

# Function-level safety invariants that text-level dedupe must not destroy.
require_count(control, "void sh3673510_board_force_heater_fuse_safe(void)\n{")
require_count(control, "uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual)\n{")
require(bms, "s_output_inhibit = 1u;\n    s_valid_snapshot_streak = 0u;\n    /* Preserve s_short_latched across AFE communication reinitialization. */")
require(bms, "void sh3673510_bms_afe_sleep(void)\n{\n    s_output_inhibit = 1u;\n    s_valid_snapshot_streak = 0u;")
require_count(bms, "s_short_latched = 0u;", 1)
'''
    if "Function-level safety invariants" not in test:
        marker = 'require_count(modbus, "static u16 read_afe_actual_reg(u16 reg);")\n'
        if marker not in test:
            raise RuntimeError("test guard append marker missing")
        test = test.replace(marker, marker + extra, 1)
    write(TEST, test)

    print("D011 final source deduped with fail-safe state preserved")


if __name__ == "__main__":
    main()
