# BMS Client Platform

> 给开发人员和 AI/Codex 的跨平台客户端入口。普通 BLE 协议先读 `BMS_BLE_通信对接规范_V1.0.md`，OTA 先读 `BMS_OTA_通信规范_V1.0.md`；发布方式、费用、各端 UI 和 OTA 完成度读 `BMS_CLIENT_RELEASE_UI_GUIDE.md`。

## 1. 单一真源

任何客户端修改前必须优先检查：

1. `docs/BMS_BLE_通信对接规范_V1.0.md`
2. `docs/BMS_OTA_通信规范_V1.0.md`
3. `docs/BMS_CLIENT_RELEASE_UI_GUIDE.md`
4. `docs/register_catalog.json`
5. `docs/protocol_test_vectors.json`
6. `docs/ota_test_vectors.json`
7. `docs/generated/`

禁止在新客户端里重新定义一套与上述文件无关的 UUID、寄存器地址、单位、状态位或 OTA wire protocol。

## 2. 当前客户端矩阵

| 客户端 | 路径 | 技术栈 | 普通 BLE | OTA V1 | 当前验收状态 |
|---|---|---|---|---|---|
| Windows/macOS/Linux 上位机 | `vendor/ble_sample/BMSAssistantQt` | Python + PySide6 + QtBluetooth | 已实现 | 已实现独立 OTA 工具 | 协议/CI，待目标 OS + BMS 实机 |
| Android App | `vendor/ble_sample/BMSAssistantAndroid` | Kotlin + Jetpack Compose | 已实现 | 已实现 `BMS OTA` Activity | Gradle 单测/CI，待 Android + BMS 实机 |
| macOS Swift App | `vendor/ble_sample/BMSAssistant` | SwiftUI + CoreBluetooth | 已实现 | 当前桌面 OTA 以 Qt 工具为主 | 普通 BLE 待实机 |
| iPhone/iPad App | `vendor/ble_sample/BMSAssistant` | SwiftUI + CoreBluetooth | 已实现移动 UI | 已实现 OTA Tab + CoreBluetooth OTA session | Swift parse/CI，待 Xcode App 工程与真机签名运行 |
| 微信小程序 | `vendor/ble_sample/BMSAssistantMiniProgram` | 原生 TypeScript | 已实现 | 已实现 OTA 页面 + 独立 OTA transport | TypeScript/OTA smoke CI，待微信真机 BLE |

“已实现”表示代码路径已具备，不等于完成真实硬件验收。最终交付必须通过目标设备实机测试。

## 3. 普通 BLE 各平台必须一致的行为

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

## 4. OTA V1 各平台必须一致的行为

OTA Service：

```text
00010203-0405-0607-0809-0A0B0C0D1912
```

OTA Characteristic：

```text
00010203-0405-0607-0809-0A0B0C0D2B12
```

统一基线：

- Telink Legacy OTA
- 16-byte firmware PDU
- 20-byte OTA data packet
- little-endian index/opcode
- CRC16/IBM(Modbus)，init `0xFFFF`、poly `0xA001`
- `.bin` 偏移 `0x08` 检查 `KNLT`
- `.bin` 偏移 `0x18` 读取 uint32 little-endian firmware size
- Start -> Data(index 0..N-1) -> End -> OTA_RESULT
- 首版可靠性优先，使用有回复写/串行写
- `OTA_RESULT=0x00` 才能判定 OTA 成功
- OTA session 期间停止普通 Modbus 业务请求

## 5. 开发顺序

修改普通协议或寄存器时：

```text
firmware
  -> register_catalog.json
  -> protocol_test_vectors.json
  -> generated assets
  -> Qt / Android / Swift / MiniProgram
  -> protocol regression
  -> real-device BLE validation
```

修改 OTA 时：

```text
firmware OTA server
  -> BMS_OTA_通信规范_V1.0.md
  -> ota_test_vectors.json
  -> Python/Kotlin/Swift/TypeScript OTA codec
  -> platform OTA transport/UI
  -> CI
  -> real-device OTA validation
```

## 6. 当前优先级

### P0：实机闭环

- 微信小程序扫描/连接/状态读取
- Qt OTA 真机升级
- Android OTA 真机升级
- iPhone OTA 真机升级
- OTA 成功后重新连接并核对软件版本
- 断电/断连/错误 bin/CRC 错误等失败场景

### P1：BMS 业务完整性

- 保护参数 65-word 完整语义化
- 事件日志语义化
- 参数写后回读与范围校验
- CSV/JSON/升级日志导出

### P2：产品化

- 危险写授权
- BLE SMP 或应用层鉴权
- 固件签名/可信升级
- 用户模式 / 工程模式权限隔离
- OTA 断点/恢复策略（如产品需要）
- OTA 吞吐优化（实机稳定后再评估 MTU/DLE/Write Without Response）

## 7. 当前回归入口

- GitHub Actions：`.github/workflows/bms-client-ci.yml`
- Python/Qt 普通协议：`python script/bms_client_asset_tool.py verify`
- Python/Qt OTA：`vendor/ble_sample/BMSAssistantQt/tests/test_ota_codec.py`
- Android：`gradlew :app:testDebugUnitTest`
- 微信普通协议：`BMSAssistantMiniProgram/tests/protocol_smoke.ts`
- 微信 OTA：`BMSAssistantMiniProgram/tests/ota_smoke.ts`
- Swift OTA：CI 先执行 `swiftc -parse`；真机行为由 Xcode/CoreBluetooth 验收

## 8. AI 开发规则

AI 继续开发时：

- MUST 复用已有客户端，不要另起第五套协议实现。
- MUST 保持普通 `Connected` 和 `Ready` 两个状态分离。
- MUST 保持普通业务请求串行。
- MUST 保持 OTA 独立于普通 Modbus pending request。
- MUST 更新共享文档/JSON 后再更新平台实现。
- MUST 为协议变更补测试向量。
- MUST 对真实板子无法自动验证的项明确标记为“待实机验证”。
- MUST NOT 因 UI 方便而绕过 CRC、长度、固件 header 或请求上限检查。
- MUST NOT 把当前无 SMP 安全的工程写操作/OTA直接定义为正式客户能力。
