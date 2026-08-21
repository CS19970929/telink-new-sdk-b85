# BMSAssistant Apple

## 定位

`BMSAssistant` 是 SwiftUI + CoreBluetooth 的 Apple 客户端代码库。

当前两条路径：

- macOS：已有可运行的工程调试桌面 App
- iPhone/iPad：已实现移动 UI、普通 BMS BLE 与 Telink OTA 源码，但当前仍需要正式 Xcode iOS App target / signing 配置后才能安装真机

## 当前普通 BLE 能力

- BLE 扫描、连接、断开
- Telink SPP 通道发现与 Notify
- Modbus RTU over BLE
- CRC 与分片重组
- 电池状态页面
- 单体电压
- SOC/SOH/容量/温度/SystemStatus
- 设备身份
- 保护参数/事件日志预览
- 手动读写、Echo、Raw frame
- 写 SOC / 0x1103 / 蓝牙名 suffix

## iPhone/iPad UI

移动端入口：

```text
Sources/BMSAssistant/Views/MobileContentView.swift
```

Tab：

```text
设备
电池
工程
OTA
```

普通 BLE 与桌面版本共用现有 AppModel / ModbusCodec / BatteryStatusSnapshot。

## iPhone/iPad OTA

OTA codec：

```text
Sources/BMSAssistant/Protocol/TelinkOTA.swift
```

CoreBluetooth OTA session：

```text
Sources/BMSAssistant/Models/OTAViewModel.swift
```

OTA UI：

```text
Sources/BMSAssistant/Views/OTAMobileView.swift
```

实现：

- 独立 OTA CoreBluetooth session，不与普通 Modbus pending request 复用
- 扫描、选择、连接目标 BMS
- OTA Service/Characteristic discovery
- OTA Notify
- iOS file importer 选择 firmware.bin
- `KNLT` / firmware size 校验
- Legacy OTA 16-byte PDU
- Write With Response 严格串行发送
- START / DATA / END / OTA_RESULT
- 升级进度
- 成功后预期 BMS 断开并重启

OTA Service：

```text
00010203-0405-0607-0809-0A0B0C0D1912
```

OTA Characteristic：

```text
00010203-0405-0607-0809-0A0B0C0D2B12
```

## iOS 当前缺口

当前 `Package.swift` 主要服务于 macOS SwiftPM 可执行程序。虽然源码已经按 `#if os(iOS)` 提供 iPhone/iPad UI/OTA，但要形成可安装 iPhone 的 App 仍需：

1. 建立正式 Xcode iOS App target/project。
2. 设置 Bundle Identifier / Team / Signing。
3. 配置 `NSBluetoothAlwaysUsageDescription`。
4. 将当前 Swift Sources 接入 App target。
5. 真机运行 CoreBluetooth。
6. 真机验证普通 BLE 与 OTA。
7. 最终 Archive / TestFlight / IPA 交付流程。

因此当前状态应称为：

```text
iOS source implementation complete enough for integration
real iOS application packaging/signing pending
```

而不是“已上架/已生成 iOS App”。

## macOS 构建

```bash
cd vendor/ble_sample/BMSAssistant
swift build
```

日常运行：

```bash
./scripts/run-macos-app.sh
```

该脚本会生成带蓝牙权限说明的 macOS `.app`。

## 协议约束

普通 BMS：

```text
Service 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
Write   6E400002-B5A3-F393-E0A9-E50E24DCCA9E
Notify  6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

普通请求继续按 ATT MTU 23 / 单请求 <=20 byte 基线。

OTA：详见：

- `docs/BMS_OTA_通信规范_V1.0.md`
- `docs/ota_test_vectors.json`

## 自动验证

`.github/workflows/bms-client-ci.yml` 当前对 `TelinkOTA.swift` 执行 `swiftc -parse`，用于拦截 Swift OTA codec 语法错误。

CoreBluetooth 和 SwiftUI 的 iOS 行为最终必须由 Xcode/iPhone 真机验收。

## 安全边界

当前 BMS 固件 `BLE_APP_SECURITY_ENABLE=0`。iOS 工程页和 OTA 页当前均为开发/工程入口；正式客户版本必须增加危险操作授权和固件可信升级策略。
