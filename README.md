# D013 / TLSR8251 / SH3673510 BMS

当前分支目标为 **D013 + TLSR8251F512ET32 + SH3673510**。源码 product profile 当前为 **4S / 100µΩ**，主通信配置为 direct UART (`MODBUS_RS485_ENABLE=0`)。

> **当前没有在已提供资料中找到 D013 专属原理图/BOM。** 因此 D013 的 GPIO、NTC、heater/fuse、通信唤醒等板级连接尚不能按硬件事实确认。当前源码大量继承 `D011_*` 宏，只能视为 CODE，不得当作 D013 原理图结论。

## 当前架构

- AFE backend：当前源码为 SH3673510，系列共用驱动文件仍名 `sh3673520_*`。
- 软件保护：统一 `bms_sw_protection.*`，参数为 `g_tParam.protect` First/Second/Third/Recover/Filter。
- AFE 硬件保护：独立 `bms_afe_hw_profile_t`；与软件保护分开修改/持久化。
- 当前源码 profile：4S、100µΩ；但硬件事实需 D013 原理图确认。
- 当前桌面上位机为 `tools/BMSAssistantQt`；主要通过 BLE SPP + Modbus 调试，专用 AFE HW V2 编辑器尚未实现在该 Qt 工程中。

## 文档入口

建议阅读顺序：

1. [D013 配置修改、拉代码、固件编译与上位机构建指南](docs/CONFIGURATION_AND_BUILD_GUIDE.md) — 想改什么、改哪儿、具体怎么改，以及完整 clone/build/package 命令。
2. [D013 产品硬件与固件配置基线](docs/D013_PRODUCT_REFERENCE.md) — 明确区分源码事实与缺失的原理图证据。
3. [D013 实板验证与发布阻断项](docs/HARDWARE_VALIDATION.md) — 首要任务是补齐 D013 原理图/BOM 并重建 IO 真值。
4. [BMS 软件架构与配置所有权](docs/ARCHITECTURE.md)
5. [软件三级保护](docs/SOFTWARE_PROTECTION.md)
6. [AFE Hardware Protection V2](docs/AFE_HARDWARE_PROTECTION_V2.md)
7. [SOC](docs/SOC.md)
8. [Flash / Storage](docs/STORAGE.md)
9. [构建与测试](docs/BUILD_AND_TEST.md)
10. [Windows/macOS/Linux Qt 上位机](tools/BMSAssistantQt/README.md)

D008/DVC1124 文档和 D011 原理图/状态文档不属于 D013。D013 中残留的 `D011_*` 宏和 `BT_D011`/`D011` 产品身份是源码技术债，不能由文档静默改名掩盖。

## 快速拉取

```bash
git clone --single-branch --branch feature/sh3673510-d013-bms https://github.com/CS19970929/telink-new-sdk-b85.git D013-BMS
cd D013-BMS
```

## 固件构建

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

最终烧录文件：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin
```

## Windows Qt 上位机

```bat
cd /d D013-BMS\tools\BMSAssistantQt
scripts\run.bat
scripts\package-windows.bat
```

D013 当前代码 profile 为4S，因此打包前必须在 `bmsassistantqt/protocol.py` 把 `currentProjectSeriesCount` 改为4；当前共享工具历史默认仍是10。完整说明见 `docs/CONFIGURATION_AND_BUILD_GUIDE.md`。

## 发布原则

D013 当前可通过软件/TC32 构建只能证明代码基线。没有 D013 原理图/BOM时，不能宣称当前 GPIO、100µΩ、NTC、heater/fuse、UART 等已经完成硬件签核；必须先按 `HARDWARE_VALIDATION.md` 补齐证据。