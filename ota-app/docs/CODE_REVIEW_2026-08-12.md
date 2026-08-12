# BLE 电池上位机 + OTA 代码评审报告

评审日期：2026-08-12。范围：`ota-app`、其测试与诊断工具，并与 `vendor/ble_sample` 固件实现交叉核对。报告中的“已修复”指本次提交已修改并通过自动测试；“需实机/源码验证”表示仅凭当前证据不能下结论。

## 1. 总体评价

工程分层清楚，`TelinkOta.Core` 不引用 Windows API，Modbus/OTA 编解码与平台传输边界基本合理。OTA 当前已由用户确认可正常使用，历史日志也显示多次获得 `OTA_SUCCESS`、重启断连、重连与服务复核；本次未做 OTA 实机写入。评审发现电池数据“只显示连接瞬间值”的根因是快照合并方向反了，不是设备停止更新；序列号缺失也来自同一合并遗漏。扫描、单飞并发、END/Result 竞态、全局超时和 SystemStatus 字序等问题已修复，但真正的平台发送背压、升级前后版本一致性判定、低压升级门禁仍需后续加强。

## 2. 问题清单

| 严重级别 | 类型/状态 | 文件:行 | 问题描述 | 建议修复 |
|---|---|---|---|---|
| 高 | 事实错误/已修复 | `src/TelinkOta.App.Wpf/Services/BatteryMonitor.cs:365,399` | 旧 `Merge(snap,last)` 无条件用旧快照覆盖本轮新数据，造成 UI 永久停留在连接瞬间。 | 已引入 `overwrite` 语义：实时值覆盖，完整窗口/基线只补空缺；仅本轮确有响应才更新健康状态。 |
| 高 | 事实错误/已修复 | `src/TelinkOta.App.Wpf/Services/BatteryMonitor.cs:399-446` | 旧合并逻辑没有复制 SN、HW、SW、MAC、蓝牙名，静态信息重试成功也无法进入 UI。 | 已合并全部静态字段，并在 `MainViewModel.cs:465-510` 对部分快照采用“非空才更新”。 |
| 高 | 风险/已修复 | `src/TelinkOta.Core/Bms/ModbusSppClient.cs:48-93` | 原实现没有串行门禁，并发请求会覆盖 `_tcs`；写失败提前返回还会遗留等待者。 | 已用 `SemaphoreSlim` 实现严格单飞，并把状态清理统一放入 `finally`。 |
| 高 | 风险/已修复 | `src/TelinkOta.Core/Ota/OtaSession.cs:237-280` | 原实现发送 END 后才创建 Result 等待者；设备快速回包会丢失 Result。 | 已在发送 END 前布置 TCS；新增“END 写返回前收到 Result”回归测试。 |
| 高 | 风险/已修复 | `src/TelinkOta.Core/Ota/OtaSession.cs:406-434` | 成功 Result 后设备可立即重启，状态尚未进入 `WaitingReboot` 时会被误判为异常断连。 | 已以成功 Result 作为预期重启断连依据。 |
| 高 | 风险/未关闭 | `src/TelinkOta.Core/Ota/OtaSession.cs:320-337` | “版本复核”只要求 OTA 版本协商有响应；BMS 软件版本读不到或升级前后相同仍可成功。 | 固件中定义可比较的版本来源，并要求 `VersionAfter` 与目标 BIN/升级前值满足明确规则；当前行为需实机/协议确认后再收紧。 |
| 中 | 事实错误/已修复 | `src/TelinkOta.Core/Bms/BatteryStatus.cs:224-233` | 固件 D115 返回低 16 位、D116 返回高 16 位，旧解析顺序相反。 | 已按 `modbus_rtu.c:205-210` 修正为 `low | high << 16`。 |
| 中 | 风险/已修复 | `src/TelinkOta.App.Wpf/ViewModels/MainViewModel.cs:116-158` | 旧扫描固定 15 秒后停止；手动重扫时旧计时器会停止新 watcher，且完全依赖新广播。 | 已使用可取消的 20 秒扫描会话，并由 `BleScanner.cs:102-129` 补充 Windows 已知设备缓存枚举。 |
| 中 | 风险/部分关闭 | `src/TelinkOta.App.Wpf/Ble/BleScanner.cs:50-129` | 实机按名称扫描未命中，但按地址扫描命中且广播名称为空，证明只按 Name 过滤不可靠。 | UI 同时展示/保留地址；后续可把上次成功设备地址持久化并提供“按地址直连”。 |
| 中 | 功能缺失/已修复 | `src/TelinkOta.App.Wpf/Services/BatteryMonitor.cs:197-230` | 没有修改蓝牙名功能。 | 已实现 `0x10 @ 0x0100` 写后缀、`0x03` 读回全名并严格比对；UI 位于 `MainWindow.xaml:62-74`。 |
| 中 | 风险/未关闭 | `src/TelinkOta.App.Wpf/Ble/WindowsBleTransport.cs:227-232` | `WaitForTxQueueDrainedAsync` 立即成功；传输循环又逐次 await 写入，因此“窗口 6/平台背压”描述与实际不符。 | 要么删除窗口配置并明确串行提交，要么基于 Windows GATT 写完成/队列容量实现可证明的在途窗口与排空。 |
| 中 | 风险/未关闭 | `tc_ble_single_sdk/.../vendor/ble_sample/app_att.c:397-419` | 126 字节 Modbus 响应被连续推入多个 Notify，队列满时立即失败且不重试；这是大窗口偶发丢帧的设备侧来源。 | 设备端增加 BLE TX FIFO 可用性检查、重试/调度发送；需固件构建和实机压力验证。 |
| 中 | 风险/已修复 | `src/TelinkOta.Core/Ota/OtaSession.cs:111-116` | 原 `TotalTimeout` 只在数据包循环内检查，连接、Result、重连阶段不受 170 秒总上限约束。 | 已对会话 linked CTS 执行 `CancelAfter(TotalTimeout)`。 |
| 中 | 事实错误/已修复 | `src/TelinkOta.Core/Ota/FirmwareParser.cs:55-127` | 有合法 CRC 但 Size=len-4 时原样发送，与本 SDK“Size 含 CRC”事实冲突；Mark 错误还映射为 `DeclaredSizeZero`。 | 已将旧格式发送载荷规范化为 Size=len 并重算 CRC；非法 Mark 返回 `MarkMissing`；SHA-256 改为最终发送载荷。 |
| 中 | 风险/未关闭 | `src/TelinkOta.App.Wpf/ViewModels/MainViewModel.cs:190-270` | `async void StartOta` 没有顶层 `try/finally`；意外异常可能让 `IsBusy`、CTS、监控恢复逻辑留在错误状态。 | 把主体下沉到 `Task`，事件入口只 await；资源与监控恢复放入 `finally`。 |
| 中 | 安全风险/未关闭 | `src/TelinkOta.App.Wpf/Services/BatteryMonitor.cs:197-230`、`vendor/ble_sample/modbus_rtu.c:400-449` | 改名写接口没有应用层认证；任何已连接客户端均可修改名称。 | 若产品有防篡改要求，在固件加入维护模式/口令挑战或受保护诊断会话；需产品安全需求确认。 |
| 中 | 安全风险/未关闭 | `src/TelinkOta.Core/Ota/OtaSession.cs:126-185` | OTA 前没有读取电压/SOC并执行低压门禁，断电安全完全依赖设备双区。 | 在 UI 明示稳定供电要求；可在释放监控链路前读取电池电压并按产品阈值阻止升级，阈值需硬件团队确认。 |
| 低 | 建议优化 | `src/TelinkOta.App.Wpf/ViewModels/MainViewModel.cs:529-545` | 日志在调用线程同步 `AppendAllText`，高频调试日志会产生 UI/IO 抖动；异常被静默吞掉。 | 使用单消费者异步日志队列、滚动文件，并至少统计日志落盘失败。 |
| 低 | 文档错误/已修复 | `OTA_PROTOCOL_FACTS.md:48-63,159` | 同一文档曾同时写 Size 含 CRC/不含 CRC，并同时写实测 BIN 有/无有效 CRC。 | 已统一为官方后处理产物 Size=文件总长（含尾 CRC），实测 BIN 尾 CRC 有效。 |
| 低 | 风险/未关闭 | `vendor/ble_sample/btname_modbus.h:6,13,20,33` | 蓝牙名总长允许 25 字节、写窗口 16 寄存器，但读窗口只有 12 寄存器（24 字节），最长名称无法完整读回。 | 本次上位机保守限制完整名 24 字符；固件后续应统一读写窗口定义。 |

