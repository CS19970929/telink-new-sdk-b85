#!/usr/bin/env python3
"""Tighten D011 protection parameter validation before AFE/Flash commit."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"
CONTROL = VENDOR / "sh3673510_control.c"
TEST = ROOT / "tests" / "sh3673510_d011_integration_check.py"

NEW_VALIDATE = r'''static uint8_t sh3510_validate_protection(void)
{
    const struct PRT_E2ROM_PARAS *p = &g_tParam.protect;

    /* High-going software protection levels may be equal but never reverse. */
    if ((p->u16VcellOvp_First > p->u16VcellOvp_Second) ||
        (p->u16VcellOvp_Second > p->u16VcellOvp_Third) ||
        (p->u16VbusOvp_First > p->u16VbusOvp_Second) ||
        (p->u16VbusOvp_Second > p->u16VbusOvp_Third) ||
        (p->u16IchgOcp_First > p->u16IchgOcp_Second) ||
        (p->u16IchgOcp_Second > p->u16IchgOcp_Third) ||
        (p->u16IdsgOcp_First > p->u16IdsgOcp_Second) ||
        (p->u16IdsgOcp_Second > p->u16IdsgOcp_Third) ||
        (p->u16TChgOTp_First > p->u16TChgOTp_Second) ||
        (p->u16TChgOTp_Second > p->u16TChgOTp_Third) ||
        (p->u16TdischgOTp_First > p->u16TdischgOTp_Second) ||
        (p->u16TdischgOTp_Second > p->u16TdischgOTp_Third) ||
        (p->u16TmosOTp_First > p->u16TmosOTp_Second) ||
        (p->u16TmosOTp_Second > p->u16TmosOTp_Third) ||
        (p->u16VdeltaOvp_First > p->u16VdeltaOvp_Second) ||
        (p->u16VdeltaOvp_Second > p->u16VdeltaOvp_Third)) return 0u;

    /* Low-going levels progress downward as severity increases. */
    if ((p->u16VcellUvp_First < p->u16VcellUvp_Second) ||
        (p->u16VcellUvp_Second < p->u16VcellUvp_Third) ||
        (p->u16VbusUvp_First < p->u16VbusUvp_Second) ||
        (p->u16VbusUvp_Second < p->u16VbusUvp_Third) ||
        (p->u16TchgUTp_First < p->u16TchgUTp_Second) ||
        (p->u16TchgUTp_Second < p->u16TchgUTp_Third) ||
        (p->u16TdischgUTp_First < p->u16TdischgUTp_Second) ||
        (p->u16TdischgUTp_Second < p->u16TdischgUTp_Third) ||
        (p->u16SocUp_First < p->u16SocUp_Second) ||
        (p->u16SocUp_Second < p->u16SocUp_Third)) return 0u;

    /* Third-level trip/recovery hysteresis must have the safe direction. */
    if ((p->u16VcellOvp_Rcv >= p->u16VcellOvp_Third) ||
        (p->u16VbusOvp_Rcv >= p->u16VbusOvp_Third) ||
        (p->u16IchgOcp_Rcv >= p->u16IchgOcp_Third) ||
        (p->u16IdsgOcp_Rcv >= p->u16IdsgOcp_Third) ||
        (p->u16TChgOTp_Rcv >= p->u16TChgOTp_Third) ||
        (p->u16TdischgOTp_Rcv >= p->u16TdischgOTp_Third) ||
        (p->u16TmosOTp_Rcv >= p->u16TmosOTp_Third) ||
        (p->u16VdeltaOvp_Rcv >= p->u16VdeltaOvp_Third)) return 0u;
    if ((p->u16VcellUvp_Rcv <= p->u16VcellUvp_Third) ||
        (p->u16VbusUvp_Rcv <= p->u16VbusUvp_Third) ||
        (p->u16TchgUTp_Rcv <= p->u16TchgUTp_Third) ||
        (p->u16TdischgUTp_Rcv <= p->u16TdischgUTp_Third) ||
        (p->u16SocUp_Rcv <= p->u16SocUp_Third)) return 0u;

    /* SH36735xx OV/UV threshold field is 10-bit with 5 mV/LSB. */
    if ((p->u16VcellOvp_First == 0u) || (p->u16VcellOvp_Third > 5115u) ||
        (p->u16VcellUvp_First == 0u) || (p->u16VcellUvp_Third > 5115u)) return 0u;

    /* D011 Rsense=250uOhm. Reject values above the AFE's highest encodable
     * current instead of silently clamping them. Lower-than-minimum requests
     * are allowed and their higher hardware backup threshold is reported via
     * the 0x2180 actual-value window. */
    if (p->u16IdsgOcp_First > 3200u) return 0u;  /* OCD1: 80mV -> 320A */
    if (p->u16IdsgOcp_Second > 6400u) return 0u; /* OCD2: 160mV -> 640A */
    if (p->u16IchgOcp_First > 1760u) return 0u;  /* OCC: 44mV -> 176A */

    /* AFE delay encodings: OV/UV/OCD1/OCC top out at ~10.01s; OCD2 shares
     * the discharge filter request but can encode only 25..400ms. */
    if ((p->u16VcellOvp_Filter > 1001u) ||
        (p->u16VcellUvp_Filter > 1001u) ||
        (p->u16IchgOcp_Filter > 1001u) ||
        (p->u16IdsgOcp_Filter > 40u)) return 0u;

    /* Temperature format is (degC + 40) * 10 and the installed lookup table
     * spans -40..105C. Validate every software level/recovery that uses it. */
    if ((p->u16TChgOTp_First > 1450u) || (p->u16TChgOTp_Second > 1450u) ||
        (p->u16TChgOTp_Third > 1450u) || (p->u16TChgOTp_Rcv > 1450u) ||
        (p->u16TchgUTp_First > 1450u) || (p->u16TchgUTp_Second > 1450u) ||
        (p->u16TchgUTp_Third > 1450u) || (p->u16TchgUTp_Rcv > 1450u) ||
        (p->u16TdischgOTp_First > 1450u) || (p->u16TdischgOTp_Second > 1450u) ||
        (p->u16TdischgOTp_Third > 1450u) || (p->u16TdischgOTp_Rcv > 1450u) ||
        (p->u16TdischgUTp_First > 1450u) || (p->u16TdischgUTp_Second > 1450u) ||
        (p->u16TdischgUTp_Third > 1450u) || (p->u16TdischgUTp_Rcv > 1450u) ||
        (p->u16TmosOTp_First > 1450u) || (p->u16TmosOTp_Second > 1450u) ||
        (p->u16TmosOTp_Third > 1450u) || (p->u16TmosOTp_Rcv > 1450u)) return 0u;

    if ((p->u16SocUp_First > 100u) || (p->u16SocUp_Second > 100u) ||
        (p->u16SocUp_Third > 100u) || (p->u16SocUp_Rcv > 100u)) return 0u;

    return 1u;
}
'''


def main() -> None:
    text = CONTROL.read_text(encoding="utf-8")
    pattern = r"static uint8_t sh3510_validate_protection\(void\)\n\{.*?\n\}\n\nuint8_t sh3673510_control_apply_protection"
    replacement = NEW_VALIDATE + "\nuint8_t sh3673510_control_apply_protection"
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        if NEW_VALIDATE.strip() not in text:
            raise RuntimeError("protection validation function not found")
        updated = text
    CONTROL.write_text(updated, encoding="utf-8", newline="\n")

    test = TEST.read_text(encoding="utf-8")
    marker = 'require(control, "sh3673510_control_get_protection_actual")\n'
    checks = '''require(control, "p->u16IdsgOcp_First > 3200u")\nrequire(control, "p->u16IdsgOcp_Second > 6400u")\nrequire(control, "p->u16IchgOcp_First > 1760u")\nrequire(control, "p->u16IdsgOcp_Filter > 40u")\nrequire(control, "p->u16SocUp_First > 100u")\n'''
    if 'require(control, "p->u16IdsgOcp_First > 3200u")' not in test:
        if marker not in test:
            raise RuntimeError("test validation marker missing")
        test = test.replace(marker, marker + checks, 1)
    TEST.write_text(test, encoding="utf-8", newline="\n")
    print("D011 complete protection parameter validation applied")


if __name__ == "__main__":
    main()
