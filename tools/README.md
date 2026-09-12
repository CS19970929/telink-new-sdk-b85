# 配套 BMS 客户端

| 目录 | 平台 | 技术 |
|---|---|---|
| `BMSAssistant/` | macOS | Swift / CoreBluetooth |
| `BMSAssistantQt/` | Windows、macOS、Linux | Python / PySide6 / QtBluetooth |
| `BMSAssistantAndroid/` | Android | Kotlin / Android BLE |

三端把 BLE SPP 当作字节传输层，共用 `Modbus RTU over BLE` 语义。协议真值与测试向量位于：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/docs/
├── register_catalog.json
├── protocol_test_vectors.json
└── generated/
```

生成和校验跨平台常量：

```bash
python3 tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/script/bms_client_asset_tool.py all
```

不要手改 `generated/`。修改协议时先更新 JSON 真值和测试向量，再重新生成各语言资产，最后分别运行三个客户端目录中的构建/测试说明。
