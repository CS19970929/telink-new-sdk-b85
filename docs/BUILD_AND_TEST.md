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

命令行产物位于 `project/tlsr_tc32/B85/825x_ble_sample_cli/`；不要把 `.raw.bin` 当最终烧录文件。

## 2. Source-order

`bms_tools/source_order.txt` 是版本化链接顺序清单。只有源码集合确实变化时才运行：

```bash
python3 bms_tools/bms.py sources --update
git diff -- bms_tools/source_order.txt
```

必须人工确认没有非预期排序漂移。

## 3. Host contract tests：按产品运行

### D008 / DVC1124

```bash
python tests/dvc1124_config_quick_check.py
python tests/d008_framework_contract_check.py
python tests/d008_20s_profile_contract_check.py
python tests/sw_protection_contract_check.py
python tests/afe_hw_profile_contract_check.py
python tests/afe_hw_access_contract_check.py
python tests/soc_contract_check.py
python tests/flash_quick_check.py
```

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

使用分支 CI 中对应的 D013 integration contract，加上 SH family、software protection、AFE HW profile、SOC、Flash contracts。测试名称/入口以当前 `.github/workflows/bms-ci.yml` 为最终事实；不要因为历史文档里写着 D011/DVC 名称就执行错产品测试。

## 4. CI 的含义

Host contract 主要验证源码结构、常量、映射和协议契约；TC32 job 验证真实目标编译/link/check-fw/MAP/manifest/verify/cppcheck。两者都不能证明：

- 原理图连接正确；
- AFE 实际寄存器 readback/延时正确；
- MOS、短路、温度、低功耗在实板安全；
- 产品最终保护参数已签核。

这些必须完成当前分支 `HARDWARE_VALIDATION.md`。

## 5. 发布门禁

每个候选 commit 至少满足：

1. `sources --check` 和当前产品 Host contracts 通过；
2. 固定 TC32 clean rebuild 通过；
3. `check-fw`、size/MAP、manifest/verify、cppcheck 通过；
4. 保护/AFE/IO/通信/Flash/OTA/低功耗改动对应实板测试完成；
5. 测试记录绑定板号/BOM、AFE型号、固件commit、requested/effective参数、仪器和结果。

烧录/OTA布局以当前 linker、`bms_tools` 和 `STORAGE.md` 为准；禁止凭历史文档修改保留区。