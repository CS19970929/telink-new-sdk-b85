# D008 参数读写与 Windows 参数页

## 适用范围与升级

本次在 `refactor/d008-common-bms-features` 实施；Windows 单一真源为 `feature/windows-afe-hw-protection-editor-v2/bms-tool-windows`，客户版与内部版共享参数功能。

**Config schema 当前从 3 升至 4（此前 2→3 的说明保留为历史），无旧格式迁移。首次加载旧 schema 时恢复整个 Config 默认值，包括软件保护、AFE Requested、SOC 配置、容量、蓝牙名称。升级前先用新版上位机导出旧固件参数并保存，自定义值升级后人工核对重设。ZIP 是备份证据，不是自动恢复脚本。** State/Event 区域与格式不变，SOC/循环/日志不因 Config 升版自动清除；历史容量学习缺少容量标记时不采用。

不增加 Flash 区域，不改变 OTA 槽位，不要求更改 BLE MTU，不加入 C037 的 SH 寄存器、RTC、OCV 表或电压校准。

## 所有权与保存

- `bms_parameter_access.c` 只解释新增业务寄存器并调用已有 owner；不是第二套保护状态机。
- `bms_config_store.c` 的 journal 保存 Config；追加 54 字节明确编码的业务参数，结构体 padding 不上 Flash。
- 加热启用/启动/停止、均衡 enable/start voltage/start delta/stop delta、SN、电流偏移/增益属于 Config。保持软件保护和 AFE profile 独立更新。
- SOC/循环命令使用 `bms_state_store_set_soc_cycle`：Flash 提交成功后才修改待保存状态与运行 RAM；失败不留下稍后偷偷生效的候选值。
- 保存完成并验证后才应答成功；Flash/OTA/门禁失败返回 Modbus `0x04`，非法值 `0x03`，未知或只读地址 `0x02`。蓝牙名称写入也传播保存失败。
- `0x10` 整帧只允许一个 owner。软件保护仍先生成最终候选、完整校验、一次保存；不支持的跨区整帧先拒绝，不执行前半段。
- 不支持的读取不再伪装为成功的全零；已定义窗口内的保留字段仍按既有定义读取。

## 寄存器

使用 Modbus `0x03/0x06/0x10`，线上每字大端，32 位值低字寄存器在前。新窗口 `0x2E00` 避开既有诊断及上位机 Flash endurance 的 `0x2700`。

| 地址 | 字数 | 语义 / 权限 |
| --- | --- | --- |
| 2E00..2E02 | 3 | 只读 magic D008 / 协议版本 2 / capabilities 007F |
| 2E03..2E04 | 2 | 最近写结果 / 成功写序号；序号包括 RAM 暂存，不能作为已落盘证明 |
| 2E05..2E07 | 3 | Config schema 4 / SN 暂存 generation / 命令语义版本 1 |
| 2E08..2E0F | 8 | 编译默认容量 / 加热 enable/start/stop / 均衡 enable/start voltage/start delta/stop delta |
| 1005 | 1 | SOC 0..100%，读写、同步 State 保存 |
| 2318 | 1 | 额定容量，单位 0.1 Ah，1..6553，读写 |
| 2319 | 1 | 循环次数 0..65535，读写、同步 State 保存 |
| 2E10 | 1 | 分组命令：1 软件保护默认、2 AFE 默认、3 容量及加热默认、6 工厂模式重入 |
| 2E20..2E22 | 3 | 加热 enable / start / stop；只允许从 2E20 整组 0x10 写入 |
| 2E24..2E27 | 4 | signed offset mA / unsigned gain ppm；只允许完整 0x10，需现有 AFE access session |
| 2E28..2E2E | 7 | 原始 mA / 校准 mA / 有效且新鲜 / 32k 样本时间戳，只读 |
| 2E30..2E3F | 16 | 持久化 SN 32 字节，只读；全零代表使用编译 SN，可由 C002 读取当前身份 |
| 2E40 | 1 | 0 开始 SN 暂存；非零 generation 完整提交，需 AFE access session |
| 2E50..2E5F | 16 | SN 暂存写区，每帧最多 4 字，需活跃 session |
| 2E70..2E73 | 4 | 均衡 enable / start voltage mV / start delta mV / stop delta mV；只允许从 2E70 整组 0x10 写入 |

