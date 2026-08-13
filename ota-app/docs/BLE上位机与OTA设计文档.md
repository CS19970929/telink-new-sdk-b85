# Telink B85m BLE 上位机 / BMS 通信 / OTA 设计文档

> 适用范围：`tc_ble_single_sdk-ota` worktree（分支 `new-c11-ota`）中的 `ota-app/` 工程。
> 目标设备：Telink TLSR825x（B85m）BLE Single Connection SDK V3.4.2.8 固件（vendor/ble_sample，BMS 电池应用）。
> 配套事实文档：[`OTA_PROTOCOL_FACTS.md`](../../OTA_PROTOCOL_FACTS.md)（协议层逐项事实与实机证据）。
>
> 文档日期：2026-08-07。文中所有协议/寄存器结论均来自 SDK 源码、实机（BT_cs-0604 / A4C13816025A）抓包与官方 Telink OTA App 2.1.2 反汇编三方核对。

---

## 1. 总体目标

在 Windows 桌面实现一个 BMS 电池上位机，同时承担两件事：

1. **电池监控**：经 BLE 的 SPP 通道，以 Modbus RTU 读取电池全部状态（实时量、单体、温度、容量、状态字、故障、保护参数、产品信息）。
2. **OTA 升级**：对 BLE 设备做安全可靠的固件升级（Telink Legacy/Extend 双协议），并满足：
   - 升级中任意断连/超时/错误包/断电都不能导致设备变砖（设备端双区机制 + App 端严格流程）；
   - 升级成功 = 设备 Result 确认 + 重启 + 重连 + 版本复核，四步缺一不可；
   - 有界发送队列 + 平台背压，不允许无节制循环写入。

---

## 2. 系统架构

分层设计，协议核心与 UI/平台 BLE API 完全解耦（为 Android/iOS 移植保留边界）：

```text
UI（WPF 两页）
 ├─ 电池信息页：设备列表 / 实时数据 / 单体 / 温度 / 状态故障 / 保护参数 / 产品信息
 └─ OTA 升级页：固件选择 / 协议与 PDU 配置 / 开始与取消 / 进度

Application 层
 ├─ MainViewModel（UI 状态、扫描、电池面板、OTA 启动编排）
 ├─ BatteryMonitor（电池轮询服务：连接/轮询/断连自愈/自动重连）
 └─ OtaSessionOptions（OTA 会话配置）

Domain 层（TelinkOta.Core，无平台依赖）
 ├─ Ota/FirmwareParser       固件解析与预检查
 ├─ Ota/OtaPacketEncoder     分包/补齐/CRC16/命令编码/Notify 解析
 ├─ Ota/OtaStateMachine      严格单向状态机
 ├─ Ota/OtaSession           会话编排（超时/取消/重启重连/版本复核/流控）
 ├─ Ota/OtaResult            设备 Result 码 → 用户可操作建议
 ├─ Bms/BatteryStatus        BMS 寄存器映射 + 快照解析
 ├─ Bms/ModbusSppClient      SPP 上的 Modbus 客户端（单飞/分片重组/迟到帧防护）
 └─ Ota/ModbusRtu            Modbus RTU 帧构造与校验

Transport 层（Windows 实现）
 └─ WindowsBleTransport + BleScanner（Windows.Devices.Bluetooth）

协议事实
 └─ OTA_PROTOCOL_FACTS.md（唯一事实依据）
```

### 2.1 代码结构

```
ota-app/
├── TelinkOta.sln
├── src/TelinkOta.Core/            协议核心（net7.0，无平台依赖）
├── src/TelinkOta.App.Wpf/         Windows 桌面 App（net7.0-windows10.0.19041.0）
├── tests/TelinkOta.Core.Tests/    NUnit 单元测试（83 个用例）
└── tools/TelinkOta.Diag/          实机诊断工具（SPP Modbus 协议探测/抓包）
```

---

## 3. BLE 传输层（Windows）

