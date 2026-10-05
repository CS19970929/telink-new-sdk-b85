"""Require every product to compile the same physical common implementation."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
products = ("d008", "d011", "d013", "d014")
common = {p.relative_to(ROOT).as_posix() for p in (ROOT / "bms/core").glob("*.c")}
sdk_sets = []
for product in products:
    entries = [line.strip() for line in (ROOT / "bms/products" / product / "sources.txt").read_text().splitlines()
               if line.strip() and not line.startswith("#")]
    assert len(entries) == len(set(entries)), product + " duplicate translation unit"
    assert common <= set(entries), product + " omitted shared module"
    expected_backend = "dvc1124" if product == "d008" else "sh3673510"
    for relative in entries:
        assert not Path(relative).is_absolute() and ".." not in Path(relative).parts
        assert (ROOT / relative).is_file(), relative
        assert "vendor/ble_sample/" not in relative, relative
        if relative.startswith("bms/afe/"):
            assert relative.startswith("bms/afe/" + expected_backend + "/")
    sdk_sets.append({x for x in entries if not x.startswith("bms/")})
    assert not list((ROOT / "bms/products" / product).glob("*.c")), "Product directories are data only"
assert all(sdk == sdk_sets[0] for sdk in sdk_sets)
assert not (ROOT / "bms_tools/source_order.txt").exists()
baselines = json.loads((ROOT / "bms/products/baselines.json").read_text())
assert set(baselines) == set(products)
for product, record in baselines.items():
    for field in ("import_snapshot_commit", "comparison_commit", "recorded_import_commit"):
        assert len(record[field]) == 40 and all(c in "0123456789abcdef" for c in record[field])
    assert isinstance(record["recorded_import_available"], bool)
# Shared SH code owns register formulas, not GPIO literals or NTC/threshold inputs.
shared = (ROOT / "bms/afe/sh3673510/sh3673510_project_config.h").read_text()
assert " GPIO_P" not in shared and "#define SH3673510_HW_DEFAULT_" not in shared
for product in products[1:]:
    config = (ROOT / "bms/products" / product / "bms_sh3673510_config.h").read_text()
    assert "#define SH3673510_BOARD_BAT_NTC1_INDEX" in config
    assert "#define SH3673510_BOARD_SPI_GROUP" in config
    assert "#define SH3673510_HW_DEFAULT_COV_MV" in config
legacy = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample"
assert not list(legacy.glob("*.c")), "Old application source copies remain"
print(f"PASS four products, {len(common)} shared C modules, one SDK, explicit backend selection, no source copies")
