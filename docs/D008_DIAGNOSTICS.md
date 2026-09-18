# D008 只读诊断闭环（schema 1）

## 目的与边界

定位启动存储失败及 MOS 输出阻断，复用 UART/BLE 的 Modbus 入口。只读诊断不执行 Flash/I2C，不清除错误、不恢复启动升级门禁、不新增参数域。所有生产者和协议处理位于主循环；ISR 不写诊断结构。固定 RAM 约 2 KiB，无 malloc、无日志 Flash 擦写。

固件在 `refactor/d008-common-bms-features`；Windows 在 `feature/windows-afe-hw-protection-editor-v2`，客户/内部版共用 `Shared/BmsDiagnostics.cs`、`BmsClient.Diagnostics.cs`、`MainWindow.Diagnostics.cs`。

`Requested`、软件允许、最近 AFE Command、有效 R6 Driver Flag 分开。允许状态表示方向的正常请求资格，AUTO_DIODE 仍可提供反向续流；不能据此认定 Gate/Vgs 或物理导通。物理反馈始终 unavailable。R6 来自现有有效采样的 `DVC1124_REG_CC2_L_FLAGS`，不是 R1 STATUS。

## 诊断 ABI

只接受标准 Modbus 0x03，单帧 1..125 words。`0x2A00..0x2AFF` 与 `0x2B00..0x2DFF` 不可跨区读取；跨任一边界/地址回绕拒绝。0x06/0x10 涉及诊断区时在全部旧地址副作用之前拒绝，exception 02；广播不响应。每 word 大端，32 位值低 word 在前。禁止直接序列化 C struct。

下表 offset 相对 `0x2A00`，均为十进制。未定义位置为保留零，不代表一个有效硬件量。

| Offset | 类型 | 含义 |
|---|---|---|
| 0,1,2,3 | u16 | magic=0x4447、schema=1、capabilities=0x003F、boot frozen bit0 |
| 4..5 | u32 | RAM 快照变更序号，模 2^32 |
| 6..7 | u32 | 读取帧时 SDK 32k tick，模 2^32 |
| 8..9,10..11,12 | u32,u32,u16 | Trace 最新序号、覆盖次数（饱和）、有效条数 |
| 13,14,15 | u16 | bit0 SW/bit1 HW/bit2 Production Build、AFE=0x1124、MCU=0x8251 |
| 16,17 | u16 | Flash SDK capacity code、实际 layout_supported 判定 |
| 18..19,20..21 | u32 | boot address（FFFFFFFF=不可用）、Flash bytes（0=未知） |
| 22..23 | u32 | BMS_DIAG_BUILD_ID；`bms.py` 自动注入当前 Git SHA 前 8 位，缺少 Git 元数据时为 0 |
| 24,25,26 | u16 | 启动 bit0 参数有效/bit1 升级完成、AFE 配置初始化结果、参数加载校验结果 |
| 32+16*d | 16 words/domain | d=0 CONFIG，1 STATE，2 FACTORY，3 EVENT，见下表 |
| 128,129 | u16 | bit0 CHG/bit1 DSG：Requested、软件允许 |
| 130,131 | u16 | 最近 R81 command/readback、有效标记；失联/关机后无效 |
| 132,133,134 | u16 | 最近 R6（低两位 CHGF/DSGF）、有效标记、物理反馈有效=0 |
| 136..137,138..139 | u32 | CHG/DSG 阻断原因位图 |
| 140..141,144 | u32,u16 | driver 最近采集/失效 tick、运行参数/升级有效位 |
| 146,147 | u16 | 最近有效采样的 backend CHG/DSG 原因；失联时为历史值，需结合通信原因 |
| 160+2*i | u32 | i=0..5 program calls、erase calls、verify failures、deferred writes、max program ticks32k、max erase ticks32k |
| 176,177,178..179 | u16,u16,u32 | 底层首次失败、最近失败、最近失败地址 |
| 180..183,184..185 | u16[4],u32 | 各域最近运行结果、底层首次失败地址 |

capabilities bit0=Boot，bit1=Trace，bit2=Storage，bit3=MOS，bit4=启动升级详情，bit5=Runtime Diagnostics。有效位为 0 时不展示成功/正常；Flash 大小取当前 SDK 识别值，不是重新检测芯片。

域记录相对 offset：0..1 base，2..3 配置 size，4 初始化尝试次数（饱和），5 启动首次失败原因，6 启动最近结果，7 是否使用过默认值，8..9 首次失败 tick。初始化已 ready 的短路返回不算新尝试；退避拒绝算尝试。FACTORY 当前没有本流程初始化，保持 NOT_RUN。布局拒绝时 base=0，配置 size 仍可非零。

结果码：0 NOT_RUN，1 OK，2 PORT，3 LAYOUT，4 REGION，5 RECORD_OPEN，6 DEFAULTS，7 SAVE_FAILED，8 PROGRAM_VERIFY，9 ERASE_VERIFY，10 OTA_DEFERRED，11 FLASH_LOCK_DEFERRED，12 BACKOFF，13 INVALID，14 STARTED。DEFAULTS 不写入首次失败；record_load 无有效记录无法区分空白、旧 schema、CRC 损坏，不能显示“确定 Flash 损坏”。底层 API 不能可靠区分的失败保持通用码。

原因位 bit0..10：参数无效、启动升级未完成、输出未授权、通信未通过资格确认、软件保护、AFE 硬件保护、Open-Wire、加热、关机保持、温度无效、其他 backend 阻断。多个原因并列，不改变原保护决策。软件/硬件通路均关闭也不解除存储/通信门禁。