### 3.1 能力与接口

`IBleTransport`（Core 抽象）提供：连接/断开、OTA 服务发现、SPP 服务发现、通知订阅、MTU 协商（尽力）、Write Without Response、Write SPP、队列排空、`OtaNotifyReceived`/`SppNotifyReceived`/`ConnectionLost` 事件。

`WindowsBleTransport`（Windows.Devices.Bluetooth）实现要点：

| 项 | 说明 |
|---|---|
| 扫描 | 双通道并行：`BluetoothLEAdvertisementWatcher` Active 原始广播 + `DeviceWatcher(GetDeviceSelectorFromPairingState(false))` 未配对设备枚举；12 秒后自动增强重试一次（总扫描 30 秒） |
| 连接 | `BluetoothLEDevice.FromBluetoothAddressAsync` **不会真正建立连接**——必须先做一次 GATT 操作（`GetGattServicesAsync`）触发连接建立，再轮询 `ConnectionStatus == Connected` |
| 地址类型 | 当前固件为 Public 地址；`FromBluetoothAddressAsync` 假定 Public。实测目标设备 A4C13816025A 前两位 0xA4 属随机静态地址段，但 Windows 下连接正常 |
| 服务发现 | `BluetoothCacheMode.Uncached`；按 UUID 匹配，不硬编码 Handle |
| 通知订阅 | CCCD 写 `Notify`（0x0100），同一 OTA Characteristic 收/发 |
| MTU | Windows 无公开 ATT MTU 接口；`GattSession.MaxPduSize - 7` 作为写负载上限估计；错误超限由首包失败自动降级兜底 |
| SPP 写 | **必须 WriteWithResponse**（实机验证：该固件丢弃 WriteWithoutResponse 的 SPP 写，与 Qt 上位机一致） |
| OTA 写 | WriteWithoutResponse（Telink 官方规范，实机验证可用） |
| 断连检测 | `GattSession.SessionStatusChanged != Active` → `ConnectionLost` |

### 3.2 已知 Windows 特性/坑

- 当前固件把设备名放在 Scan Response，主广播只带 UUID 1812/180F；Windows 收到主广播但漏收 Scan Response 时只能先显示“名称未广播”。上位机必须使用 Active 模式，后续同地址 Scan Response 到达时再补全名称；
- 单靠 `BluetoothLEDevice.GetDeviceSelector()` 查询系统缓存对未配对设备无效，本机实测返回 0 台。当前按微软 GATT Client 指南并行使用 `GetDeviceSelectorFromPairingState(false)` 创建设备枚举器，该查询会请求 Windows 主动发现未配对 BLE 设备；
- 名称缓存只用于同一地址仍在线、但某个广播包没有 LocalName 时补全显示，不代表设备在线。Windows 报告 `Removed`/`IsPresent=false` 后进入 5 秒防抖；期间无新广播则从 UI 移除，防止休眠设备永久显示；扫描完成后保留轻量 DeviceWatcher 在线状态监听，连接/OTA/手动停止时释放；
- `BluetoothSignalStrengthFilter` 是“进入/离开范围”的状态过滤，不是单纯 RSSI 排序。2026-08-13 本机实测旧过滤配置在弱信号环境明显减少回调，因此发现阶段已取消该过滤，列表仍按 RSSI 排序；
- `BluetoothLEAdvertisementWatcher.Stop()` 会先进入异步 `Stopping`。复用同一实例会使快速重扫排队并让旧 `Stopped` 事件污染新状态；当前每轮创建新 Watcher、先解绑旧事件，并在扫描中途自动换新实例重试；
- 不得在实时扫描同时并发对 Windows 缓存列表逐个调用 `BluetoothLEDevice.FromIdAsync`。当前仅读取 `DeviceInformation` 的地址/存在属性补名，避免争用蓝牙栈或误占单连接设备；
- 2026-08-13 实机扫描验证：双通道增强扫描发现 70 台设备，目标 `BT_cs_0812 / A4C13816025A` 在 `Added` 阶段即被识别；连续观测 RSSI 在 `-96~-81 dBm` 波动；
- `FromBluetoothAddressAsync` 不发起连接（必须 GATT 操作触发）——已处理；
- Windows 不暴露协商后的 ATT MTU——用 `MaxPduSize-7` 估计 + 首包失败降级；
- Telink 设备为**单连接**：上位机/诊断工具/手机同时连接会互相挤掉——同一时刻只允许一个客户端；
- 实测通知存在**重复投递**（同一帧到达两次）——客户端按期望长度匹配帧来免疫（见 §5.3）。

