# BMS BLE 通信对接规范 V1.0

> 适用对象：Windows/macOS/Linux 上位机、Android App、iOS/iPadOS App、微信小程序、自动化测试工具、AI/Codex 开发代理。
>
> 本文以当前固件 `vendor/ble_sample` 的实际源码行为为准。客户端不得根据历史注释、Characteristic 名称或旧上位机行为自行推断协议。

## 1. 文档状态

- 协议版本：`V1.0`
- 固件基线：`codex-new-new-master-no-ide-toolchain`
- 当前 BMS 类型：`D3PRO`
- 当前串数：`SeriesNum = 10`
- BLE 安全：`BLE_APP_SECURITY_ENABLE = 0`
- BLE OTA：`BLE_OTA_SERVER_ENABLE = 1`
- 默认 ATT MTU：`23`
- 单次请求安全上限：`20 byte`
- 响应 Notify 分片：`20 byte`

### 1.1 规范性关键词

本文中的以下关键词具有强约束含义：

- **MUST**：客户端必须实现。
- **MUST NOT**：客户端禁止这样实现。
- **SHOULD**：强烈建议实现，除非平台有明确限制。
- **MAY**：可选能力。

### 1.2 AI/Codex 首要规则

任何后续 AI 在开发 BMS 客户端时：

1. **MUST** 先读取本文、`register_catalog.json`、`protocol_test_vectors.json`。
2. **MUST** 把 BLE 视为字节传输层，把 Modbus RTU 视为业务协议层。
3. **MUST** 使用 `6E400002...` 作为客户端写入请求入口。
4. **MUST** 使用 `6E400003...` 作为 BMS Notify 响应出口。
5. **MUST** 对 Notify 做完整 Modbus 帧重组与 CRC 校验。
6. **MUST** 保持同一时刻只有一条在途业务请求。
7. **MUST NOT** 依赖 Characteristic 历史名称判断方向。
8. **MUST NOT** 把标准 Battery Service 当作主 BMS 数据源。
9. **MUST NOT** 假设一次 Notify 就等于一帧 Modbus 响应。
10. **MUST NOT** 在当前固件上发送超过 20 byte 的单次业务请求。

## 2. 源码真源

客户端协议事实主要来自以下文件：

- `vendor/ble_sample/app.c`
- `vendor/ble_sample/app_att.c`
- `vendor/ble_sample/app_att.h`
- `vendor/ble_sample/modbus_rtu.c`
- `vendor/ble_sample/modbus_rtu.h`
- `vendor/ble_sample/btname_modbus.c`
- `vendor/ble_sample/btname_modbus.h`
- `vendor/ble_sample/bms_event_log.c`
- `vendor/ble_sample/bms_event_log.h`
- `vendor/ble_sample/conf.h`
- `docs/register_catalog.json`
- `docs/protocol_test_vectors.json`

跨平台客户端共享资产：

- `docs/register_catalog.json`：寄存器、UUID、单位、状态位、BLE 约束的结构化真源。
- `docs/protocol_test_vectors.json`：请求、响应、CRC、分片重组、电池快照的回归测试向量。
- `script/bms_client_asset_tool.py`：多语言客户端资产生成与校验入口。

## 3. 总体架构

当前业务通信不是“每个数据一个 Characteristic”，而是：

```text
App / 小程序 / 上位机
        |
        | BLE GATT
        v
Telink SPP Service
        |
        | 原始字节流
        v
Modbus RTU frame
        |
        v
modbus_on_frame()
        |
        +--> BMS 寄存器读取
        +--> BMS 参数写入
        +--> 状态/日志/控制
```

UART 链路也复用 `modbus_on_frame()`，因此 BLE 与 UART 的业务协议是一致的。

### 3.1 推荐客户端分层

```text
UI / ViewModel
      |
Application Service
      |
Protocol Core
  - ModbusCodec
  - RegisterCatalog
  - ResponseAccumulator
      |
Transport Session
      |
Platform BLE Adapter
```

平台只应该差异化实现 BLE API 与 UI；CRC、寄存器、单位、状态位、业务动作、错误模型必须保持一致。

## 4. BLE 广播

### 4.1 广播类型

- 类型：Connectable Undirected Advertising
- 地址类型：Public Address
- 广播间隔：约 `800 ms`
- 发射功率配置：`RF_POWER_P3dBm`

### 4.2 ADV 内容

当前主广播包含：

