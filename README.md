# TLSR8251 BMS 固件模板

本仓库当前产品目标为 **TLSR8251 + HS-D008 + DVC1124-2**。重构优先级是安全和兼容，其次才是代码量；保护阈值、协议、Flash 布局、OTA 边界和已验证时序不得在普通整理中改变。

## 当前状态

- 应用层只通过 `bms_afe.h` 使用 AFE；旧 `sh367309_datadeal.*`、`MTPWrite`、`App_AFEGet` 和虚拟 GPIO/ADC 兼容层已删除。
- `bms_state.*` / `bms_error.h` 统一持有 BMS 报告、系统状态、错误计数和分级故障历史，通信层不再跨文件遍历私有数组。
- DVC1124 寄存器真值集中在 `dvc1124_reg.h`；板级默认值集中在 `dvc1124_project_config.h`。
- BLE 与 UART 共用同一个 Modbus/AFE 配置服务。
- 参数、SOC、runtime、事件日志和 DVC 配置使用彼此独立的 Flash 区域。
- 主机契约测试可运行；固定 TC32 构建和 HS-D008 实板验证仍是发布前必需项。

固件主目录：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/
└── tc_ble_single_sdk/vendor/ble_sample/
```

## 常用命令

```bash
python3 bms_tools/bms.py sources --check
python3 tests/dvc1124_config_quick_check.py
python3 tests/flash_quick_check.py
python3 -m unittest tests/test_bms_tools.py
```

在已安装固定版 Telink TC32 工具链的 Windows 环境中：

```powershell
python bms_tools/bms.py env
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py ci --jobs 4
python bms_tools/bms.py verify
```

新增、删除或重命名源码后，显式更新并审核链接顺序：

```bash
python3 bms_tools/bms.py sources --update
git diff -- bms_tools/source_order.txt
```

## 文档入口

- [架构与 AFE 移植](docs/ARCHITECTURE.md)
- [构建、测试与发布门禁](docs/BUILD_AND_TEST.md)
- [Flash 与持久化](docs/STORAGE.md)
- [SOC 当前行为](docs/SOC.md)
- [待完成的硬件验证](docs/HARDWARE_VALIDATION.md)
- [DVC1124 / HS-D008 硬件基线](docs/DVC1124_HS_D008.md)
- [DVC1124 配置与通信接口](docs/DVC1124_CONFIG_INTERFACE.md)
- [DVC1124 后续任务](docs/DVC1124_DEVELOPMENT_TASKS.md)
- [配套 BMS 客户端](tools/README.md)

Vendor release note、patch note 和 license 文件保留在 SDK 原目录；它们不属于项目设计文档，不应随应用重构改写。

## 发布原则

主机测试通过只证明源码契约未回归，不代表固件可量产。任何涉及保护、MOS、采样、低功耗、Flash 或 OTA 的版本，必须完成 TC32 编译、MAP/BIN 尺寸检查、固件校验和对应实板测试，并把结果绑定到具体 commit。