---

## 4. BMS BLE 通信（Modbus RTU over SPP）

### 4.1 服务与特征

| 项 | UUID | 用途 |
|---|---|---|
| SPP Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | 业务数据通道 |
| 写特征（客户端→设备） | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Modbus 请求（WriteWithResponse） |
| 通知特征（设备→客户端） | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Modbus 响应（20 字节分片 Notify） |

> 注意：固件特征命名（Server2Client/Client2Server）与实际方向相反，以本表为准。

### 4.2 Modbus RTU 帧

- 从机地址 `0x01`（固件 `MB_ADDR`）；0x00 广播可被处理但不回包；
- 功能码：`0x03` 读保持寄存器（一次最多 125 寄存器）、`0x06` 写单、`0x10` 写多、`0x7F` Echo（链路验证）；
- **寄存器/数量均为大端 u16**（固件 `u16be`/`put_u16be`）；
- CRC16/MODBUS（poly 0xA001，init 0xFFFF），wire 低字节在前；
- 响应由固件 `notify_big_packet` 按 **20 字节**分片通知，客户端自行重组。

### 4.3 寄存器地图（固件 read_reg / stCell_Info 实码）

| 窗口 | 地址 | 内容 |
|---|---|---|
| 稳定窗口 | `0xD120` 起 11 寄存器 | Magic=`0x4253`("BS")、版本、总压(V×100)、电流(int16 A×10，充电正/放电负)、SOC(%)、最高/最低/MOS 温度((+40℃)×10)、单体最高/最低/压差(mV) |
| 完整窗口 | `0xD000` 起 63 寄存器 | stCell_Info 平铺：32 单体(mV)、单体极值/位置/压差、总压(V×100)、10 路温度((+40℃)×10)、充/放电电流(A×10)、SOC/SOH(%)、容量(当前/满充/出厂 Ah×100)、循环次数、3 组故障标志、2 组均衡标志 |
| 状态字 | `0xD115` 起 2 寄存器 | SystemStatus u32 位标志（StartUpBMS/MOS/继电器/加热/冷却/AFE/均衡/休眠/ProjectVer 等） |
| 故障记录 | `0xD100` 起 21 寄存器 | 最近三组故障记录（hex） |
| 保护参数 | `0x2100` 起 65 寄存器 | 13 组 × 5（First/Second/Third/Rcv/Filter）：Vcell/Vbus OVP·UVP、充/放电 OCP、充/放电 OTP·UTP、Tmos OTP、Vdelta OVP、SocLow |
| MAC | `0x0000` 起 3 寄存器 | 6 字节 MAC |
| 蓝牙名 | `0x0100` 起 12 寄存器 | ASCII |
| 序列号 | `0xC002` 起 16 寄存器 | ASCII |
| 硬件版本 | `0xC012` 起 16 寄存器 | ASCII |
| 软件版本 | `0xC022` 起 16 寄存器 | ASCII |

单位换算：电压 raw/100 V；电流 int16 raw/10 A（稳定窗口）或 u16 充/放电分别/10 A（完整窗口）；温度 raw/10−40 ℃；容量 raw/100 Ah。

### 4.4 轮询策略（BatteryMonitor）

