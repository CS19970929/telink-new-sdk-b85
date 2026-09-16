#!/usr/bin/env python3
"""DVC1124 contract for product branches whose active AFE is SH3673510.

DVC1124 sources remain buildable as shared/dormant code, but D008-only product
policy assertions do not apply to D011/D013.  This check enforces the storage
boundary that still matters on an SH product branch: dormant DVC code must not
re-introduce a private Flash/KV owner after Storage V1.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"


def read(name):
    return (SRC / name).read_text(encoding="utf-8", errors="strict")


backend = read("bms_afe_backend.h")
active_sh = re.search(
    r"^\s*#define\s+BMS_AFE_BACKEND\s+BMS_AFE_BACKEND_SH3673510\s*$",
    backend,
    re.MULTILINE,
)
if not active_sh:
    raise AssertionError(
        "inactive-DVC contract is only valid when SH3673510 is the selected backend"
    )

store = read("dvc1124_config_store.c")
store_hdr = read("dvc1124_config_store.h")
service = read("dvc1124_config_service.c")
service_hdr = read("dvc1124_config_service.h")

for token in (
    "flash_kv32",
    "flash_store_cfg",
    "DVC1124_ConfigStoreLoad",
    "DVC1124_ConfigStoreSave",
    "DVC1124_ConfigStoreRestore",
    "DVC1124_ConfigStoreCapture",
):
    if token in store:
        raise AssertionError(f"legacy DVC Flash ownership returned: {token}")

if "DVC1124_FIXED_CONFIG_COMPILE_TIME" not in store_hdr:
    raise AssertionError("DVC fixed configuration must remain compile-time owned")
if "dvc1124_persistent_config_t" in store_hdr:
    raise AssertionError("DVC fixed register configuration must not be a persisted object")
if "DVC1124_ConfigStore" in service:
    raise AssertionError("DVC config service must not depend on a private ConfigStore")
if "diagnostic READ-ONLY" not in service_hdr:
    raise AssertionError("fixed DVC configuration must stay diagnostic/read-only")
if "bms_afe_hw_profile_get(&hw)" not in service:
    raise AssertionError("runtime AFE protection must remain owned by the common HW profile")

print("DVC1124 inactive-backend Storage V1 contract: PASS")