- Flags：`0x05`
- Appearance：`0x0180`
- Incomplete 16-bit Service UUID：
  - `0x1812`
  - `0x180F`

注意：当前代码虽然在 ADV 中广播 `0x1812`，但 HID Service 实际未启用。

客户端 **MUST NOT** 仅凭 `0x1812` 判断设备具有 HID 业务能力。

### 4.3 Scan Response

设备完整名称放在 Scan Response 的 `Complete Local Name` 字段中。

编译期默认值示例：

```text
BT_FD190126F03200046_007
```

运行时 `btname_init()` 会从持久化存储加载后缀，并刷新实际广播名。

客户端设备发现建议按以下优先级识别：

1. Local Name / Peripheral Name 以 `BT_` 开头；
2. 再结合 `0x180F` / `0x1812` 作为弱特征；
3. 最终连接后必须以目标自定义 SPP Service 是否存在作为能力确认。

## 5. GATT 服务与 Characteristic

### 5.1 业务 SPP Service

| 用途 | UUID | 客户端动作 |
|---|---|---|
| SPP Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | Discover |
| Request Characteristic | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write / Write Without Response |
| Response Characteristic | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Notify |

### 5.2 真实方向

当前源码中的历史变量名/描述文本与真实行为相反。

**唯一允许的客户端实现：**