| 周期 | 内容 |
|---|---|
| 1s | 0xD120 稳定窗口（Magic 校验） |
| 2s | 0xD000 完整窗口 + SystemStatus |
| 4s | 0xD100 故障记录 |
| 连接时 + 每 10s | 静态信息（产品信息/MAC/蓝牙名/保护参数），未取到时持续重试 |

### 4.5 鲁棒性设计

1. **单飞请求**：一次一个请求，响应分片累积到完整帧才交付，杜绝交错；
2. **迟到帧/重复通知防护**：`ModbusSppClient.TryComplete` 按"期望数据长度 + 从机地址"匹配帧，累积缓冲只逐字节/逐帧丢弃确认错误的字节，**绝不整体清空**（避免丢掉在途的真实响应分片）；
3. **基线补齐**：完整窗口每 2s 才读一次，快照每秒发布——上一轮完整窗口数据作为基线合并进每轮快照（只填 null 不覆盖新值），避免字段闪烁；
4. **链路自愈**：监听 `ConnectionLost`；连续 6 轮无有效数据或链路断开 → 自动重连（15s 连接超时、10s 间隔重试）并恢复轮询；
5. **超时**：稳定窗口 4s、完整窗口 6s、其余 4s（设备唤醒/处理慢时避免误判）。

---

## 5. OTA 设计

### 5.1 协议事实（摘要，详见 OTA_PROTOCOL_FACTS.md）

- OTA Service `00010203-0405-0607-0809-0A0B0C0D1912` / Characteristic `...2B12`（同一特征收发 + Notify）；
- Opcode：`0xFF00` VERSION、`0xFF01` START、`0xFF02` END、`0xFF03` START_EXT、`0xFF04` FW_VERSION_REQ、`0xFF05` FW_VERSION_RSP、`0xFF06` RESULT、`0xFF80` SET_FW_INDEX；全部小端；
- 数据包 `[Adr_Index(2 LE)][数据][CRC16(2 LE)]`，**首包 Index=0**，PDU 16~240（16 的整数倍），受 `MTU-7` 限制；
- 尾包不足 PDU 时以 `0xFF` 补齐到 16 的整数倍（官方 App 行为），CRC16 覆盖补齐位；
- CRC16 = CRC-16/MODBUS（0xA001），`"123456789"→0x4B37`；
- 设备固件 CRC32：init `0xFFFFFFFF`、reflected `0xEDB88320`、**无 final xor**、尾部 4 字节 LE；`"123456789"→0x340BC6D9`，实机 BIN 前 91072 B → `0x814C0E73`；
- **Firmware Size@0x18 = 文件总长（含尾部 4 字节 CRC32）**——`tl_check_fw2.exe` 后处理实机验证（追加 CRC 并回写 Size）；
- Firmware Mark @0x08 = `0x544C4E4B`("TLNK")；
- 启动标志 = 各 Firmware 区偏移 0x08（本工程目标区 `0x20008`），值 `0x544C4E4B`；
- END = `[index_max(2 LE)][index_max ^ 0xFFFF(2 LE)]`；
- Result 码 0x00~0x0E 全映射为"结果名 + 用户可操作建议"；
- 设备端超时：packet 15s / process 180s（本工程 app_config.h）。

### 5.2 固件解析（FirmwareParser）

预检查（失败即拒绝，不静默修改）：
1. 文件 ≥ 0x20 字节；
2. `Size@0x18` 合法（>0、≤ 分区上限 124K、与文件长度关系一致）；
3. Mark @0x08 == `TLNK`（否则设备将回 `0x0A MARK_ERR`）；
4. 尾部 4 字节为合法 Telink CRC32（覆盖前 len−4 字节）→ 原样发送；
5. 否则视为"原始构建产物"：**自动追加 CRC32 并回写 Size@0x18 = len+4**（与 `tl_check_fw2.exe` 语义一致），日志明确提示；
6. 输出：发送载荷、声明尺寸、Bin 版本（0x02 处 u16）、SDK 版本串（`$$$...$$$`）、SHA-256。