capabilities bit0..6 分别为容量、SN、加热、电流校准、分组重置、同步 State、均衡。未列出的 2E 地址拒绝访问。协议版本 2 的新增字段按 capability 探测，不通过猜测兼容将来版本。

SN 流程：打开现有 AFE access → 2E40 写 0 → 读 2E06 generation → 四帧各写 4 字 → 2E40 写 generation → 读回 2E30 → 关闭 session。必须完整覆盖 16 字，60 秒内提交；未提交不写 Flash，generation 错误/缺片/过期拒绝。SN 为可打印 ASCII，末尾零填充；Windows 限制 1..32 字符。最长写帧 17 字节，可用于 MTU23。读取沿用现有 BLE 应答分片。

旧 `1102=3` 与 C037 MOS 命令有冲突，现明确拒绝；工厂重入改为能力声明后的 `2E10=6` 且需 access session。既有 `1102=0A` 关机指令保留。

## 加热

温度编码 `(℃+40)*10`，协议范围 -40.0..125.0℃；只接受 `enable=0/1` 且 `start<stop`。该范围是编码范围，不是硬件允许的安全设定建议。默认沿用现有宏：enable=1、start=0℃、stop=5℃，不替用户改温度策略。

下一次原有加热服务采样周期使用新配置。关闭业务加热不会跳过既有加热回路过温/粘连/熔断处理；PB1 不用作充电检测。D008 以可靠充电电流作为 charge-session 进入事件，低温时先进入 ARMING 禁止充电方向，下一帧确认 Ichg 消失后才真正开 Heater；因预热主动关 CHG 导致的零电流不会清除 session，可靠放电电流会立即退出 session 并关闭 Heater。DVC common-port 对 Heater 的单侧充电禁止使用 AUTO_DIODE，保证合法反向放电路径。

## 均衡

均衡参数与软件压差保护参数完全独立，禁止复用 `u16VdeltaOvp_First/Second/Third`。默认：`enable=1`、`start voltage=3400mV`、`start delta=50mV`、`stop delta=30mV`；start voltage、start/stop delta 均通过 `2E70..2E73` 整组修改并原子保存。当前 3400mV 是 D008 默认 16S LFP 的业务默认值，量产值仍需电芯/热测试签核。

Balance 必须先通过电压可信门禁：cell count、单体物理范围、min/max/delta 自洽、单帧跳变、总压差 sanity limit 和连续稳定确认。任何异常先请求均衡 OFF 并进入 OpenWire suspected；OpenWire active/suspected/confirmed、Heater ARMING/ACTIVE、AFE/NTC/短路/过流/低压/温度等 hard fault 均禁止均衡。Cell OVP 不一刀切禁止均衡，在 charge session 和测量可信时允许停充后继续泄放高单体，形成 COV 恢复闭环。

DVC COW 诊断在约 1s 的 100uA 下拉有效窗口内采样，当前约 200ms 后捕获新的 cell snapshot；使用中的 cell 在诊断窗口为 0mV 时判定 open。诊断确认无断线后仍需重新累计稳定样本，才能恢复均衡。

## 容量和电流校准

额定容量修改保留 SOC 百分比和循环，清除运行中的容量学习。学习记录 flags 高 16 位绑定学习时额定容量；容量 Config 已保存而 State 清理尚未写入时掉电，旧容量学习不会套到不同额定容量。学习清理仍经原 State 检查点保存。

电流公式：`current_ma = trunc((raw_current_ma - offset_ma) * gain_ppm / 1000000)`，结果饱和至 ±INT32_MAX；极端输入先对减法饱和，正常测量范围不受影响。offset ±1000000 mA，gain 100000..10000000 ppm；默认 0、1000000。使用有界 32 步整数运算，无动态内存、浮点或 TC32 缺失的 64 位运行库依赖。

