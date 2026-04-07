# BLE 部分梳理与客户端对接文档

## 1. 文档目的

本文用于梳理当前项目 `vendor/ble_sample` 的 BLE 实现，输出一份可直接支撑以下工作的中文技术文档：

- iOS App
- iPad App
- macOS App
- 上位机调试工具
- 日常联调、测试、故障定位

本文把内容分成两类：

- 事实：直接来自当前固件源码。
- 建议：面向客户端落地、测试调试和后续固件演进的工程建议。

## 2. 分析范围与依据

### 2.1 分析范围

本次仅梳理当前工程 `vendor/ble_sample` 相关 BLE 与 BLE 上层协议部分，不扩展到全部 BMS 算法细节。

### 2.2 主要依据文件

- `vendor/ble_sample/app.c`
- `vendor/ble_sample/app_att.c`
- `vendor/ble_sample/app_att.h`
- `vendor/ble_sample/modbus_rtu.c`
- `vendor/ble_sample/modbus_rtu.h`
- `vendor/ble_sample/modbus_uart.c`
- `vendor/ble_sample/btname_modbus.c`
- `vendor/ble_sample/btname_modbus.h`
- `vendor/ble_sample/conf.h`
- `vendor/ble_sample/param.h`
- `vendor/ble_sample/sci_upper.h`
- `vendor/ble_sample/bms_event_log.h`
- `vendor/ble_sample/runtime.c`
- `stack/ble/service/uuid.h`
- `vendor/common/app_buffer.h`

### 2.3 当前固件构建假设

以下内容是事实：

- `FD_BMS_TYPE` 当前编译为 `D3PRO`
- `SeriesNum` 当前配置为 `10`
- `BLE_APP_SECURITY_ENABLE = 0`
- `BLE_OTA_SERVER_ENABLE = 1`
- `PM_DEEPSLEEP_RETENTION_ENABLE = 0`

以下内容属于工程假设：

- 你当前要对接的目标设备就是这份固件对应的产物，而不是其他 `vendor/*` 变体。
- 上位机需要同时兼容 BLE 和 UART/串口调试路径。

## 3. 一页结论

### 3.1 核心结论

1. 这不是“自定义 App 协议帧”，而是 `Modbus RTU frame over BLE characteristic`。
2. BLE 只是传输层，业务解析核心统一走 `modbus_on_frame()`，UART 也复用同一套解析逻辑。
3. 真正用于业务收发的是 Telink SPP 自定义服务，不是 Battery Service，不是 HID。
4. 当前固件里 `characteristic 名称/描述` 与 `真实收发方向` 是反的，客户端不能按名字理解。
5. 当前 `MTU_SIZE_SETTING` 未覆盖，默认 MTU 是 `23`，所以单次写入的实际安全负载只有 `20 byte`。
6. 响应支持分片通知，但请求侧没有重组逻辑，因此 `BLE 写请求必须控制在单包内`。
7. 当前协议非常适合做成“一个共享协议核心 + 多传输适配器”的架构：
   - `CoreBluetooth` 适配 iOS/iPad/mac
   - `Serial/UART` 适配上位机

### 3.2 客户端首先要记住的 4 件事

1. 写请求：写到 `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
2. 收响应：订阅 `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
3. 数据体：直接发 `Modbus RTU` 原始字节流，包含地址、功能码、寄存器、CRC
4. 大响应：按通知流自己重组，不能假设一次通知就是一帧

## 4. BLE 架构总览

## 4.1 模块关系

当前 BLE 数据链路如下：

`CoreBluetooth / 手机 / 上位机`
-> `Telink SPP Characteristic Write`
-> `module_onReceiveData()`
-> `modbus_on_frame()`
-> `notify_big_packet()`
-> `Telink SPP Characteristic Notify`

UART 链路如下：

`串口主机`
-> `modbus_uart_poll()`
-> `modbus_on_frame()`
-> `modbus_uart_send()`

结论：

- BLE 和 UART 在业务层完全共用 `modbus_on_frame()`。
- 你应该把 `Modbus codec` 做成独立模块，BLE/UART 仅作为 transport adapter。

## 4.2 初始化顺序

`user_init_normal()` 中与本文相关的顺序为：

