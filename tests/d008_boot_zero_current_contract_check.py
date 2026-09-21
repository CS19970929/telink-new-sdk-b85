"""D008 boot zero-current calibration contract checks."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk" / "vendor" / "ble_sample"

driver = (BASE / "dvc1124.c").read_text(encoding="utf-8")
header = (BASE / "dvc1124.h").read_text(encoding="utf-8")
config = (BASE / "dvc1124_project_config.h").read_text(encoding="utf-8")
backend = (BASE / "dvc1124_config_store.c").read_text(encoding="utf-8")
conf = (BASE / "conf.h").read_text(encoding="utf-8")

assert "#define DVC1124_BOOT_ZERO_ENABLE              1u" in config
assert "#define DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS  270u" in config
assert "DVC1124_BootCurrentZeroCalibrate" in header
assert "DVC1124_StartCadcCalibration()" in driver
assert driver.count("dvc_boot_zero_wait_fresh_cc2();") == 2
assert "DVC1124_FET_PDSGC_MASK" in driver and "DVC1124_FET_PCHGC_MASK" in driver
assert "DVC1124_CC2_DSGF_MASK" in driver and "DVC1124_CC2_CHGF_MASK" in driver
assert "factory_current_ma = bms_config_calibrate_current(current_ma);" in driver
assert "current_ma = dvc_apply_boot_zero(factory_current_ma);" in driver
assert "#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u" in conf

init_pos = backend.index("DVC1124_UpdataAfeConfig();")
zero_pos = backend.index("(void)DVC1124_BootCurrentZeroCalibrate();")
assert init_pos < zero_pos

zero_fn = driver[driver.index("uint8_t DVC1124_BootCurrentZeroCalibrate(void)"):]
assert "bms_config_set_user" not in zero_fn
assert "storage_record_save" not in zero_fn

print("PASS D008 boot zero-current calibration is init-only, FET-safe, RAM-only, deadband-preserving")