## 3. 按评审重点的分类结论

### 3.1 协议正确性

- OTA 包索引、END 互补字段、CRC16/MODBUS 与尾包补齐已有单测覆盖；事实依据为 `OTA_PROTOCOL_FACTS.md:88-99`，实现入口为 `OtaPacketEncoder.cs:24-107`。
- Firmware Mark、Size、CRC32 已统一：`FirmwareParser.cs:31-127` 使用显式小端读写，旧 Size=len-4 输入会规范化，最终 SHA 对应实际发送载荷。分区上限仍为 124 KiB，依据 `OtaConstants.cs:29-37` 与 Flash 布局文档。
- Modbus 地址/数量按大端、CRC 按低字节先传；请求和 0x10 回包实现见 `ModbusRtu.cs:18-113`，与固件 `modbus_rtu.c:304-308,349-376,400-449` 一致。
- 产品信息地址与固件一致：SN/HW/SW 分别为 C002/C012/C022，见 `BatteryStatus.cs:33-36` 与 `modbus_rtu.h:18-25`；实机读到 `D666-20260812 / C31 / V8.0`。

### 3.2 可靠性与并发

- `ModbusSppClient` 现在严格单飞、限制异常累积缓冲、识别异常响应并在短静默窗内丢弃重复通知，见 `ModbusSppClient.cs:11-190`。相同长度且延迟超过静默窗的旧帧仍无法仅凭 Modbus RTU 区分，这是无事务号协议的固有限制。
- `BatteryMonitor` 以本轮 `anyOk` 判断链路健康，三轮连续失败才重连，旧值只用于显示基线而不伪装本轮成功，见 `BatteryMonitor.cs:268-396`。
- OTA 已补全局超时、意外断连取消、END/Result 竞态和成功后即时重启竞态，见 `OtaSession.cs:111-116,237-280,397-434`。
- Windows 扫描现在合并主动广播与系统已知设备缓存；实机证据显示设备广播可能没有 LocalName，因此地址识别是必要兜底。缓存枚举本身不能证明设备当前可连接，UI 仍应以连接结果为准。

