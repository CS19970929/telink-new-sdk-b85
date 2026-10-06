#!/usr/bin/env python3
"""Execute D014's actual product defaults and validator; no copied board macros."""
from project_paths import host_includes
from validation_support import ROOT, profile_prefix, run_c

code = profile_prefix('d014') + """
int main(void)
{
    bms_afe_hw_profile_t p;
    bms_afe_hw_profile_build_default(&p);
    if (p.ocd1_a10 != SH3673510_HW_DEFAULT_OCD1_A10 ||
        p.ocd_recover_a10 != SH3673510_HW_DEFAULT_OCD_RECOVER_A10 ||
        p.occ1_a10 != SH3673510_HW_DEFAULT_OCC1_A10 ||
        p.occ_recover_a10 != SH3673510_HW_DEFAULT_OCC_RECOVER_A10 ||
        !bms_afe_hw_profile_validate(&p)) return 1;
    p.ocd_recover_a10 = sh3673510_quantize_current_a10(p.ocd1_a10, SH3673510_BOARD_SHUNT_UOHM, 5000u, 15u, 0);
    if (bms_afe_hw_profile_validate(&p)) return 2;
    p.ocd_recover_a10 = SH3673510_HW_DEFAULT_OCD_RECOVER_A10;
    p.occ_recover_a10 = sh3673510_quantize_current_a10(p.occ1_a10, SH3673510_BOARD_SHUNT_UOHM, 1375u, 31u, 0);
    if (bms_afe_hw_profile_validate(&p)) return 3;
    puts("D014 independent AFE defaults and effective recovery boundaries: PASS");
    return 0;
}
"""
run_c(code, flags=host_includes(ROOT, 'd014'), name='d014-afe-defaults')
