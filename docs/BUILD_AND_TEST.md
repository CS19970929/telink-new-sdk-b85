# 构建、测试与发布门禁

## 1. 固定目标环境

正式固件使用项目锁定的 `tc32-elf-gcc 4.5.1-tc32-1.3`、Telink B85 Vendor 库、现有 `boot.link` 和 `tl_check_fw2.exe`。不得用 host GCC/clang、ARM GCC 或其他 ABI 的成功结果替代 TC32 production build。

```powershell
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static --no-report
```

## 2. Source-order

`bms_tools/source_order.txt` 是版本化链接顺序清单。源码集合改变时才运行 `python bms_tools/bms.py sources --update`，并人工检查 diff。

## 3. Host contracts：按产品运行

### D008 / DVC1124
`dvc1124_config_quick_check.py`、`d008_framework_contract_check.py`、`d008_20s_profile_contract_check.py`、software protection、AFE HW profile/access、SOC、Flash。

### D011 / SH3673510 10S

```bash
python tests/sh3673520_contract_check.py
python tests/sh3673510_d011_integration_check.py
python tests/sh3673510_protection_mode_check.py
python tests/sh3673510_temperature_encoding_check.py
python tests/sh3673510_comm_mode_check.py
python tests/sw_protection_contract_check.py
python tests/afe_hw_profile_contract_check.py
python tests/afe_hw_access_contract_check.py
python tests/soc_contract_check.py
python tests/flash_quick_check.py
```

### D013 / SH3673510 4S
使用当前分支 `.github/workflows/bms-ci.yml` 指定的 D013 integration contract，加 SH family、software protection、AFE HW profile、SOC、Flash contracts。工作流文件是测试入口的最终事实。

## 4. CI 的含义

Host contracts 主要验证源码结构、常量、映射和协议契约；TC32 job 验证真实目标编译/link/check-fw/MAP/manifest/verify/cppcheck。两者都不能替代原理图核对、AFE寄存器readback、MOS/短路/温度/低功耗实板验证和最终产品参数签核。

## 5. 发布门禁

1. `sources --check` 和当前产品 Host contracts 通过；
2. 固定 TC32 clean rebuild 通过；
3. `check-fw`、size/MAP、manifest/verify、cppcheck 通过；
4. 当前分支 `HARDWARE_VALIDATION.md` 对应项完成；
5. 实测记录绑定板号/BOM、AFE型号、固件commit、requested/effective、仪器和结论。

Flash/OTA布局以当前 linker、`bms_tools` 和 `STORAGE.md` 为准，不按历史文档猜地址。