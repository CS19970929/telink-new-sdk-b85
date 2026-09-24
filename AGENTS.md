# AGENTS.md — D014 / TLSR8251 / SH3673510

本分支目标是 **HS-D014-8S15A + TLSR8251F512ET32 + SH3673510 + 8S**。实现基线来自 `refactor/d011-common-bms-features`，但所有板级事实必须以 D014 原理图/BOM 为准，不能把 D011 独有 heater/fuse 硬件继续带入。

## 开发前必读

1. `docs/D014_PRODUCT_REFERENCE.md`：D014 原理图事实、代码配置和未签核参数。
2. `docs/HARDWARE_VALIDATION.md`：实板发布阻断项。
3. `docs/ARCHITECTURE.md`。
4. `docs/SOFTWARE_PROTECTION.md`、`docs/AFE_HARDWARE_PROTECTION_V2.md`。
5. `tests/sh3673510_d014_integration_check.py`：D014 板级静态 contract。

## D014 已确认配置

- MCU：TLSR8251F512ET32。
- AFE：SH3673510。
- 有效 cell：8S，VC0..VC8 / B0..B8。
- Rsense：RS1/RS2/RS3 = 2mΩ 并联；软件整数模型 667µΩ。
- SPI：PB6 MISO / PB7 MOSI / PD7 SCLK / PD2 CS，Mode 3 / 500kHz。
- RS485：PA1 485-EN，PC2 SCI1-TX，PC3 SCI1-RX；PD4 CMNT-EN，PD3 CMNT-WK。
- TS1/TS2：10K-3435。
- TS3：NC，因此 D014 heater 必须 disabled。
- TS4：用户确认实装 10K-3435 MOS NTC；用于独立 MOS 高温保护。RN4 图纸标 10M 与实装 BOM 不一致，保留为图纸差异待核对。
- Balance：B1..B8 有物理均衡通道，允许启用公共 balance 策略。

## 关键安全边界

- `SH3673510_PRODUCT_HEATER_SUPPORTED=0`。不得在 D014 上配置或驱动 D011 的 PB4/HT-CHG、PB5/HT-RF-EN 路径。
- `SH3673510_PRODUCT_HEATER_NTC_SUPPORTED=0`。
- `SH3673510_PRODUCT_MOS_NTC_SUPPORTED=1`。TS4 无效必须触发温度断线关断；MOS 高温使用独立软件阈值。AFE TS4 硬件温度位暂不开启，因为芯片对 TS1/TS2/TS4 共用 OTC/UTC 阈值，不能表达独立 MOS 高温策略。
- 软件保护 `g_tParam.protect` 与 AFE hardware profile 必须继续独立。
- SC/OCD/OCC 恢复必须依赖物理恢复窗口/AFE 状态，不能仅凭关 MOS 后电流为 0。
- 通信失败、AFE 重配失败、wake 失败都必须保持 fail-safe。
- 8S 之外 VC9..VC20 不得进入 cell min/max、SOC、保护、balance 或 open-wire。

## 命名兼容

`sh3673510_project_config.h` 当前保留部分 `SH3673510_D011_*` 和 `D011_*` 兼容别名，是为了最小风险复用已验证公共实现。D014 新增代码必须优先使用 `D014_*` 板级宏；不要把兼容别名当成 D011 硬件事实。

## 产品参数边界

原理图不能确定额定容量和最终保护参数。当前 `CapacityFactory=116`、`AFE_ODC1/2` 是继承迁移默认；不要在文档或发布说明里称为 D014 已签核值。D14 暂时与历史 D11 共享 numeric wire/storage ID，修改该 ID 前必须同步 Windows 上位机和兼容策略。

## 构建与验证

至少运行：

```text
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

Host contract/CI 通过不能替代实板验证。

## Windows 上位机

D008/D011/D013/D014 的 Windows 工具继续以分支 `feature/windows-afe-hw-protection-editor-v2` 下 `bms-tool-windows/` 为单一真源。默认不要为了 UI 便利修改固件协议。若后续给 D014 分配独立 numeric product ID，必须同步上位机并保留兼容处理。
