"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_sh3673510_protection_mode_check():
    print("CHECK sh3673510_protection_mode_check", flush=True)
    #!/usr/bin/env python3
    """Static contract checks for independent SH3673510 SW/HW protection modes."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source

    ROOT = Path(__file__).resolve().parents[1]
    HERE = Sources(ROOT)


    def text(name: str) -> str:
        return (HERE / name).read_text(encoding="utf-8", errors="ignore")


    def require(src: str, needle: str) -> None:
        if needle not in src:
            raise AssertionError(f"missing protection-mode invariant: {needle}")


    cfg = text("sh3673510_project_config.h") + text("bms_product.h") + (ROOT / "bms/products/sh3673510_defaults.h").read_text(encoding="utf8")
    bms = text("sh3673510_bms.c")

    for needle in (
        "#define SH3673510_SW_PROTECT_ENABLE             1u",
        "#define SH3673510_HW_PROTECT_ENABLE             1u",
        "#define SH3673510_BOARD_PD_EN                      SH3673510_HW_PROTECT_ENABLE",
        "#define SH3673510_BOARD_MOS_EN                     SH3673510_HW_PROTECT_ENABLE",
        "#define SH3673510_BOARD_OCC_EN                     SH3673510_HW_PROTECT_ENABLE",
        "#define SH3673510_BOARD_WDT_EN                     SH3673510_HW_PROTECT_ENABLE",
        "#define SH3673510_BOARD_TS2_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE",
        "#define SH3673510_BOARD_TS1_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE",
        "#define SH3673510_BOARD_SC_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE",
        "#define SH3673510_BOARD_OCD_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE",
        "#define SH3673510_BOARD_UV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE",
        "#define SH3673510_BOARD_OV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE",
        "#define SH3673510_BOARD_CADC_EN                    1u",
    ):
        require(cfg, needle)

    for needle in (
        "#if SH3673510_SW_PROTECT_ENABLE",
        "bms_sw_protection_clear();",
        "#if SH3673510_HW_PROTECT_ENABLE",
        "merge_hw_protection_faults(&status);",
        "if (!service_short_recovery(&status)) return 0u;",
        "if (!s_sample_pending && !service_hw_flag_recovery(&status)) return 0u;",
        "s_hw_charge_protect = 0u;",
        "s_hw_discharge_protect = 0u;",
    ):
        require(bms, needle)

    # The HW-only test must not be masked by a backend-owned heater state machine.
    if "static void apply_heater" in bms or "s_heater_mos_overtemp" in bms:
        raise AssertionError("SH backend must not own heater policy")

    print("SH3673510 protection mode contract: PASS")

def check_sh3673510_temperature_encoding_check():
    print("CHECK sh3673510_temperature_encoding_check", flush=True)
    #!/usr/bin/env python3
    """Regression checks for SH3673510 hardware temperature threshold encoding."""
    from pathlib import Path
    from project_paths import Sources

    ROOT = Path(__file__).resolve().parents[1]
    CONTROL = (Sources(ROOT) / "sh3673510_control.c").read_text(encoding="utf8")


    CORRECT_HIGH_FORMULA = (
        "result = ((uint32_t)r100 * 512u + (denominator / 2u)) / denominator;"
    )
    OBSOLETE_HIGH_FORMULA = "700L - (3L * (int32_t)r100)"

    if CORRECT_HIGH_FORMULA not in CONTROL:
        raise AssertionError("SH3673510 high-temperature divider formula is missing")
    if OBSOLETE_HIGH_FORMULA in CONTROL:
        raise AssertionError("obsolete SH3673510 high-temperature formula reintroduced")


    def high_code(r100: int) -> int:
        """Rntc/(10K+Rntc)*512, with resistance in 100-ohm units."""
        denominator = r100 + 100
        return (r100 * 512 + denominator // 2) // denominator


    # Existing 10K NTC table points used by the firmware:
    # 55 C -> 3.5 kOhm, 60 C -> 3.0 kOhm.
    assert high_code(35) == 0x85
    assert high_code(30) == 0x76

    print("SH3673510 temperature encoding contract: PASS")

if __name__ == "__main__":
    check_sh3673510_protection_mode_check()
    check_sh3673510_temperature_encoding_check()