### 3.3 设备约束与安全

- 单连接互斥在 UI 中由 OTA 前停止 `BatteryMonitor` 实现，见 `MainViewModel.cs:199-207`；该策略正确，但 `StartOta` 的异常恢复仍需 `finally` 加固。
- 本次没有修改固件 Flash/OTA 分区，也没有执行实机 OTA。`0x74000~0x7FFFF` 禁止擦除仍是发布与烧录流程硬约束，上位机没有任何整片擦除接口。
- PDU 在协商后按实际 `transport.MaxWriteLength - 4` 重新计算，见 `OtaSession.cs:144-158`；MTU=23 时结果为 16，符合设备现状。
- “失败不损坏”依赖设备双区 boot/commit 实现；上位机能保证失败后停止并从头重试，不能单独证明任意断电点均安全，仍需断电注入实测。

### 3.4 测试与可维护性

- Core 仍无 Windows 命名空间依赖；Windows API 只存在 WPF/Diag 项目，移动端复用边界成立。
- 自动测试由 83 个增至 94 个，新增 SystemStatus 字序、蓝牙名约束、0x10 帧/确认响应、END 即时 Result 等回归。Release 全解通过，0 warning/0 error。
- `BatteryMonitor` 与 `BleScanner` 位于 WPF 工程，尚缺可注入时钟/扫描器后的单元测试；当前关键合并错误只能靠代码评审与实机日志发现，建议将轮询合并策略下沉到 Core。

## 4. 测试缺口清单

