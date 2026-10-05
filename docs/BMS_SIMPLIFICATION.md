# BMS 应用层简化实施记录

日期：2026-10-05。审查与实现起点：`6604d738add37bf6512fdba9ea90f5b270b17c7f`，分支 `codex-bms-monorepo`。范围为 BMS core/app、DVC1124/SH3673510 后端及应用使用的通信/存储平台代码；Telink SDK 驱动源码未改。

目标是减少无消费者状态、重复编码、重复资格计数和不明确的缓冲区所有权。没有新增 manager、运行时 ops 表或队列框架。减少代码量不能替代安全边界或实板证据。

## 26 项建议的处理

| 项 | 处理 | 实现及保留边界 |
|---|---|---|
| 01 | 已实施 | 删除 SOC 无业务消费者的校准标志、旧 SOC、CHG/DSG 标志和无用途容量累计。真实积分、余数、循环及 State 编码保留。 |
| 02 | 已实施 | 一个 `soc_integrate_current(dir)` 取代三份旧包装；删除忽略参数并修改全部调用点。保留 mA × 实测时间及分块算术。 |
| 03 | 已整理 | 保留已有完整动作函数与 ETA 文件，删除死逻辑；下节固定真实动作顺序。不机械拆 SOC 文件，不删除学习/ETA。 |
| 04 | 已实施 | 删除 SH 仅清零的 `s_balance_mask`；请求与硬件读回保留。 |
| 05 | 已实施 | 全串检查通过才发布完整 SH 报告；读失败或末串非法不会发布半帧。缺串仍为 61001。 |
| 06 | 已实施 | SH feature getter 只读同轮接受的温度及有效位，移到采样状态所有者；不再额外读温度 SPI。失败/重配使缓存资格失效。charger 物理读保留。 |
| 07 | 已实施 | 三新帧计数仅在 guard；SH 查询公共资格。器件锁存、重配、物理恢复窗口、输出请求/命令缓存仍各有职责。 |
| 08 | 已收敛 | 保留触发累积衰减、恢复连续样本的运行行为，纠正文档并加入黄金序列。不将触发改为连续计数。 |
| 09 | 已实施 | SW 算法接收 const 参数/测量视图，拥有私有三等级故障字；报告边界合并 HW，SW 阻断及诊断只读 SW 来源。历史仍按兼容报告上升沿。 |
| 10 | 已实施 | profile getter 保留一次缓存读取和最终校验；独立 init、外部提交与 apply 校验保留。 |
| 11 | 已实施 | user 配置有效资格只在加载/成功发布边界计算；高频电流校准不再拷贝和校验整套 user 参数。启动失败门禁保留。 |
| 12 | 已实施 | 软件参数 `candidate → validate → save → publish`；Modbus 0x06/0x10 和默认重置不再先改全局后回滚。SaveParam 兼容入口保留。 |
| 13 | 已实施 | DVC OperatingConfig 大实现移回 `.c`；删除无调用可变 API 与 getter。已有只读诊断路径保留。 |
| 14 | 已实施 | DVC boot/basic 共用 wake、body diode、watchdog 编码；两阶段写入、20 ms 等待和顺序保持。 |
| 15 | 已实施 | SH current 量化规则单源；SC effective 取成功编程 actual，不再重复表。SW/HW profile 保持独立。 |
| 16 | 已实施 | 名称 handler 合并；SH 保存失败返回失败，旧缓存/GAP 不发布。空 suffix/qty=0 的既有产品差异保留。SDK memcpy 共用，SDK 未声明的短字符串 helper 保留。 |
| 17 | 已实施 | token/fragment 使用已有 `mb_crc16`，初值、长度和端序保持。 |
| 18 | 已实施 | 统一设备级授权：任意 BLE disconnect 撤销授权及未完成碎片，UART 也须重新 OPEN。会话与碎片期限保持独立。 |
| 19 | 已实施 | UART 共用流程；TX 全帧接受或拒绝，RX IRQ 暂停 DMA 后交付，解析完才 rearm。D008 mux 和 SH DE 差异保留。 |
| 20 | 已实施 | RS485 >50 ms 卡死在主循环停止 DMA、reset UART、释放 DE、恢复 RX；中止不计成功完成。正常 DMA/UART/min-hold 条件保留。 |
| 21 | 已实施 | SIF 主循环准备双缓冲包，ISR 仅波形推进/领取；TC32 packed 协议改显式编码，长度、校验及 MOS 缩放保持。mux GPIO 重配置回主循环。 |
| 22 | 已实施 | 共用 sample 后处理、event 收集和启动/IRQ 外壳；删除空 terminate 分支和无消费 event 内部字段。D008 ACC/LDO/shutdown 与 SH 电源路径保留。 |
| 23 | 已实施 | 板级不可达条件、未使用 HID 数据及注释属性删除；活动 Battery/SPP/OTA 属性、UUID 和顺序保持。 |
| 24 | 已实施 | 删除无调用 Flash inline 页写/擦除；真实平台分块写及 verify 为唯一实现。journal、commit marker、OTA/lock/backoff、schema 2 与九类编号保持。 |
| 25 | 已实施可选裁剪 | `BMS_DIAG_TRACE_ENABLE` 默认 1；显式 0 移除 1536-byte ring 及索引，能力位清除、原 trace 窗口读零。启动/运行/MOS 快照、首次存储失败、学习、ETA、事件继续保留。 |
| 26 | 保留待产品签核 | 未获得 SH 电池体系签核，不猜测修改 AUTO 或出厂容量/保护值。现有明确化学体系字段与 D008 三 profile 保留；签核后可通过现有配置路径选择并按 SOC 类别编号更新。 |

