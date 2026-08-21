# BMS Client Platform

> 给开发人员和 AI/Codex 的跨平台客户端入口。协议细节先读 `BMS_BLE_通信对接规范_V1.0.md`。

## 1. 单一真源

任何客户端修改前必须优先检查：

1. `docs/BMS_BLE_通信对接规范_V1.0.md`
2. `docs/register_catalog.json`
3. `docs/protocol_test_vectors.json`
4. `docs/generated/`

禁止在新客户端里重新定义一套与上述文件无关的 UUID、寄存器地址、单位或状态位。

## 2. 当前客户端矩阵

| 客户端 | 路径 | 技术栈 | 当前定位 | 状态 |
|---|---|---|---|---|
| Windows/macOS/Linux 上位机 | `vendor/ble_sample/BMSAssistantQt` | Python + PySide6 + QtBluetooth | 工程调试主线 | 已有可用骨架，继续收口共享资产 |
| Android App | `vendor/ble_sample/BMSAssistantAndroid` | Kotlin + Jetpack Compose | 工程版/移动版 | 已有 BLE、协议、UI、导出与单测骨架 |
| macOS App | `vendor/ble_sample/BMSAssistant` | SwiftUI + CoreBluetooth | Apple 工程版 | 已有桌面完整 UI |
| iPhone/iPad App | `vendor/ble_sample/BMSAssistant` | SwiftUI + CoreBluetooth | Apple 移动版 | 新增 `MobileContentView.swift`，共用 AppModel/Protocol/Transport |
| 微信小程序 | `vendor/ble_sample/BMSAssistantMiniProgram` | 原生 TypeScript | 移动轻量版 | 已实现首版 BLE + Modbus + 状态页 |

## 3. 所有平台必须一致的行为

- Service：`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- Request：客户端写 `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- Response：订阅 `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- Modbus RTU：`0x03 / 0x06 / 0x10 / 0x7F`
- 单请求 `<= 20 byte`
- 大响应按 Notify fragment 重组
- 完整帧后再 CRC16/MODBUS 校验
- 同时只允许一个 pending request
- `0xD120` magic 有效时优先实时窗口，否则 Legacy fallback
- 危险写操作不得默认出现在正式客户模式

## 4. 开发顺序

修改协议或寄存器时：

```text
firmware
  -> register_catalog.json
  -> protocol_test_vectors.json
  -> generated assets
  -> Qt / Android / Swift / MiniProgram
  -> protocol regression
  -> real-device BLE validation
```

## 5. 当前优先级

### P0：先保证三端可对接

- 扫描、连接、Ready 状态
- Echo
- 设备身份
- 总压、电流、SOC、温度
- 单体电压
- SystemStatus
- 断连恢复

### P1：工程能力

- 保护参数语义化
- 事件日志语义化
- 参数写后回读
- CSV/JSON 导出
- 原始寄存器/原始帧工具

### P2：产品化

- 危险写授权
- BLE 安全会话
- OTA 跨平台统一
- 用户模式 / 工程模式权限隔离

## 6. 当前已完成的回归入口

- Python/Qt：`python script/bms_client_asset_tool.py verify`
- Android：`gradlew :app:testDebugUnitTest`
- 微信 TypeScript：`tests/protocol_smoke.ts`
- Swift：当前先做源码语法与真机 CoreBluetooth 验收；后续补统一 XCTest 向量

## 7. AI 开发规则

AI 继续开发时：

- MUST 复用已有客户端，不要另起第五套协议实现。
- MUST 保持 `Connected` 和 `Ready` 两个状态分离。
- MUST 保持业务请求串行。
- MUST 更新共享 JSON 后再更新平台常量。
- MUST 为协议变更补测试向量。
- MUST 对真实板子无法自动验证的项明确标记为“待实机验证”。
- MUST NOT 因 UI 方便而绕过 CRC、长度或请求上限检查。
- MUST NOT 把当前无 SMP 安全的工程写操作直接定义为正式客户能力。