1. BLE stack 初始化
2. 广播参数设置
3. 广播数据/扫描响应设置并开启广播
4. `LoadParam()`
5. `bms_event_log_init()`
6. `soc_kv_store_init()`
7. `btname_init()`
8. `bms_event_log_note_startup()`
9. `Runtime_Init()`
10. `WriteProID_Default()`

注意：

- 编译期默认广播名先被设置。
- 随后 `btname_init()` 会从 Flash 中读取蓝牙名并重新刷新 Scan Response。
- 所以设备稳定运行后的可见名称，以 Flash 中持久化名字为准；若无有效数据，则退回 `BT_DEFAULT` 风格名字。

## 5. 广播、连接、低功耗行为

## 5.1 广播内容

### 广播包 ADV

- Flags = `0x05`
- Appearance = `0x0180`
- 16-bit UUID 列表：
  - `0x1812` HID
  - `0x180F` Battery

### 扫描响应 Scan Response

- 完整本地名 `Complete Local Name`
- 初始编译值：`DEV_NAME_STR = "BT_FD190126F03200046_007"`
- 实际运行值：会被 `btname_init()` 读 Flash 后覆盖

### 事实性结论

- App 扫描时可以按名字筛选。
- 不建议只靠广播 UUID 做业务筛选，因为当前 ADV 中宣称了 `0x1812 HID`，但实际 GATT 没有启用 HID 服务。

## 5.2 广播参数

- 广播类型：`ADV_TYPE_CONNECTABLE_UNDIRECTED`
- 广播间隔：`800 ms`
- 地址类型：`Public Address`
- 发射功率：`RF_POWER_P3dBm`

## 5.3 连接参数

连接建立后，固件会主动请求：

- Interval Min = `10 ms`
- Interval Max = `10 ms`
- Latency = `99`
- Timeout = `4 s`

这组参数的设计意图不是“高实时”，而是“低功耗优先，允许从机跳过较多 connection event”。

客户端结论：

- 不要把 UI 刷新频率、采样节奏绑定到 BLE 连接间隔。
- iOS 最终是否接受这组参数，由系统决定，客户端应按“实际回包时间”驱动超时逻辑。

## 5.4 MTU 与通知分片

### 事实

- `ble_sample` 没有重定义 `MTU_SIZE_SETTING`
- `vendor/common/app_buffer.h` 默认 `MTU_SIZE_SETTING = 23`
- `notify_big_packet()` 固定用 `20 byte` 做通知切片

### 直接影响

- 单次 BLE 写入的安全业务负载：`20 byte`
- 大响应由固件拆成多个 `20 byte notify`
- 请求侧没有分片重组，所以长请求不可直接使用

### 工程结论

- `0x03` 读寄存器：请求固定 8 字节，安全
- `0x06` 写单寄存器：请求固定 8 字节，安全
- `0x10` 写多寄存器：请求总长为 `9 + 2 * qty`
  - 在当前 MTU=23 前提下，`qty <= 5` 才能安全单包发送

## 5.5 当前低功耗行为

### 事实

代码里原本“60 秒无连接/无操作进入 deep sleep”的逻辑被 `#if 0` 关掉了，当前实际运行路径不是这套逻辑。

当前有效路径更接近：

- 默认允许 `SUSPEND_ADV | SUSPEND_CONN`
- 当满足以下任一条件时，退出低功耗：
  - 充电状态有效
  - `bus_mux` 不是 `OWC idle`
  - 有放电电流
  - 处于 `MODE_FACTORY`
  - 已建立 BLE 连接

### 对客户端的含义

- 设备不会按旧注释那样“60 秒自动断开并 deep sleep”。
- 但设备是否进入更低功耗状态，会受充电状态、OWC/UART 总线状态、工厂模式影响。
- 做后台稳定性测试时，要把这些条件一并记录。

## 6. GATT 服务表

## 6.1 标准服务

| 服务 | UUID | 作用 | 备注 |
| --- | --- | --- | --- |
| GAP | `0x1800` | 设备名、Appearance、连接参数 | 只读 |
| GATT | `0x1801` | Service Changed | 理论支持 |
| Device Information | `0x180A` | PnP ID | 只读 |
| Battery | `0x180F` | Battery Level | 当前看更像占位服务 |

