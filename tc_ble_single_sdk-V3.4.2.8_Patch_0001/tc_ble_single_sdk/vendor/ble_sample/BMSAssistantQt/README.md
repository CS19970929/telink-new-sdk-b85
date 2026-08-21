# BMSAssistantQt

## 项目说明

`BMSAssistantQt` 是 `PySide6 + QtBluetooth + QtWidgets` 的跨平台 BMS 工程上位机，覆盖 Windows/macOS/Linux。

普通 BMS 功能：

- BLE 扫描、连接、断开
- Telink SPP Service/Characteristic discovery
- Modbus RTU over BLE
- CRC 与 Notify 分片重组
- 电池状态自动刷新
- Cell 1~10、总压、电流、SOC/SOH、容量、温度、SystemStatus
- 设备身份
- 保护参数/事件日志预览
- 手动读写寄存器、Echo、Raw frame
- 写 SOC / 0x1103 / 蓝牙名 suffix
- JSON 电池快照、CSV 报文日志

OTA：

- Telink Legacy OTA V1
- 独立 OTA GUI：`ota_tool.py`
- 16-byte firmware PDU / 20-byte OTA data packet
- `.bin` header `KNLT` / firmware size 校验
- START / DATA / END / OTA_RESULT
- Write With Response 串行发送
- OTA 进度、result code 与日志

## 目录

```text
BMSAssistantQt/
├── main.py                 # 普通 BMS 上位机
├── ota_tool.py             # Telink OTA 工程工具
├── bmsassistantqt/
│   ├── app_controller.py
│   ├── ble_transport.py
│   ├── models.py
│   ├── protocol.py
│   ├── ota.py              # OTA codec / bin parser
│   └── ui/main_window.py
├── tests/test_ota_codec.py
└── scripts/
    ├── run.sh
    ├── run.bat
    ├── run-ota.sh
    ├── run-ota.bat
    ├── run-macos-app.sh
    ├── package-macos.sh
    ├── package-linux.sh
    └── package-windows.bat
```

## 普通上位机运行

Windows：

```bat
cd /d "...\vendor\ble_sample\BMSAssistantQt"
scripts\run.bat
```

macOS：

```bash
cd vendor/ble_sample/BMSAssistantQt
./scripts/run-macos-app.sh
```

Linux：

```bash
cd vendor/ble_sample/BMSAssistantQt
./scripts/run.sh
```

## OTA 运行

Windows：

```bat
cd /d "...\vendor\ble_sample\BMSAssistantQt"
scripts\run-ota.bat
```

macOS/Linux 开发运行：

```bash
cd vendor/ble_sample/BMSAssistantQt
./scripts/run-ota.sh
```

macOS 真机 BLE 使用时仍需保证 Python/Qt 进程具备系统蓝牙权限；后续交付包可把 OTA GUI 与主上位机统一包装进同一 `.app`。

## OTA 真机测试

1. 启动 OTA 工具。
2. 扫描 BMS，选择目标设备。
3. 点击连接，必须发现：

```text
Service 00010203-0405-0607-0809-0A0B0C0D1912
Characteristic 00010203-0405-0607-0809-0A0B0C0D2B12
```

4. 选择目标 `firmware.bin`。
5. 必须通过 `KNLT` 与 header firmware size 校验。
6. 点击开始 OTA，确认升级期间不掉电。
7. 工具严格按 START -> DATA -> END 顺序串行发送。
8. 只有收到：

```text
06 FF 00
```

才判定 OTA 成功。
9. 等待 BMS 重启。
10. 用普通上位机重新连接并读取软件版本，确认新固件运行。

## 普通 BLE 协议

```text
Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
Write:   6E400002-B5A3-F393-E0A9-E50E24DCCA9E
Notify:  6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

当前安全基线：

- ATT MTU 23
- 单请求 <=20 byte
- `0x10` 建议 <=5 words
- 大响应按完整 Modbus frame 长度与 CRC 重组

## OTA 协议

详见：

- `docs/BMS_OTA_通信规范_V1.0.md`
- `docs/ota_test_vectors.json`

固定回归向量：

```text
START
01 FF

DATA index=0
00 00 26 80 00 00 00 00 5D 02 4B 4E 4C 54 30 04 88 00 1C A3

END max_index=0x14FA
02 FF FA 14 05 EB
```

## 自动验证

GitHub Actions：

```text
.github/workflows/bms-client-ci.yml
```

Python OTA codec 固定向量会在 PR 中自动验证。

## 安全边界

当前 BMS 固件：

```text
BLE_APP_SECURITY_ENABLE = 0
```

因此 OTA 当前属于工程联调功能，不是最终量产安全方案。正式版本需要增加 BLE/应用层鉴权与固件可信校验。