```text
Client -> BMS:
write 6E400002-B5A3-F393-E0A9-E50E24DCCA9E

BMS -> Client:
notify 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

客户端 **MUST NOT** 根据以下历史名称决定收发方向：

- `Server2Client`
- `Client2Server`
- `Telink SPP: Module->Phone`
- `Telink SPP: Phone->Module`

### 5.3 Ready 条件

客户端只有在以下条件全部满足后才能认为业务链路 `READY`：

1. BLE 物理连接成功；
2. 发现 SPP Service；
3. 发现 Request Characteristic；
4. 发现 Response Characteristic；
5. Response Characteristic Notify 订阅成功；
6. 平台收到订阅完成回调。

`Connected != Ready`。

## 6. 连接参数

当前连接建立后，固件会请求正常模式连接参数：

- Interval Min：`10 ms`
- Interval Max：`10 ms`
- Slave Latency：`99`
- Supervision Timeout：`4 s`

OTA 模式会请求：

- Interval：`10 ms`
- Slave Latency：`0`
- Supervision Timeout：`4 s`

客户端不应把 UI 刷新频率绑定到 BLE Connection Interval；iOS/Android/微信最终是否接受参数由系统 BLE 栈决定。

## 7. MTU、请求长度与 Notify 分片

### 7.1 当前限制

- 默认 ATT MTU：`23`
- ATT Value 安全业务载荷：`20 byte`
- 固件请求侧重组：**不支持**
- 固件响应侧分片：**支持**，固定每片最多 `20 byte`

### 7.2 请求规则

每一条 Modbus RTU 请求 **MUST** 完整放入一次 GATT Write。

当前安全规则：

```text
request.length <= 20 bytes
```

典型长度：

- `0x03` 读寄存器：8 byte
- `0x06` 写单寄存器：8 byte
- `0x10` 写多寄存器：`9 + 2 * qty` byte

因此当前 `0x10`：

```text
qty <= 5
```

### 7.3 响应规则

固件 `notify_big_packet()` 会将完整 Modbus 响应按 20 byte 切成多个 Notify。

客户端 **MUST** 把每次 Notify 当作 `fragment`，而不是完整帧。

### 7.4 完整帧长度推断

| 功能码 | 响应总长度 |
|---|---|
| `0x03` | `3 + byteCount + 2` |
| `0x06` | `8` |
| `0x10` | `8` |
| 异常响应 | `5` |
| `0x7F` Echo | 与请求长度一致 |

客户端 **MUST** 使用“功能码 + 长度字段 + CRC”判断帧完整，不应使用 Notify 间隔超时作为主要帧边界判据。

## 8. Modbus RTU over BLE

### 8.1 基本帧

```text
[addr][func][payload...][crc_lo][crc_hi]
```

- 设备地址：`0x01`
- 广播地址：`0x00`
- CRC：CRC16/MODBUS
- CRC 初值：`0xFFFF`
- 多项式：`0xA001`
- CRC 帧尾顺序：低字节在前

### 8.2 支持功能码

| 功能码 | 含义 | 当前状态 |
|---|---|---|
| `0x03` | Read Holding Registers | 支持 |
| `0x06` | Write Single Register | 支持 |
| `0x10` | Write Multiple Registers | 支持 |
| `0x7F` | Echo / Link Test | 自定义支持 |

不支持的功能码会返回 Modbus 异常响应：

```text
[addr][func | 0x80][0x01][crc_lo][crc_hi]
```

## 9. 标准请求示例

### 9.1 Echo

请求：

```text
01 7F 12 34 56 78 6F 34
```

期望响应：完全相同。

### 9.2 读取实时状态窗口

```text
01 03 D1 20 00 0B 3C FB
```

读取 `0xD120` 起 11 个寄存器。

### 9.3 读取系统状态

```text
01 03 D1 15 00 02 EC F3
```

### 9.4 写入 SOC=60

```text
01 06 10 05 00 3C 9D 1A
```

### 9.5 写多寄存器

`0x10` 请求必须重新计算 CRC，且总长度不得超过 20 byte。

## 10. 主要寄存器地图

结构化完整定义以 `docs/register_catalog.json` 为准。

### 10.1 设备身份

| 地址 | 数量 | 含义 |
|---|---:|---|
| `0x0000` | 3 word | Public MAC |
| `0x0100` | 当前读 12 word | 蓝牙名称窗口 |
| `0xC002` | 16 word | 产品序列号 |
| `0xC012` | 16 word | 硬件版本 |
| `0xC022` | 16 word | 软件版本 |

字符串按每个寄存器高字节在前拼接，遇 `0x00` 截断。

### 10.2 实时状态窗口（优先使用）

基地址：`0xD120`

| 地址 | 字段 | 原始单位 | 客户端解码 |
|---|---|---|---|
| `0xD120` | magic | raw | 必须为 `0x4253` |
| `0xD121` | protocol_version | raw | 当前 `0x0001` |
| `0xD122` | pack_voltage | 0.01V | `raw / 100.0` V |
| `0xD123` | signed_current | int16, 0.1A | `int16(raw) / 10.0` A |
| `0xD124` | SOC | % | `raw` |
| `0xD125` | max_temp | 0.1C + 40C offset | `raw / 10.0 - 40.0` C |
| `0xD126` | min_temp | 同上 | 同上 |
| `0xD127` | MOS temp | 同上 | 同上 |
| `0xD128` | max_cell | mV | raw mV |
| `0xD129` | min_cell | mV | raw mV |
| `0xD12A` | cell_delta | mV | raw mV |

客户端读取实时窗口后：

- `magic == 0x4253`：优先采用实时窗口。
- magic 不匹配：回退 Legacy 区。

### 10.3 Legacy 状态区

基地址：`0xD000`，当前客户端读取 63 word。

当前 D3PRO 主要字段：

| 地址/索引 | 含义 |
|---|---|
| `0xD000..0xD009` | Cell1..Cell10 mV |
| `0xD01D` | 电池温度 ADC mV 镜像 |
| `0xD01E` | MOS 温度 ADC mV 镜像 |
| `0xD01F` | 总压 ADC mV 镜像 |
| `0xD020` | 最大单体 mV |
| `0xD021` | 最小单体 mV |
| `0xD022` | 最大单体位置 |
| `0xD023` | 最小单体位置 |
| `0xD024` | 压差 mV |
| `0xD025` | 总压工程量 0.01V |
| `0xD026..0xD02F` | 温度数组 |
| `0xD030` | 最大温度 |
| `0xD031` | 最小温度 |
| `0xD032` | 充电电流原始值 |
| `0xD033` | 放电电流原始值 |
| `0xD034` | SOC |
| `0xD035` | SOH |
| `0xD036` | 当前容量 0.01Ah |
| `0xD037` | 满充容量 0.01Ah |
| `0xD038` | 出厂容量 0.01Ah |
| `0xD039` | 循环次数 |

Legacy 区属于固件内部结构的兼容平铺，不建议作为长期新协议扩展入口。

## 11. SystemStatus

地址：

- `0xD115`：低 16 bit
- `0xD116`：高 16 bit

组合方式：

```text
systemStatusRaw = lowWord | (highWord << 16)
```

当前共享目录定义的主要位：

| Bit | 含义 |
|---:|---|
| 0 | 启动完成 |
| 1 | 预充 MOS |
| 2 | 充电 MOS |
| 3 | 放电 MOS |
| 4 | 预充继电器 |
| 5 | 充电继电器 |
| 6 | 放电继电器 |
| 7 | 主继电器 |
| 8 | 加热 |
| 9 | 冷却 |
| 10 | AFE1 |
| 11 | AFE2 |
| 12 | 均衡 |
| 13 | 待休眠 |
| 14 | BMS 关断输出 |
| 15 | 加热关闭输出 |
| 18 | 外部驱动控制 |

完整、后续可扩展定义以 `register_catalog.json` 为准。

## 12. 保护参数

保护参数主区：`0x2100..0x2140`，65 word，可读写。

每组一般按以下 5 个寄存器排列：

```text
First
Second
Third
Rcv
Filter
```

分组：

| 地址范围 | 项目 |
|---|---|
| `0x2100..0x2104` | Vcell OVP |
| `0x2105..0x2109` | Vcell UVP |
| `0x210A..0x210E` | Vbus OVP |
| `0x210F..0x2113` | Vbus UVP |
| `0x2114..0x2118` | Ichg OCP |
| `0x2119..0x211D` | Idsg OCP |
| `0x211E..0x2122` | Tchg OTP |
| `0x2123..0x2127` | Tchg UTP |
| `0x2128..0x212C` | Tdsg OTP |
| `0x212D..0x2131` | Tdsg UTP |
| `0x2132..0x2136` | Tmos OTP |
| `0x2137..0x213B` | Vdelta OVP |
| `0x213C..0x2140` | SOC Low |

保护参数属于危险写操作。正式客户版 UI **SHOULD** 增加权限、确认、范围校验和写后回读。

## 13. 事件日志

### 13.1 入口

- 读取入口：`0xC008`
- 最大记录数量：100
- 清除寄存器：`0x1007`
- 清除命令：写 `0x0001`

每个日志寄存器：

```text
high byte = event id
low byte  = interval code
```

### 13.2 当前 Event ID

```text
0  NULL
1  BMS_START_UP
2  BMS_SLEEP
3  BALANCE_OPEN
4  HEAT_OPEN
5  COOL_OPEN
6  VCELL_OVP
7  VBUS_OVP
8  CHG_OCP
9  VCELL_UVP
10 VBUS_UVP
11 DSG_OCP
12 CHG_UTP
13 DSG_UTP
14 CHG_OTP
15 DSG_OTP
16 VDELTA_OP
17 CBC_ERR
18 AFE1_ERR
19 AFE2_ERR
20 EEPROM_ERR
```

### 13.3 时间间隔编码

当前日志低字节：

- 启动事件：`0`
- 间隔 `<= 60 s`：`171`
- `>60 s` 且 `<=168 h`：按小时向上取整，值 `1..168`
- 更长：`170`

### 13.4 兼容风险

`0xC008` 同时位于产品信息的历史地址范围内。

固件对“从 `0xC008` 开始读取”做事件日志特殊处理。

客户端 **MUST** 将事件日志作为独立业务动作，不得通过跨越 `0xC008` 的产品信息批量读取推断结果。

## 14. 蓝牙名称

### 14.1 固件规则

- 固定前缀：`BT_`
- 持久化内容：suffix
- 完整名称最大长度：25 byte
- suffix 最大长度：22 byte
- 允许字符：`A-Z a-z 0-9 _ -`

### 14.2 写入语义

客户端 UI 应让用户输入 **suffix**，例如：

```text
DEMO01
```

固件最终广播：

```text
BT_DEMO01
```

不要把 `BT_` 再作为 suffix 发送，否则会得到重复前缀语义。

### 14.3 当前兼容限制

- 读取窗口：12 word
- 写入窗口宏：16 word
- BLE 单包约束导致一次 `0x10` 安全写入最多 5 word / 10 byte 数据

因此客户端第一版 **SHOULD** 限制 suffix 每次写入不超过 10 个 ASCII byte。

## 15. 写操作与风险分级

### 15.1 普通写动作

| 地址 | 动作 |
|---|---|
| `0x0100` | 蓝牙名 suffix |
| `0x1005` | 写 SOC |
| `0x2100..0x2140` | 保护参数 |
| `0x2319` | 循环次数相关 |

### 15.2 调试/危险动作

| 地址 | 值 | 说明 |
|---|---|---|
| `0x1007` | `0x0001` | 清除事件日志 |
| `0x1102` | `0x0003` | 重新进入工厂模式相关逻辑 |
| `0x1102` | `0x000A` | deep sleep 调试标志 |
| `0x1103` | `0x0003` | 当前保留调试动作 |

客户版 App/小程序 **MUST NOT** 默认暴露所有调试写操作。

工程上位机 MAY 提供，但必须显式标注“工程/危险操作”。

## 16. 安全边界

当前：

```text
BLE_APP_SECURITY_ENABLE = 0
```

即 BLE SMP 安全未启用。

因此当前协议的危险写操作不存在足够的产品级访问控制。

正式量产客户版在开放以下动作前应增加安全层：

- 保护参数写入
- MOS/控制类写入
- 调试寄存器
- 日志清除
- OTA

推荐后续协议层至少增加：

1. 权限/会话认证；
2. 危险写命令授权；
3. 防重放或随机挑战；
4. 写前范围校验；
5. 写后回读确认。

在上述机制完成前，客户端应通过 UI 权限隐藏降低误操作风险，但这不能替代真正的协议安全。

## 17. OTA

当前固件启用 Telink OTA Server。

### 17.1 OTA GATT

| 用途 | UUID |
|---|---|
| OTA Service | `00010203-0405-0607-0809-0A0B0C0D1912` |
| OTA Characteristic | `00010203-0405-0607-0809-0A0B0C0D2B12` |

OTA Characteristic 支持 Write / Write Without Response / Notify。

### 17.2 OTA 超时

- OTA Process Timeout：`180 s`
- OTA Data Packet Timeout：`15 s`

OTA 开始时固件会将连接 latency 调整为 0，结束后恢复正常模式。

### 17.3 客户端策略

业务通信与 OTA 应拆为不同 Service/流程。

首版 BMS 客户端可先完成普通业务通信，再独立接入 Telink OTA；不得把 OTA 数据塞入 Modbus SPP Characteristic。

## 18. 统一客户端命令模型

### 18.1 强制串行

当前协议没有 Transaction ID。

客户端 **MUST** 保持：

```text
同一时刻最多 1 条 pending request
```

推荐状态机：

```text
Idle
  -> Sending
  -> WaitingFragments
  -> FrameComplete
  -> CRCVerify
  -> Parse
  -> Complete
  -> Idle