### 5.3 双协议流程

**Extend（优先）**：
```
连接 → 发现 OTA 服务 → 订阅通知 → MTU/PDU 协商
→ CMD_OTA_FW_VERSION_REQ（5s 内等 CMD_OTA_FW_VERSION_RSP，accept=1 才继续）
→ CMD_OTA_START_EXT [pdu_len][version_compare][rsvd16]
→ 数据包（有界窗口）→ 排空 → CMD_OTA_END [index_max][index_max^0xFFFF]
→ 等 CMD_OTA_RESULT（20s）→ 0x00 成功
```

**Legacy（兼容/回退）**：可选先发 `CMD_OTA_VERSION` → `CMD_OTA_START` → 数据包 → END → 写完即成功（3s 宽限等可能的 Result）。Auto 模式下 Extend 版本响应超时自动回退 Legacy。

**升级后验证**：等设备重启断连（8s）→ 自动重连（30s）→ 重新发现/订阅 → 版本复核（OTA 版本协商响应 + 尽力读取 BMS 软件版本 0xC022 前后对比）→ 判定成功。

### 5.4 发送流控（BoundedWriter）

- Write Without Response 并发窗口默认 **6**（可配 1~32）：窗口满即节流，基于平台可写状态而非固定延时；
- 窗口 10s 不释放判定发送停滞（设备 packet timeout 15s，留余量）→ 中止会话；
- 全部在途写入排空后才发送 END；
- 首包写入失败（PDU 超出设备 MTU）→ 自动降级 PDU=16 从头重试一次（AutoDowngradePdu）。

### 5.5 状态机与超时

严格单向状态机（`OtaStateMachine`）：Idle → Connecting → DiscoveringServices → EnablingNotifications → ValidatingFirmware → NegotiatingMtuAndPdu → VersionCheck → SendingStart → Transferring → DrainingTxQueue → SendingEnd → WaitingResult → WaitingReboot → Reconnecting → VerifyingVersion → Success；任意失败收敛到 Cancelled/Failed/Disconnected/TimedOut。

| 超时 | 值 | 说明 |
|---|---|---|
| 连接 | 15s | |
| 服务发现 | 10s | |
| 版本响应 | 5s | |
| 发送停滞 | 10s | < 设备 15s |
| 会话总超时 | 170s | < 设备 180s |
| Result | 20s | |
| 重启检测 | 8s | |
| 重连 | 30s | |
| Legacy Result 宽限 | 3s | |

### 5.6 安全与断电保护（设备端事实）

- 设备双区交替（本工程：运行区 0x00000，OTA 区 0x20000，最大 124K）；
- 升级数据写非活动区，成功前该区启动标志保持无效（0xFF），成功后写 `TLNK`，旧区标志最后清零 → 任意阶段断电旧固件仍可启动；
- Flash 保护：启动锁定、OTA 写前解锁、结束重锁（固件 `app_flash_protection_operation`）；
- App 端不做任何"续传"（协议未定义），断连后必须从头重试。

---

## 6. UI 设计（WPF）

两页 TabControl，共享设备选择与底部日志：

**电池信息页**：
- 设备列表（名称/地址/RSSI）+ 扫描/停止/连接/断开；
- 设备信息：MAC、蓝牙名、序列号、硬件/软件版本；
- 实时数据：总压、电流、SOC、SOH、充/放电电流、容量（当前/满充/出厂）、循环次数；
- 温度：最高/最低/MOS + 10 路明细；
- 单体电压：32 节 + 极值/位置/压差；
- 状态与故障：SystemStatus 32 位逐位解码、3 组故障标志逐位解码、均衡标志；
- 保护参数：13 组 × 5 完整表；
- 故障记录 hex。

**OTA 升级页**：固件选择（预检查日志）、协议（Auto/Extend/Legacy）、PDU（16~240）、发送窗口、尾包补齐选项、版本复核/版本比较开关、开始/取消、进度条。