Trace：64 个物理槽，每槽 12 words，地址 `0x2B00 + slot*12`。偏移 0..1 sequence，2..3 tick32k，4 event，5 reserved，6..7 arg0，8..9 arg1，10..11 reserved。事件 1 BOOT、2 INIT(domain,result)、3 STORAGE(reason,address)、4 PARAMS(bits,0)、5 MOS(charge_reason | requested<<16,discharge_reason)、6 AFE(R81,valid)、7 BOOT_DONE(param_bits,afe_result)、8 DRIVER(CHGF/DSGF,valid)、9 UPGRADE、10 CURRENT_RECOVERY、11 PM_STATE、12 PROTECTION、13 SAMPLE_STATE。同一 Requested/阻断状态不重复记录；断电丢失，覆盖次数可见。32k 为 SDK 时基，不假定 32768 Hz 或硬件实测精度。

单帧不会与主循环生产者交错；跨帧并非全局同一时刻。Windows 分别读取冻结启动块及单帧运行块，保存原始帧时间；Trace 前后读 sequence，改变后最多完整重读一次，再变化则标明可能缺失。启动未冻结/明显重启标明不一致，不无限重试。

## 操作与定位

1. 打开任一 Windows 版本并连接串口/BLE。连接后自动探测；旧固件零 magic/illegal function/address 显示“不支持”，timeout 显示通信失败。
2. 打开“BMS 诊断”，点击“读取完整诊断”。完整读取会关闭周期刷新，保留此次 Trace 供导出；周期刷新只读状态，默认关闭，每 5 秒一次。OTA/工厂操作期间不开始采集；正在采集时 OTA 入口等待用户结束采集。
3. 查看 CONFIG 启动尝试/首次失败；同时检查 boot address、capacity code、layout_supported 和参数/升级门禁。两次失败不证明启动地址错误，必须匹配实际数据。
4. EVENT 有独立初始化调用，不能从 Config 的短路结果推断其未执行。
5. 点击“导出 AI 诊断包”，提供 boot/storage/mos/current/SOC/power/protection/runtime/trace/events、参数/AFE证据、manifest、summary 及仅本次采集的原始读帧。失败和取消也可保留部分证据；包内不包含授权/密码/写帧。

## 验证与未决

Host：`tests/bms_diag_host_check.py` 执行真实诊断核心与 Modbus 入口，验证边界、CRC、只读无副作用、广播、冻结及回绕。既有 storage/power/SOC host harness 链接真实诊断模块，覆盖两次失败、独立 Event、空白默认、失败退避、参数门禁及 MOS 多原因。Windows：`bms-tool-windows/test-diagnostics.ps1` 使用实际 BmsClient 与分片模拟传输，验证解码、旧固件、分页变化、取消和部分导出。

正式交付执行 source-order、TC32 clean build/check-fw/MAP/manifest/verify、cppcheck，SW/HW=1/1、1/0、0/1、0/0。用户工作区已有参数、16S 台架配置、SW=0 等修改不纳入诊断提交，交付构建与工作区台架 BIN 必须区分。

TODO_VERIFY_HW：UART/BLE 实机导出、实际启动地址与两次存储错误根因、BLE 负载对 200ms 采样的最坏延迟、运行栈高水位、MOS Gate/Vgs、异常供电/Flash 时序。Host 和构建通过不能关闭这些项。未自动烧录、未新增故障注入/CLI/Panic 持久化。


## Runtime Diagnostics（capability bit5）

保持 schema 1，不改已有 offset；使用此前保留的 `0x2AC0..0x2AFF`（offset 192..255），因此不新增第二份 Snapshot RAM。该区仍只读、主循环生产，不执行 AFE/Flash I/O。

| Offset | 类型 | 含义 |
|---|---|---|
| 192 | u16 | runtime version=1 |
| 193 | u16 | bit0 采样有效且新鲜，bit1 当前过流恢复状态 pending |
| 194..195 | i32 | AFE 原始电流 mA（软件工厂校准前） |
| 196..197 | i32 | 业务实际使用电流 mA（软件校准后） |
| 198..199 | u32 | 最近采样 32k tick |
| 200 | u16 | SOC 当前 deadband mA；仍与 D008 固定 ≤200mA 不可靠区间共同生效 |
| 202..212 | u16 | soc_est、soc_display、OCV state/center/low/high/confidence、rest seconds、learning state、capacity learned、learned capacity 0.1Ah |
| 213..214 | u32 | suspend 阻断原因位图 |
| 215 | u16 | 当前应用是否允许 suspend |
| 216 | u16 | 自动低压关机 region |
| 217..218 | u32 | 当前 region 累积秒数 |
| 219,220,221 | u16 | BLE connected、sample pending、suspend 电流门槛 mA |
| 222,223,224 | u16 | 软件保护 Level1/Level2/Level3 当前位图 |
| 225 | u16 | 1=MODE_FACTORY，0=MODE_NORMAL |

PM 阻断位：bit0 无有效/新鲜采样，bit1 OTA，bit2 Flash stack session，bit3 OWC/bus busy，bit4 双向绝对电流达到 suspend 门槛，bit5 sample pending，bit6 显式关机流程，bit7 ACC sleep 流程。该位图只解释既有 `blt_pm_proc()` 决策，不参与或改变低功耗策略。

Runtime Snapshot 由 AFE/SOC/PM 各 owner 在原有主循环路径更新；`PM_STATE`、`PROTECTION`、`SAMPLE_STATE` 仅在状态边沿写 RAM Trace，避免按 200ms 周期刷满环形缓冲。