```

失败状态：

```text
Timeout
CRCError
ProtocolError
BLEDisconnected
```

### 18.2 多步业务动作

例如“刷新电池状态”：

1. 读 `0xD000` 63 word；
2. 等完整响应；
3. 读 `0xD115` 2 word；
4. 等完整响应；
5. 读 `0xD120` 11 word；
6. 等完整响应；
7. 生成统一 `BatteryStatusSnapshot`。

不得并发发送三条读请求。

## 19. 推荐统一领域模型

所有平台至少统一：

```text
DiscoveredDevice
ConnectionStatus
DeviceIdentitySnapshot
BatteryStatusSnapshot
RegisterBlock
ExchangeLogEntry
```

推荐 `BatteryStatusSnapshot` 字段：

```text
source
protocolVersion
packVoltage
signedCurrent
soc
soh
maxTemp
minTemp
mosTemp
maxCellVoltage
minCellVoltage
cellDelta
maxCellPosition
minCellPosition
capacityNow
capacityFull
capacityFactory
cycleCount
cellVoltages[]
systemStatusRaw
activeStatusFlags[]
updatedAt
```

## 20. 平台实现映射

### 20.1 Android

推荐：

- Kotlin
- `BluetoothLeScanner`
- `BluetoothGatt`
- Jetpack Compose

流程：扫描 -> connectGatt -> discoverServices -> setCharacteristicNotification -> 写 CCCD -> Ready -> Modbus。

### 20.2 iOS/iPadOS/macOS

推荐：

- Swift
- SwiftUI
- CoreBluetooth

流程：`CBCentralManager` -> `CBPeripheral` -> Discover Service -> Discover Characteristics -> `setNotifyValue(true)` -> Ready。

### 20.3 Windows/macOS/Linux 上位机

当前主线：

- Python
- PySide6
- QtBluetooth
- QtWidgets

工程版允许额外提供：

- 原始寄存器读取
- 原始 Hex
- Echo
- 日志导出
- 危险写操作
- BLE/UART 双 transport

### 20.4 微信小程序

推荐原生 TypeScript。

核心 API：

```text
wx.openBluetoothAdapter
wx.startBluetoothDevicesDiscovery
wx.onBluetoothDeviceFound
wx.createBLEConnection
wx.getBLEDeviceServices
wx.getBLEDeviceCharacteristics
wx.notifyBLECharacteristicValueChange
wx.onBLECharacteristicValueChange
wx.writeBLECharacteristicValue
```

微信小程序必须复用相同的：

- UUID
- CRC16/MODBUS
- ModbusCodec
- ResponseAccumulator
- RegisterCatalog
- BatteryStatusSnapshot

## 21. 客户端错误模型

统一分成：

1. PlatformError：蓝牙权限、蓝牙关闭、平台 API 错误；
2. ConnectionError：连接失败、Service/Characteristic 缺失；
3. ProtocolError：CRC、长度、功能码、Modbus Exception；
4. BusinessError：寄存器不支持、请求过长、当前 Busy；
5. UserInputError：地址、Hex、参数范围错误。

## 22. 日志规范

每个平台建议统一日志格式：

```text
timestamp
direction: TX | RX | INFO | ERR
title
payloadHex
note
```

其中 RX 应记录“完整重组后的帧”，可选同时保留每个 Notify fragment 供工程调试。

## 23. 回归测试最低要求

每个平台实现完成后至少通过：

1. CRC16/MODBUS 固定向量；
2. `0x7F` Echo；
3. `0x03` 短响应；
4. `0x03` 多 Notify 长响应；
5. `0x06` 写单寄存器；
6. `0x10` 写 1~5 word；
7. CRC 错误拒绝；
8. Modbus Exception 解析；
9. `0xD120` 实时窗口解码；
10. Legacy fallback；
11. 断连时清空 pending request；
12. Ready 前禁止发业务命令。

测试向量统一来自：

```text
docs/protocol_test_vectors.json
```

## 24. 当前已知协议问题

以下是 V1.0 必须兼容、未来应修复的问题：

1. SPP Characteristic 历史命名与真实方向相反；
2. ADV 宣称 `0x1812`，但 HID Service 未启用；
3. 蓝牙名读 12 word、写窗口 16 word，不一致；
4. `0xC008` 同时存在产品信息历史重叠与事件日志特殊入口；
5. 请求不支持跨 GATT Write 重组；
6. 响应分片长度固定 20 byte，未使用协商后的动态 MTU；
7. BLE SMP 当前关闭；
8. Legacy 状态区直接暴露内部结构，不适合作为长期扩展协议；
9. 当前协议没有 Transaction ID，只适合严格串行请求模型。

## 25. V2 演进原则

后续协议升级优先保持客户端业务模型稳定，而不是让 UI 跟着固件内部结构变化。

建议 V2：

- 增加明确协议版本/能力位；
- 修复 SPP 命名；
- 清理广播 UUID；
- 独立事件日志地址区；
- 统一蓝牙名窗口；
- 动态 MTU；
- 支持请求分片或应用层帧；
- 增加安全会话与危险写授权；
- 新增稳定的业务数据窗口；
- 为 OTA 定义跨平台统一客户端流程。

## 26. 开发验收定义

一个客户端只有满足以下条件才算“协议层完成”：

- 能稳定扫描并识别 BMS；
- 能建立 BLE 连接并进入 Ready；
- Echo 连续测试通过；
- 能读取实时状态；
- 能读取全部单体电压；
- 能正确显示总压、电流、SOC、温度、压差；
- 能显示系统状态；
- 能正确重组多 Notify 响应；
- CRC 错误不会进入业务层；
- 所有业务请求严格串行；
- 超时、断连、重连状态可恢复；
- 通过 `protocol_test_vectors.json` 对应测试；
- 用户版与工程版危险功能边界清晰。

---

## 附录 A：客户端最小伪代码

```text
scan()
connect(device)
discover SPP service
discover request/response characteristics
subscribe response notify
mark READY

request(frame):
    assert READY
    assert no pending request
    assert len(frame) <= 20
    clear ResponseAccumulator
    write request characteristic

onNotify(fragment):
    append fragment
    expectedLen = inferExpectedLength()
    if complete:
        verify CRC
        parse Modbus response
        resolve pending request
```

## 附录 B：共享资产修改规则

修改协议、寄存器或单位时，推荐顺序：

1. 修改固件；
2. 修改 `docs/register_catalog.json`；
3. 修改/增加 `docs/protocol_test_vectors.json`；
4. 运行 `python script/bms_client_asset_tool.py all`；
5. 更新本文；
6. 更新各平台客户端；
7. 跑同一套测试向量；
8. 实机 BLE 联调。

任何平台代码中的寄存器常量都不应成为新的“第二真源”。
