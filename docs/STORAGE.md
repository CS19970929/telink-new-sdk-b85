# Flash、SOC 持久化与低功耗

本文对应四产品共用实现。2026-10-06 按开发期需求删除老化模式、重新划分业务 Flash，并统一采用内部 journal schema 3；不迁移旧开发布局。通信字段、事件读取格式和 APP/OTA/SDK 区域保留；事件清空命令 `0x1007` 已取消。参数更新规则见 [OTA_PARAMETERS.md](OTA_PARAMETERS.md)。

## 存储结构与所有权

```text
Config / State / Event（各自拥有业务缓存）
             ↓
storage_record（固定记录、CRC32、序号、最后提交）
             ↓
storage_port → bms_storage_platform_telink → SDK Flash
```

没有 Factory 域、老化计时、KV、文件系统或额外管理层。业务不持有物理地址，不直接调用 SDK 擦写。所有写入都由主循环路径执行，ISR 不擦写 Flash。普通 getter 和协议读取只读 RAM 缓存，不触发初始化、擦除或保存；启动路径显式加载并验证各域。

| 域 | 业务数据 | 内部 schema / payload | 正常写入条件 |
|---|---|---|---|
| Config | 软件保护、产品/容量/SOC 设置、独立 AFE requested profile、校准、SN、蓝牙后缀、六类更新编号 | 3 / 322 bytes；magic `CFG2` | 候选编码实际改变或启动需要重置 |
| State | SOC、放电累计、循环、SOC_STATE 编号及保留槽 | 3 / 44 bytes | 有变化且检查点到期，或显式保存 |
| Event | 100 条事件环、重复计数、写位置、EVENTS 编号 | 3 / 404 bytes | dirty 且检查点到期，或入睡前提交 |

每域独立提交，没有跨域原子事务。Config 候选提交成功后才发布 cache；State 区分已提交 cache 与待保存副本，保存失败不丢弃候选。Event 在 RAM 先记事件，失败保留 dirty。启动需要的数据校验/保存失败时，原有输出授权保持关闭，普通 `SaveParam` 不会绕过该门禁。

`CFG2` 的 322-byte payload 及通信 `0x2E05=2` 保留；它与内部 journal schema 3 是不同层次。`0x2E87` 保留原位置，恒为 1，没有老化更新宏；废弃的 `0x2E10=6` 返回非法值（异常码 3），不再提供成功空操作。SN/电流校准继续要求 AFE 已授权会话。诊断原 Factory 槽位保留 NOT_RUN、地址/大小为 0，原运行模式字段为 0；其他诊断字段位置不变。

## Flash 布局

TLSR8251 512 KiB：

| 区域 | 地址 | 大小 |
|---|---|---:|
| Firmware A | `0x00000..0x1EFFF` | 124 KiB |
| OTA A meta | `0x1F000..0x1FFFF` | 4 KiB |
| Firmware B | `0x20000..0x3EFFF` | 124 KiB |
| OTA B meta | `0x3F000..0x3FFFF` | 4 KiB |
| Config | `0x40000..0x43FFF` | 16 KiB / 4 sectors |
| State | `0x44000..0x4BFFF` | 32 KiB / 8 sectors |
| Event | `0x4C000..0x5BFFF` | 64 KiB / 16 sectors |
| 未分配 | `0x5C000..0x73FFF` | 96 KiB |
| SDK 保留 | `0x74000..0x7FFFF` | 48 KiB |

SDK 当前 825x 定义 SMP pairing `0x74000`、MAC `0x76000`、calibration `0x77000`，master pairing `0x78000`；整个 SDK 保留区不进入业务擦除。旧文档中的 `0x7F000` MAC 是其他 MCU 分支定义，不能用于 TLSR8251。

| Flash 容量 | Config base | State base | Event base |
|---|---:|---:|---:|
| 512 KiB | `0x40000` | `0x44000` | `0x4C000` |
| 1 MiB | `0xB0000` | `0xB4000` | `0xBC000` |
| 2 MiB | `0x1B0000` | `0x1B4000` | `0x1BC000` |

