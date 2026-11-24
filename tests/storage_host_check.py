"""用 D008 生产记录、业务存储及参数协议执行 RAM Flash 故障注入。"""
from validation_support import function, read, run_c, without_includes

source = read("tests/fixtures/d008_storage/stores.c")
units = [f"bms/core/{name}.c" for name in (
    "storage_record", "bms_soc", "bms_state", "bms_sw_protection", "bms_sleep_config",
    "bms_afe_hw_profile", "bms_parameter_access")]
run_c(source, sources=units, name="d008-semantic-stores")
run_c(source, sources=units, flags=["-DD008_TEST_FACTORY=1"], name="d008-factory-stores")

telink = "bms/platform/telink/"
platform = without_includes(read(telink + "flash_store_cfg.h"))
platform += "\n" + function(read(telink + "flash_store_safe.h"), "static inline int flash_store_verify_erased(")
platform += "\n" + without_includes(read(telink + "bms_storage_platform_telink.c"))
run_c(read("tests/fixtures/d008_storage/legacy_platform.c").replace("/* PRODUCTION */", platform),
      name="d008-legacy-flash-boundary")