## 6.2 自定义业务服务

### Telink SPP Service

- Service UUID：`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- Characteristic A UUID：`6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- Characteristic B UUID：`6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

### OTA Service

- Service UUID：`00010203-0405-0607-0809-0A0B0C0D1912`
- Characteristic UUID：`00010203-0405-0607-0809-0A0B0C0D2B12`

## 6.3 关键 Handle 表

| Handle | 类型 | UUID | 当前真实用途 |
| --- | --- | --- | --- |
| `0x0013` | Primary Service | `6E400001...` | SPP 服务 |
| `0x0014` | Char Declaration | `0x2803` | 指向 `0x0015` |
| `0x0015` | Char Value | `6E400002...` | 客户端写入请求入口 |
| `0x0016` | User Description | `0x2901` | 文本 `Telink SPP: Module->Phone`，但描述是反的 |
| `0x0017` | Char Declaration | `0x2803` | 指向 `0x0018` |
| `0x0018` | Char Value | `6E400003...` | 固件通知响应出口 |
| `0x0019` | CCCD | `0x2902` | 对 `0x0018` 开启 Notify |
| `0x001A` | User Description | `0x2901` | 文本 `Telink SPP: Phone->Module`，但描述是反的 |
| `0x001B` | Primary Service | OTA Service | OTA 服务 |
| `0x001D` | Char Value | OTA Char | OTA 数据入口 |
| `0x001E` | CCCD | `0x2902` | OTA 通知使能 |

## 6.4 这套 SPP 的真实收发方向

### 事实

- `module_onReceiveData()` 挂在 `0x0015 / 6E400002...`
- `notify_big_packet()` 通知发到 `0x0018 / 6E400003...`

### 因此客户端必须这样做

- 向 `6E400002...` 写入请求
- 对 `6E400003...` 打开通知并接收响应

### 不要按这些名字做判断

- `Server2Client`
- `Client2Server`
- `Telink SPP: Module->Phone`
- `Telink SPP: Phone->Module`

它们在当前固件里和真实行为不一致。

## 7. BLE 上层协议：Modbus RTU over BLE

## 7.1 帧格式

BLE 业务负载直接承载一帧 Modbus RTU 原始报文：

`[addr][func][payload...][crc_lo][crc_hi]`

### 事实

- 设备地址：`0x01`
- 广播地址：`0x00`
- CRC：`CRC16/MODBUS`
- CRC 在帧尾按 `low byte first` 排列

## 7.2 支持的功能码

| 功能码 | 含义 | 当前状态 |
| --- | --- | --- |
| `0x03` | Read Holding Registers | 支持 |
| `0x06` | Write Single Register | 支持 |
| `0x10` | Write Multiple Registers | 支持 |
| `0x7F` | Echo/Test | 固件自定义测试码 |

对于不支持的功能码，固件会回复异常：

- `func | 0x80`
- exception code = `0x01`

## 7.3 请求与响应重组规则

### 请求侧

当前固件没有请求重组逻辑，所以一帧请求必须完整落在单个 GATT Write 内。

### 响应侧

固件会把完整 Modbus RTU 响应按 `20 byte` 切片通知出来。客户端需要自己重组。

### 推荐重组策略

收到通知后按功能码判断总长度：

- `0x03` 响应总长 = `3 + byteCount + 2`
- `0x06` 响应总长 = `8`
- `0x10` 响应总长 = `8`
- 异常响应总长 = `5`
- `0x7F` Echo 响应总长 = 与请求长度相同

客户端不应使用“通知间时间隔”作为帧结束判据，应使用“功能码 + 长度字段 + CRC 校验”作为闭环判据。

## 7.4 示例报文

### 读取 MAC 地址 3 个寄存器

`01 03 00 00 00 03 05 CB`

### 读取产品序列号 16 个寄存器

`01 03 C0 02 00 10 D9 C6`

### 读取事件日志 10 个寄存器

`01 03 C0 08 00 0A 78 0F`

### 读取保护参数起始 5 个寄存器

`01 03 21 00 00 05 8F F5`

### Echo 测试

`01 7F 12 34 56 78 6F 34`

### 写蓝牙名 suffix 为 `D3A`

说明：

- 这里只写 suffix，不写固定前缀 `BT_`
- 数据区按 ASCII 原始字节写入，遇 `0x00` 结束

报文：

`01 10 01 00 00 02 04 44 33 41 00 2A 90`

## 8. 寄存器地图

## 8.1 设备身份与命名

| 地址范围 | 说明 | 读写 | 备注 |
| --- | --- | --- | --- |
| `0x0000`~`0x0002` | 公有 MAC 地址，3 个 16-bit word | 读 | 来源于 `g_stCellInfoReport.mac_public[]` |
| `0x0100`~`0x010B` | 当前蓝牙名读取区 | 读 | 当前只开放 12 个寄存器 |
| `0x0100`~`0x010F` | 蓝牙名写入区 | 写 | 当前写入范围为 16 个寄存器 |

### 蓝牙名规则

- 固定前缀：`BT_`
- 只允许写 suffix
- suffix 允许字符：`[A-Za-z0-9_-]`
- 总长度上限：`25`
- suffix 最大长度：`22`
- 持久化位置：`FLASH_ADDR_BLE_NAME_BASE = 0x50000`

### 当前实现的不一致点

- `BTNAME_REG_COUNT = 12`
- `BTNAME_REG_WORDS = 16`
- 头文件注释又写“25 字节名称需要 13 个寄存器”

所以当前蓝牙名读写协议存在范围不一致，客户端应优先按“写 16，读 12”的固件事实做兼容，后续建议固件统一。

## 8.2 产品信息与事件日志

| 地址范围 | 说明 | 读写 | 备注 |
| --- | --- | --- | --- |
| `0xC002`~`0xC011` | `BMS_SerialNumber` | 读 | 16 个寄存器，ASCII 两字节一组 |
| `0xC012`~`0xC021` | `BMS_HardWareVersion` | 读 | 16 个寄存器 |
| `0xC022`~`0xC031` | `BMS_SoftWareVersion` | 读 | 16 个寄存器 |
| `0xC008` 起读 | 事件日志窗口 | 读 | 这是旧上位机兼容特判，和上面地址有重叠 |
| `0x1007` | 事件日志清除 | 写 | 写入 `0x0001` 触发 factory reset |

### 事件日志特别说明

这是一个历史兼容点：

- 正常产品信息区从 `0xC002` 开始
- 但如果你发起 `0x03 @ 0xC008`，固件不会返回产品信息，而是直接进入 `event log` 特判逻辑

客户端结论：

- 读产品信息时不要从 `0xC008` 起读
- 事件日志请明确用“从 `0xC008` 起读”的独立路径

## 8.3 实时数据与状态

| 地址范围 | 说明 | 读写 | 备注 |
| --- | --- | --- | --- |
| `0xD000`~`0xD01F` | `g_stCellInfoReport.u16VCell[0..31]` | 读 | 原始 32 word 数组 |
| `0xD100`~`0xD108` | 最近三组故障记录窗口 | 读 | 前 3 个寄存器当前返回 0 |
| `0xD109`~`0xD114` | `System_ErrFlag` 打包窗口 | 读 | 每寄存器打包两个 `u8 flag` |
| `0xD115` | `SystemStatus` 低 16 bit | 读 | 状态字 |
| `0xD116` | `SystemStatus` 高 16 bit | 读 | 状态字 |

### 关于 `0xD000`~`0xD01F`

这里只是直接平铺 `u16VCell[32]`，不是已经定义好的“统一上位机模型”。

当前固件事实：

- 当前编译型别是 `D3PRO`
- `SeriesNum = 10`
- 因此可以推断前 10 路更像真实 cell voltage
- 但数组末尾还被塞入了额外 ADC 信息：
  - `u16VCell[29] = bat_temp_mv`
  - `u16VCell[30] = mos_temp_mv`
  - `u16VCell[31] = Vbat_mv`

所以这个区间对用户侧 App 不够友好，更适合工程调试页直接展示。

## 8.4 保护参数区

保护参数平铺到 `0x2100`~`0x2140`，总计 65 个寄存器，可读可写。

每组 5 个寄存器依次为：

- `First`
- `Second`
- `Third`
- `Rcv`
- `Filter`

分组如下：

| 地址范围 | 含义 |
| --- | --- |
| `0x2100`~`0x2104` | Vcell OVP |
| `0x2105`~`0x2109` | Vcell UVP |
| `0x210A`~`0x210E` | Vbus OVP |
| `0x210F`~`0x2113` | Vbus UVP |
| `0x2114`~`0x2118` | Ichg OCP |
| `0x2119`~`0x211D` | Idsg OCP |
| `0x211E`~`0x2122` | Tchg OTP |
| `0x2123`~`0x2127` | Tchg UTP |
| `0x2128`~`0x212C` | Tdischg OTP |
| `0x212D`~`0x2131` | Tdischg UTP |
| `0x2132`~`0x2136` | Tmos OTP |
| `0x2137`~`0x213B` | Vdelta OVP |
| `0x213C`~`0x2140` | SocLow |

关于单位，需分两类处理：

- 可以从代码注释确认的量：
  - cell voltage 多数按 `mV`
  - 温度按 `(+40 degC) * 10`
  - 电流状态量多处按 `A * 10`
- 保护参数内部比较量的最终工程单位，建议你在上位机第一页只显示“原始值 + 中文标签”，不要先做强假设换算，避免误导。

## 8.5 控制与调试寄存器

| 地址 | 含义 | 写入值 | 备注 |
| --- | --- | --- | --- |
| `0x1005` | SOC 参数设置 | 任意 `u16` | 内部调用 `set_soc_param()` |
| `0x1007` | 事件日志复位 | `0x0001` | 清空事件日志 |
| `0x1102` | 调试控制 1 | `0x0003` | 使能 `enable_current_test` |
| `0x1102` | 调试控制 1 | `0x000A` | 置位 `deepsleep_en` |
| `0x1103` | 调试控制 2 | `0x0003` | 关闭 `enable_current_test` |
| `0x2319` | 循环次数 | 任意 `u16` | 同时刷新 SOC 参数 |

## 9. 当前固件中会影响客户端实现的协议不一致点

这些都是事实，不是推测。

### 9.1 SPP 特征方向反了

- 名字叫 `Server2Client` 的 `6E400002`，实际是客户端写入口
- 名字叫 `Client2Server` 的 `6E400003`，实际是设备通知出口

### 9.2 User Description 也反了

- `0x0016` 文本是 `Module->Phone`
- 但它挂在写入口上

### 9.3 广播里带了 HID UUID，但 GATT 没启用 HID 服务

影响：

- 纯靠 ADV UUID 做筛选的 App 可能误判设备能力

### 9.4 蓝牙名寄存器范围不统一

- 读：12 words
- 写：16 words
- 注释：又说应该 13 words

### 9.5 `0xC008` 地址重叠了“产品信息区”和“事件日志入口”

影响：

- 批量扫产品信息时如果覆盖 `0xC008`，会拿到错误语义的数据

### 9.6 Battery Service 目前更像占位能力

虽然有 `0x180F / 0x2A19`，但当前代码中没有发现业务主路径对它的稳定更新调用。客户端不应把它当作主数据源。

### 9.7 请求不能跨包

响应支持 20 字节通知分片，请求不支持跨包重组。调试工具必须做写包长度约束。

## 10. 面向 iOS / iPad / macOS / 上位机的实现建议

## 10.1 推荐总体架构

建议拆成四层：

1. `Protocol Core`
   - `CRC16/MODBUS`
   - `ModbusFrame`
   - `RegisterMap`
   - `ResponseAssembler`
2. `Transport Layer`
   - `BLETransport`
   - `UARTTransport`
3. `Domain Layer`
   - `DeviceInfoService`
   - `RealtimeDataService`
   - `ProtectConfigService`
   - `LogService`
   - `DebugService`
4. `UI Layer`
   - 用户模式
   - 工程模式

### 核心原则

- 协议核心必须与 UI 解耦
- BLE 与 UART 共用同一套 `Modbus codec`
- 寄存器地址不要散落在界面代码里

## 10.2 Apple 端建议

### 建议方案

优先做一套 `SwiftUI + CoreBluetooth` 的多平台工程，直接覆盖：

- iPhone
- iPad
- macOS

### 好处

- BLE 栈统一
- 协议核心统一
- 页面与 ViewModel 可以大部分复用
- 工程调试工具可以先在 macOS 版落地，再下沉到 iPad/iPhone 子集

### 模块建议

- `BmsBluetoothManager`
- `BmsModbusCodec`
- `BmsRegisterCatalog`
- `BmsSessionRecorder`
- `BmsDeviceStore`
- `BmsDebugConsole`

## 10.3 上位机建议

如果上位机包含 Windows：

- 推荐 `Qt + C++` 或 `Tauri + Rust`

如果上位机仅面向你自己调试：

- 直接用同一套 macOS App 即可
- 再补一个串口 transport，就能覆盖 BLE + UART 两种链路

### 我的建议

优先顺序建议如下：

1. 先做一套 Apple 通用协议核心
2. 先做 macOS 工程版
3. 再裁剪出 iPhone/iPad 用户版
4. Windows 上位机如确有需求，再复用协议核心移植

## 10.4 用户版与工程版的功能边界

### 用户版建议功能

- 扫描并连接设备
- 展示设备名、SN、HW/SW Version
- 展示关键状态与告警
- 基本实时数据页
- 日志查看
- 蓝牙名修改
- OTA 升级

### 工程版建议功能

- 原始寄存器浏览器
- 批量读写模板
- 事件日志导出
- Echo 测试
- 分包重组日志
- BLE RSSI / reconnect / timeout 统计
- 串口与 BLE 双链路切换
- 当前连接会话原始报文录制

## 11. 建议的测试清单

## 11.1 基础链路

- 扫描设备名是否稳定
- 连接后是否能发现 `6E400001...`
- 是否能正确订阅 `6E400003...`
- 是否能向 `6E400002...` 成功写入

## 11.2 协议正确性

- `0x7F` Echo 自检
- `0x03` 读短帧
- `0x03` 读长帧并重组
- `0x06` 写单寄存器
- `0x10` 写 1~5 个寄存器
- 错误 CRC 是否被设备静默丢弃

## 11.3 业务正确性

- 产品信息区读取
- 蓝牙名读写与重启保持
- 保护参数区读写
- 事件日志读取与清除
- 循环次数写入

## 11.4 稳定性

- 断连重连
- App 前后台切换
- 手机锁屏后恢复
- 设备充电/放电状态切换下的 BLE 稳定性
- OWC/UART 总线忙时 BLE 行为

## 11.5 OTA

- 服务发现
- 进入 OTA 流程
- 数据发送稳定性
- 成功/失败结果回调
- 升级后版本号与广播名是否正常

## 12. 建议的固件后续改造项

如果你准备长期维护客户端，建议尽早把以下问题修正到协议层：

1. 修正 SPP 两个 characteristic 的命名、属性与真实方向一致。
2. 广播包移除 `0x1812 HID`，避免虚假能力暴露。
3. 蓝牙名寄存器范围统一为同一规格，建议直接固定成 16 words。
4. 解决 `0xC008` 与产品信息区的语义重叠。
5. 把 `MTU_SIZE_SETTING` 提升到 `247`，同时把通知切片长度从固定 `20` 改为 `effective_mtu - 3`。
6. 如需长期支持大包请求，增加请求侧重组机制，或者自定义应用层帧头。
7. 为实时数据定义一份“稳定的业务模型寄存器表”，不要直接暴露 `u16VCell[32]` 这种内部数组。

## 13. 最终建议

对于你当前要做的 iOS/iPad/mac App 与上位机，我建议直接采用下面这条路线：

1. 先把 `Modbus RTU over BLE/UART` 抽成统一协议核心
2. 先做 macOS 工程版工具，把所有寄存器、日志、蓝牙名、Echo、录包能力做全
3. 再在同一套 Apple 工程里裁剪 iPhone/iPad 用户版界面
4. 如果后续要做 Windows 上位机，再复用同一套协议核心即可

这条路线的好处是：

- 研发资产可复用
- 测试链路统一
- 用户版与工程版不会出现协议分叉
- 后续固件升级时，维护成本最低