三种容量都使用 4/8/16 个 4 KiB sector，地址仅由 `flash_store_cfg.h` 定义；OTA 兼容门禁及 SDK 地址冲突由现有布局检查验证。硬件型号与当前 OTA 配置仍需按实际设备验收。

## 一致性、失败与寿命

32-byte 固定头包含 magic、格式、schema、长度、sequence、CRC32 和提交标记；数据显式 little-endian 编码，提交标记最后写。平台按 256-byte page 边界编程，并逐段读回验证；擦除也读回验证。加载扫描所有槽位，选择最新完整且 CRC 正确的记录，跳过写中断残留。轮换不擦除最后有效记录所在 sector。

读取结果区分空白、版本不适配、损坏和端口报告的 I/O 错误。开发期空白/旧格式可使用默认并保存；端口明确报告 I/O 错误时初始化失败，不能把读取失败当空白写默认。Telink SDK 的 `flash_read_page` 没有错误返回，当前端口无法识别所有物理读故障；CRC 不等于完整硬件故障检测。

保存失败保留重试游标。擦除成功后编程失败，跳过受影响槽位，继续使用该 sector 的后续槽位，不因五秒重试反复擦同一 sector。每域连续三次实际擦除/编程失败后，本次启动停止物理写入；OTA、Flash lock 或退避拒绝事务不消耗该预算。成功保存清零连续失败计数，MCU reset 重建预算；频繁 reset 仍可能再次产生擦写，不能据此保证异常电源下寿命。

正常 State/Event 采用 60 s 检查点、失败 5 s 退避。Config 相同编码直接成功，不增加 sequence 或物理写入。寿命主要取决于变化频率，而不是读取次数：

| 域 | 每 sector 槽位 | 全区域一轮记录数 |
|---|---:|---:|
| Config：32+322 对齐为 356 bytes | 11 | 44 |
| State：32+44=76 bytes | 53 | 424 |
| Event：32+404=436 bytes | 9 | 144 |

Event 从 8 增至 16 个 sector。同样持续 dirty、每分钟保存一次的假设下，平均每 sector 约从 20 次擦除/天降到 10 次；State 同假设约 3.4 次/天。无变化时不会持续保存，显式写入/入睡会增加次数，写失败与频繁重启也会改变分布。实际寿命需用实装 Flash 的保证擦写次数、温度、数据保持要求和设备负载计算，不承诺固定年限。

## Event 行为

- 仍是 100 条记录、ID 0..20、最新在前，读取 `0xC008..0xC06B`；每条仍为 event ID 高字节与时间码低字节。
- 故障发生沿记录，恢复只释放 latch；CBC 恢复不再误记 CBC_ERR。AFE1 错误对应原事件 ID，不新增事件或改变 ID。
- 时间由真实 32K tick 差值累计，处理小数秒和 tick 回绕，不再把一次调用视为一秒。相邻时间更新须短于约 37 小时的回绕周期。
- 相邻同 ID 在首条事件之后 60 s 窗口内合并，重复计数独立保存并饱和到 65535；checkpoint 不会终止合并，增加计数仍会标记 dirty。Startup/Sleep 不合并，重启不合并上一启动的记录。
- 时间码保持：Startup=0，间隔不超过 60 s 为 171，1..168 表示向上取整小时，超过 168 小时为 170。保存时验证 ID、时间码与计数，不接收不一致 payload。
- Sleep 表示进入低功耗的尝试，不证明硬件已睡眠；失败保存的同次尝试不重复追加，物理转换取消后解除 latch，下一次尝试可另记。
- 采样仍约 1 s，短于采样间隔的瞬态可能漏记；未增加 MOS 物理反馈、故障恢复历史或新日志状态命令。突然 reset 仍可能丢失未提交的 RAM 窗口。

`0x1007` 的 `0x06`/`0x10` 写入返回 illegal address，广播不应答且无副作用。Windows 工具保留读取和解码，内部工厂工具的清空按钮与发送路径已移除。

## SOC 当前存储逻辑