日志同时写入 exe 同目录 `TelinkOta.log` 文件，便于远程排查。

---

## 7. 诊断与测试

### 7.1 单元测试（83 个用例，NUnit）

- CRC16/CRC32 跨平台测试向量（含真实固件 BIN 向量 `0x814C0E73`）；
- FirmwareParser：Size/Mark/CRC 校验、自动补齐+Size 回写、异常文件（过小/零尺寸/超分区/坏 Mark/异常尾部/文件不存在）；
- PacketEncoder：分包/补齐/Index/END xor/命令布局/Notify 解析；
- OtaStateMachine：全流程合法迁移、非法跳转/回退/终端态防护；
- OtaSession：成功闭环（含重启重连+版本复核）、Legacy 回退、Result 错误映射、PDU 超限降级、总超时、用户取消、连接/服务/重连失败、SPP 版本复核、进度事件、PDU 钳制；
- Bms：稳定窗口/完整窗口解析与换算、状态位/故障位解码、MAC 格式化、ModbusSppClient 分片重组、超时、写失败、**迟到帧/异长度帧防护**；
- Modbus：帧格式（大端）、文档示例帧比对、CRC 校验、异常响应、分片。

### 7.2 实机诊断工具（TelinkOta.Diag）

控制台工具，直接连设备做协议探测：扫描、特征枚举、全从机地址扫描、寄存器页扫描、单请求通知流抓包、连续轮询成功率/内容新鲜度、Echo/0x06/0x04 探针。历史实机结论：
- 该固件丢弃 SPP 的 WriteWithoutResponse（必须 WriteWithResponse）；
- 早期固件 `read_reg` 为空实现（全寄存器返回 0，连 Magic/MAC 都是 0）→ 需刷含完整寄存器表的新固件；
- 通知存在重复投递 → 客户端按长度匹配帧免疫；
- 稳定值（静止电池）连续读取内容相同属正常，非陈旧帧。

### 7.3 已知问题与后续

| 问题 | 状态/处理 |
|---|---|
| 设备固件为空 read_reg（早期版本） | App 日志明确提示"固件未实现 Modbus 寄存器表"，需刷新固件 |
| BLE 链路掉线导致显示冻结 | 已加断连检测 + 自动重连（待实机复验） |
| Windows 无 ATT MTU 接口 | `MaxPduSize-7` 估计 + 首包失败自动降级 PDU=16 |
| 设备默认 MTU=23 只支持 PDU=16 | 更大 PDU 需固件 `MTU_SIZE_SETTING` 调大 |
| Android/iOS | 协议核心（TelinkOta.Core）无平台依赖，可直接平移；BLE 传输按 CoreBluetooth / Android BLE 另写适配器 |
| 100 次循环升级稳定性 | 需要自动化（上位机 + 自动重启），列为后续 |

---

## 8. 关键决策记录（踩坑实录）

1. **SPP 写必须 WriteWithResponse**——Telink 固件 attribute 写回调对 ATT 写命令不触发（实机 Echo 验证），与 Qt 上位机一致；
2. **Modbus 寄存器/数量是大端**——初版按小端编码导致 qty 恒超 125 被设备静默拒绝；
3. **Windows `FromBluetoothAddressAsync` 不建连**——必须用 GATT 操作触发；
4. **通知重复投递**——响应帧按"期望长度+地址"匹配，逐帧丢弃迟到帧，绝不清空在途缓冲；
5. **Size@0x18 含尾部 CRC**——`tl_check_fw2.exe` 后处理会追加 CRC 并回写 Size；原始构建产物需 App 端补齐并回写，且 CRC 必须覆盖回写后的内容；
6. **快照基线**——低频窗口（2s）数据需合并进每秒快照，否则字段闪烁；
7. **单连接设备**——监控与 OTA 互斥：OTA 开始暂停监控并释放链路，结束后自动恢复。