原始值保留在 snapshot.raw_current_ma 和新只读寄存器；校准值进入现有软件保护、SOC、显示及 PM 电流路径。**软件校准不会改变 AFE 硬件过流/短路的模拟量化与 Rsense 配置，不能代替硬件校准。** 内部上位机取得 10 个不同时间戳的新鲜样本，显示均值/范围，只生成候选；须用外部仪表确认，再点击保存。客户版没有校准写入界面。

## 量产写权限

`BMS_PRODUCTION_BUILD=1` 时，诊断窗口、原始/校准电流、SN、AFE requested/effective 等读取保持可用，不影响 BLE/串口售后诊断。

SN 暂存/提交与软件电流 offset/gain 写入属于工厂身份/校准操作，量产构建要求同时满足：

1. 设备处于 `MODE_FACTORY`；
2. 现有 AFE access session 有效。

设备离开 Factory Mode 后，这些写操作返回授权失败；需要返工时先通过受控 `2E10=6` 流程重新进入 Factory Mode。开发构建保持原有 session-only 行为，方便台架调试。

这是一层防误操作和生命周期门禁，**不是密码学认证**；当前 AFE access 固定 magic/session 仍不应描述为防攻击安全边界。容量/SOC/循环/加热等业务参数继续按各自既有权限和原子保存规则处理。

## Windows 使用

连接后新增“D008 参数”页，能力探测区分不支持和超时。现有 Windows 分支支持手动全读、容量/SOC/循环/SN、加热整组编辑、分组默认和 ZIP 导出；**本次先只落 D008 固件，2E70..2E73 均衡编辑尚未同步 Windows 真源，不能声称 UI 已支持。**客户版遵循高级功能解锁；内部版额外有电流零点/增益操作。

写入前重新检查协议版本，暂停轮询并复用 BmsClient 通信锁和既有 OTA/任务互斥；切换设备后必须先读取新设备。成功后读回比对，超时/失败提示重新读取，不盲目重发提交。

旧固件不能操作新增写命令，但仍可备份已存在的软/硬保护及设备身份/蓝牙名称。部分读取失败仍导出 `parameters.json`，包含时间、原始寄存器值、错误项；不包含密码、access token 或 SN generation。备份不是跨时刻的原子整机快照，不提供自动导入。

分组默认不会清除 SN、电流校准或事件日志；业务默认涉及容量、加热和均衡；不清除 SN、电流校准或事件日志。AFE 默认仍走原事务，应用失败回滚行为及 CONFIG_INCONSISTENT 语义不变。

## 实板验收 TODO_VERIFY_HW

1. 升级前导出旧参数；升级后确认 schema 4 默认值，再按已确认参数重设。
2. BLE MTU23/串口分别写加热整组、均衡整组、容量、SOC、循环和 SN；复位读回。均衡 Windows UI 同步前先用协议/CLI验证 2E70..2E73。
3. 断开、超时、OTA 占用及中途掉电后读回；不得把未确认应答视作失败且盲目重复提交。
4. 实际温度跨越启停点、加热禁用、无效 NTC、回路异常时验证安全动作；覆盖低温插充电器→ARMING→Ichg消失→ACTIVE、加热中转放电、充电器弱源/拔出等场景，不得仅凭字段宣称硬件验收。
5. 外部仪表校准零点/正负电流，复位保留；核对软件电流保护、SOC积分、suspend 退出条件及硬件保护仍按各自参数动作。
6. 均衡验收必须覆盖正常50mV阈值、可调起始电压、49/50/51mV边界、断线/接触不良/单体突跳、OpenWire、COV停充后继续均衡、AFE写失败与actual mask读回。
7. 测量最坏 Flash 保存时延、BLE 链路、栈高水位及功耗；host tests/TC32 构建不替代实板结果。