## SOC 真实动作顺序

`bms_soc_process_sample` 先做初始化/有效性、首帧、重复帧、gap 资格检查；接着计算方向及实测间隔，处理 charger/load 变化并积分。方向切换会清静置/端点时间并结束该轮策略。通过资格的时间按既有 200 ms quantum 执行策略、低 SOC 故障和显示/报告；无效帧不补填未知时间。

策略内依次更新 profile/endpoint/sag，检查 full anchor、强制 empty anchor，然后 learning quality、放电端点、idle empty、静置 OCV，最后 ETA。端点函数的优先返回与学习动作保留。显示和真实 SOC、requested/effective、参数和状态记录继续独立。

## 明确的行为修复与约束

- SH 名称落盘失败现在返回 Modbus 失败，避免上位机误以为保存成功。
- 设备级授权在 BLE disconnect 后失效，跨 UART/BLE 的操作必须重新 OPEN；wire function/字段没有改变。
- 忙、空、超长 TX 拒绝整个应答，不再截断或覆盖正在发送的 DMA 包。
- RS485 超时是中止，不是假造成功；50 ms 是主循环恢复判定阈值，主循环被阻塞时不保证硬实时到达。
- RX 是一个待处理帧的所有权交接，主站应等待应答；任意无间隔连续流仍可能丢帧，不宣称双缓冲接收队列。
- `BMS_DIAG_TRACE_ENABLE=0` 是明确选择的能力裁剪，默认生产配置仍为 1。可用 `EXTRA_DEFINES=-DBMS_DIAG_TRACE_ENABLE=0` 单独验证，并使用独立外部 `BMS_BUILD_ROOT` 区分产物。

## 验证方式与证据边界

新增实际源码 host 测试覆盖名称保存失败、滤波黄金序列、UART DMA/IRQ 交错与卡死恢复、SH 每读阶段及每串失败的完整发布、SIF 黄金字节与包所有权，以及全部 65536 个电流请求 × 三 Rsense × 三档编码（每优化级 589824 组）。既有 SOC simulator、保护/恢复、参数/存储及九类编号回归继续运行。

SIF 黄金向量冻结自基线实际 builder，使用与固件相同的 `-fpack-struct`；另以 TC32 编译和 nm 核对布局：public 20、realtime 33、cell `4+2N` byte。三个 D008 profile 共 72 个黄金包一致。活动 GATT 表去注释 token SHA-256 为 `fbd941ac3690f069f171299f40cea5e3f5260a82578c762404a3abc4f56838b3`，与基线相同；47 个保留初始化式均与基线一致。

存储故障注入验证保存失败不发布运行参数/cache、重启取最后有效记录，但部分 fixture 的参数校验器/硬件结果仍为 stub。SH 采样测试也不等于真实 SW→AFE→MOS→负载全链路。UART/SIF 模型不证明 GPIO 波形、DMA 实际时延或 tick 最坏抖动。

本次只执行无 BIN 的 host/CMake、TC32 ELF/MAP/resources 与静态检查；没有烧录/OTA。实板待验：500 µs SIF 波形及 mux 切换、RS485 最后停止位/DE/卡死恢复、SPI/I2C watchdog 静默关断、SC/OCD/OCC 物理恢复、Flash 掉电和低功耗恢复。沿用 [硬件验证清单](HARDWARE_VALIDATION.md)，不据软件通过解除发布阻断。

最终固定提交、测试结果与 MAP 对比见本页后续验证记录。
