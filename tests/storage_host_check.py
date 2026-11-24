"""用 D008 生产记录、业务存储及参数协议执行 RAM Flash 故障注入。"""
from validation_support import read, run_c

source = read("tests/fixtures/d008_storage/stores.c")
units = [f"bms/core/{name}.c" for name in (
    "storage_record", "bms_soc", "bms_state", "bms_sw_protection", "bms_sleep_config",
    "bms_afe_hw_profile", "bms_parameter_access")]
run_c(source, sources=units, name="d008-semantic-stores")
run_c(source, sources=units, flags=["-DD008_TEST_FACTORY=1"], name="d008-factory-stores")