1. `BatteryMonitor`：新旧快照合并、静态信息延迟成功、三轮失败重连、取消与停止并发、改名期间轮询互斥。
2. `BleScanner`：旧扫描计时器不能停止新扫描、缓存设备与广播去重、无名称但地址命中、watcher Aborted 状态。
3. `ModbusSppClient`：两个真正并发调用的串行化、写失败后的下一请求、异常帧、CRC 错后重新同步、超长噪声、迟到同长度重复帧。
4. 蓝牙改名：0x10 异常响应、写成功但读回不一致、奇数长度补 0、断连、掉电后持久化和重新广播；最后两项需实机验证。
5. OTA：每个阶段触发总超时、Result 与断连的所有先后排列、重连期间旧连接事件、首包 PDU 降级后状态清理。
6. FirmwareParser：Size=len-4 规范化后的 CRC/SHA 精确向量、合法 CRC 但任意 Size 拒绝、分区边界 `0x1EFFF/0x1F000`、超大文件与整数边界。
7. 设备端 Notify：126/130/250 字节响应在不同连接间隔与 TX FIFO 压力下的无丢包测试，需实机验证。
8. 双区安全：START 后、任意数据包、END 前后、Result 前后的断电矩阵；重启后必须仍能运行旧版本或完整新版本，需实机验证。

## 5. 对已知问题的分析与建议

### 扫描慢或不显示

实机在关闭原连接后，按名称扫描 20 秒仍未命中；按历史地址 `A4C13816025A` 在约 15 秒内命中，但广播名称为空且 RSSI=-96。由此可确认慢扫描至少受弱信号和广播不带名称影响，而不是单一 UI 故障。已增加缓存设备枚举、可取消的 20 秒扫描和地址去重；建议下一步持久化最后地址，并在 UI 标记“缓存设备/当前广播设备”。

### 连接后数据不更新

根因是上位机合并逻辑覆盖新值。实机旧进程日志持续显示 D120 约 13~51 ms、D000 约 22~79 ms 成功返回，证明设备一直在更新传输；修复版只读诊断连续读到单体压差 `26 → 28 → 28 → 27 mV`，直接证明数据源是新鲜的。

### 不显示序列号

固件 C002 可读，实机返回 `D666-20260812`。旧 UI 的静态快照合并遗漏 SN 是直接原因，现已修复，并将空字段改为不覆盖 UI 已有值。

### 修改蓝牙名

固件已有 `0x10` 写入能力：`btname_modbus.c:142-182` 校验后缀、持久化并更新广播；上位机现写入后必须通过 `0x03 @ 0x0100` 读回 `BT_` 全名一致才报告成功。为避免擅自改变样机，本次只读实机诊断没有执行改名；功能协议和边界已由单元测试验证。

### 0x0B 尺寸问题

用户已确认当前 OTA 正常，因此历史 0x0B 不再作为现存故障。0x0B 的设备定义仍是固件尺寸非法；本工程事实为 Size@0x18 等于含尾部 CRC32 的文件总长。解析器现在会：合法新格式原样发送；合法 CRC 的旧 len-4 格式先回写 Size=len 并重算 CRC；无法解释的 Size/文件长度关系直接拒绝。若未来重现 0x0B，应保留原 BIN、解析日志、设备当前固件和 Result 原始通知再判断，禁止用尺寸探针写入量产设备。

## 6. 优先级排序的行动清单

1. 先让用户运行本次 Release 版，验证 UI 实时值持续变化、SN 显示和扫描缓存效果；改名请指定测试名称后再做一次实机写后读回与掉电复核。
2. 将 `StartOta` 重构为可 await 的 `Task + try/finally`，保证任意异常都恢复监控、Busy 与 CTS。
3. 明确 OTA 的目标版本来源和判定规则，成功条件从“服务响应”收紧为可审计的版本匹配。
4. 修复设备端大包 Notify 的 FIFO 背压/重试，并做长时间 D000 压力测试。
5. 根据产品阈值增加低压/供电风险提示与升级门禁，完成双区断电注入矩阵。
6. 统一固件蓝牙名的 24/25 字节读写窗口，并为扫描、轮询与 Windows BLE 生命周期补平台集成测试。