SOC 输入只由合格的新样本推进，通常约 200 ms；重复、无效或过大 gap 不虚构积分。名义容量、化学体系、OCV、死区属于 Config；实时 estimate/display 与积分余量属于 RAM；State 只消费整数 SOC 0..100、放电累计百分比 0..100、cycle 0..65535 和更新编号。

容量学习删除后，Config payload 仍为 322 bytes：原开关偏移 253/254 写零、读取忽略；State 仍为 44 bytes：0/4/8 为 SOC/放电累计/cycle，12..39 写零、读取忽略，40 为 SOC_STATE 编号（little-endian u32）。schema 3 和编号保持，已有效保存的 SOC/循环继续使用；旧学习容量不再参与满容量计算。读取和重新保存旧保留槽的回归验证了这一边界。

SOC/循环变化由主循环汇入待保存副本，检查点到期且编码改变时提交；显式设置先成功保存再修改算法值。计划休眠/断电主动提交当前 SOC、放电累计及 cycle。启动从最后有效 State 恢复这三个值，并使用名义容量与循环 SOH 重算满容量/剩余容量；积分余量、显示跟随、OCV 静置、ETA 状态重新建立。

没有持久化每一笔库仑积分、绝对剩余容量、掉电时刻或当前保护 latch。突然掉电可能丢失最近未提交的约 60 s 变化及不足整数百分比的积分余量；State 和 Event 是历史数据，不能用它们替代当前保护恢复资格。此次保留这些策略，没有更改 MCU reset 后输出授权规则。

## 当前低功耗逻辑

| 产品/路径 | 进入条件与动作 | 持久化/恢复 |
|---|---|---|
| D008 普通 suspend | OTA/Flash/总线/样本门禁通过、有效电流绝对值小于 500 mA；BLE 连接事件间也允许；应用采样唤醒约 200 ms | RAM 保留，不为每次 suspend 擦写 |
| D008 ACC 深睡 | ACC 高稳定 200 ms；等待事务结束和 BLE 断开；关广告，AFE shutdown；PC4 保持高 | State/Event 必须保存成功；ACC 低唤醒，完整 reboot；CHG_IN 不作为该路径唤醒源 |
| D008 显式断电 | `0x1102=0x000A` 锁存请求，等待应答发完；AFE shutdown 成功后最后拉低 PC4 | State/Event 必须成功；失败保持供电/请求并 5 s 重试 |
| D008 自动低压断电 | 合格样本、BLE 未连接且无事务；单体 min<2550 mV 1 h，<2800 mV 1 h，<3000 mV 且非充电 24 h | 复用上述断电路径，阈值/时长未改 |
| SH 三产品深睡 | 原有开关关闭 3 s、低压定时或 AFE 通信错误 30 min 请求；OTA、Flash、UART、唤醒 PAD 门禁；先 AFE sleep 再复查 PAD | 此次补 State/Event 尽力保存；失败上报仍按原策略入睡；转换返回解除 Sleep latch |

SH 默认 `SH3673510_FIXED_UART_BLOCKS_PM=1` 会阻止 suspend 与显式 deep sleep，不能把存在入口当作默认设备可入睡的证据。SH 的低压窗口保留 min<2550 mV 1 h、<2800 mV 1 h、<3000 mV 且无充电 24 h。唤醒网络有效极性、Gate 行为及耗电需实板确认；深睡启动/AFE 恢复仍走原有流程。

D008 保存失败阻止主动断电，SH 保持低功耗优先，这是当前已有产品策略差异。此次没有统一成新的掉电安全政策，没有新增空闲低功耗入口或改变保护恢复判据。

## 验证与实板边界

回归覆盖编码、CRC、逐字节中断、sector 轮换、部分槽位后续利用、连续失败预算、门禁不耗预算、I/O 失败不写默认、cache-only 读取、事件合并/时间/取消、更新编号和协议。四产品实际 sources 与 TC32 ELF/MAP/resource 单独验证；不会自动生成 BIN。

实板仍需验证擦写过程中掉电、复位风暴、低电压写入、Flash 读回故障、最大擦写时间/watchdog/BLE 延迟，以及 ACC/AFE/MCU 进入及唤醒顺序。Host 和 ELF 不证明这些硬件行为。
