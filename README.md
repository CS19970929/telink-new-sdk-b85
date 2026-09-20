# HS-D014 / TLSR8251 / SH3673510 BMS

当前分支：**HS-D014-8S15A + TLSR8251F512ET32 + SH3673510 + 8S**。

本分支从 `refactor/d011-common-bms-features` 的最新稳定实现派生，保留软件三级保护、独立 AFE Hardware Protection V2、SOC、Flash/OTA、balance/open-wire、通信失效 fail-safe 等公共能力；板级配置改为 D014 原理图事实。

## D014 核心配置

- 8S，有效 VC1..VC8。
- 分流：3 × 2mΩ 并联，软件使用 **667µΩ** 整数模型。
- SH3673510 SPI：PB6/PB7/PD7/PD2，Mode 3，500kHz。
- Modbus RTU over isolated RS485，PA1=485-EN。
- TS1/TS2：10K-3435。
- TS3：NC，因此 heater 整体强制关闭。
- TS4：MOS 通道，但 RN4 图纸为 10M；BOM 确认前不作为可信 MOS NTC。
- 8 路 cell balance 启用公共 balance 策略。

详细证据、差异和未签核项见 [D014_PRODUCT_REFERENCE.md](docs/D014_PRODUCT_REFERENCE.md)。

## 构建与检查

```powershell
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python tests/sh3673520_contract_check.py
python tests/sh3673510_d014_integration_check.py
python tests/sh3673510_protection_mode_check.py
python tests/sh3673510_temperature_encoding_check.py
python tests/sw_protection_contract_check.py
python tests/common_feature_policy_contract_check.py
python tests/afe_hw_profile_contract_check.py
python tests/afe_hw_access_contract_check.py
python tests/soc_contract_check.py
python tests/flash_quick_check.py
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static --no-report
```

固件输出：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin
```

## 当前发布阻断项

首次上板前重点看 [HARDWARE_VALIDATION.md](docs/HARDWARE_VALIDATION.md)。尤其需要确认：

- 667µΩ 电流增益/方向/零偏；
- D014 最终容量和 OC/SC/温度保护参数；
- TS4/RN4 实装 BOM；
- 8S balance/open-wire；
- RS485/CMNT-WK 与低功耗唤醒；
- D014 是否需要独立 numeric product ID，并同步 Windows 上位机。

编译和 host contract 通过不能替代实板保护波形、温度和低功耗验收。
