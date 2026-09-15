# 配套 BMS 客户端

当前仓库客户端：

| 目录 | 平台 | 技术 | 当前定位 |
|---|---|---|---|
| `tools/BMSAssistantQt/` | Windows / macOS / Linux | Python / PySide6 / QtBluetooth | **当前桌面上位机主入口** |
| `tools/BMSAssistant/` | macOS | Swift / CoreBluetooth | 原生 macOS 客户端 |
| `tools/BMSAssistantAndroid/` | Android | Kotlin / Android BLE | Android 客户端 |

桌面 Qt 上位机的完整 clone、运行、打包、输出目录和各产品串数配置见：

```text
tools/BMSAssistantQt/README.md
```

三端把 BLE SPP 当作字节传输层，共用 `Modbus RTU over BLE` 语义。协议资料位于：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/docs/
├── register_catalog.json
├── protocol_test_vectors.json
└── generated/
```

生成/校验资产：

```bash
python3 tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/script/bms_client_asset_tool.py all
```

不要直接手改 `generated/`。

注意：当前 `register_catalog.json` 和 Qt 客户端仍保留部分历史10S描述/常量。D008、D013 打包上位机前，必须按对应分支 `docs/CONFIGURATION_AND_BUILD_GUIDE.md` 核对产品串数和协议布局。当前 `BMSAssistantQt` 也尚未实现专用 AFE Hardware Protection V2 编辑器/direct-serial transport。