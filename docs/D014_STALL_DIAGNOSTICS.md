# D014 首次停顿与 AFE 恢复取证

实施起点：`5ff3ef7c4f408cc5e5a3e808e2a1248b37c620fc`，分支
`codex-soc-coulomb-ocv-refactor-20261020`。本轮只实施已证实的 SH WDT 恢复缺口和可选诊断，
没有证明或修复“最初 LED/BLE 同停”的唯一原因。实板确认范围仍只有 D014 / BT_cs001。

后续以 `78e4cfa9` 为起点补充 schema 2：启动计数变化仅表示重新进入main，DeepSleep唤醒同样会增加计数，
不能直接称为异常复位。增加跨DeepSleep的REG7调用标记及原因，并提供正常基线对比解码；schema 1仍可离线读取，
但不能把其原保留word13的0解释为“排除DeepSleep”。本次补充同样未执行编译/host/实板验证。

## WDT 修正及依据

原链路为 `FLAG1 → FLAG2 → BSTATUS1/2`。即使 FLAG2 已确认 WDT，后面的 BSTATUS
失败仍先返回普通状态错误，跳过 WDT；已设置的恢复待办也会被这个提前返回遮蔽。
持续失败再叠加关断无法验证时可能进入 35s 总线静默，超过已置 WDT 的清除窗口。
单次 BSTATUS 失败不等于必然 Powerdown，这个缺口也不能解释 LED/BLE 的首次同时停顿。

现在 `valid_fields` bit0/1/2 分别表示 FLAG1、FLAG2、完整 BSTATUS1/2 已成功读取。
后续读取失败仍交付有效 FLAG2；调用方在普通状态错误前处理“有效 FLAG2 的 WDT”或已有恢复待办。
未成功读到 FLAG2 且无待办时，不根据缓冲内容清 WDT；部分 BSTATUS 不作为 MOS OFF 反馈发布。

确认 WDT 时先作废旧 FET 命令和测量资格，禁止两方向输出，再调用原专用恢复：验证关断功率通路及均衡，
LTCLR/W0C 清 WDT 并回读，恢复完整运行/保护配置且保持两侧 OFF。失败继续禁止输出；成功后重新等待
必要温度初始化及完整测量。真实保护、短路锁存、配置禁止、1s 通信确认和关断未确认时的 35s 静默保留。
没有增加 RESET、额外日常 SPI 喂狗或故障期间无限重试。

芯片依据为本机 `references/afe/AFE_完整开发参考库_20261005/sources/SH36735XX CV1.0A.pdf`
物理页17（WDT、有效通信、LTCLR及9.8s Powerdown条件）、34（SCONF5的WDT使能/档位）、40（FLAG2及W0C）。
32.34s 是当前编译默认档位，不能代替实板有效配置核对；版本和实装适用性仍按
[资料指南](AFE_REFERENCE_GUIDE.md)核验。真正 Powerdown 不能由本次软件恢复保证重新启动。

## 开关和业务边界

独立开关 `BMS_STALL_DIAG_ENABLE` 默认 **0**；不依赖完整服务诊断、trace、栈扫描或运行日志。
只有 D014 / B85、当前 `PM_TIM_RECOVER_MODE=1` 且DeepSleep retention关闭时允许开启，production 明确拒绝。
四产品 sources.txt 登记同一平台源文件，关闭时该源文件为空，记录宏的实参不求值。
新只读窗口关闭时仍返回 magic/schema/enabled=0，防止误以为设备已经记录现场。

后续授权开发验证时，可用以下配置；这些命令本轮**未执行**，不生成 BIN：

```powershell
$env:BMS_BUILD_ROOT = "$env:LOCALAPPDATA/CodexTemp/bms-d014-stall/development"
$env:EXTRA_DEFINES = '-DBMS_STALL_DIAG_ENABLE=1'
python bms_tools/bms.py --product d014 link --jobs 4
python bms_tools/bms.py --product d014 resources
```

诊断不参与任何业务决策，不改变 Suspend 许可、SDK 参数/完整返回值、200ms 期限、SOC 实际时间积分与
400ms gap 处理、485 10s 供电策略、保护、配置或 Flash/OTA 布局。PC0/PC1 不变。
PM 包装只转交一次原 `cpu_sleep_wakeup`；原驱动负责睡眠、唤醒和时钟恢复。
IRQ 只增加固定计数并保存原始 IRQ 源；应用唤醒回调只增加计数和更新缓存，不执行采样或格式化。

运行记录只读 system tick 和数字寄存器，并复用原有 `bms_time_*()` 已取得的 32K 值；不增加模拟32K读取、
AFE访问、主动通信、Flash写入、喂狗或新的保活/唤醒。启动递增B85 `DEEP_ANA_REG1=0x3B` 的8位启动计数；
schema 2另在启动捕获并清除REG7，在DeepSleep驱动调用前/返回后写入/撤销REG7标记。普通Suspend和每轮任务
不新增模拟总线访问。SDK `drivers/B85/lib/include/pm.h` 允许REG1跨芯片/WDT复位保留、掉电清零；
REG7跨DeepSleep保留，WDT/chip reset/掉电清零。检查实际编译源码及 `liblt_825x.a`、`liblt_general_stack.a`
后，未发现REG7的直接模拟寄存器访问；库中的立即数0x36还用于SMP参数，不能只按数值认定寄存器占用。
这不是对所有动态寻址的形式证明，仍需D014实板核对。SDK REG0晶振bit1、REG2系统用途和既有REG6标签不改。
其它产品/MCU的保留寄存器所有权尚未审查，不能迁移开启。

开启后有额外指令、RAM/ramcode以及启动/DeepSleep标记的模拟总线操作，不能承诺功耗或指令时序完全相同；资源以未来实际
ELF/MAP 为准。本轮未编译，不报告推测资源值。默认关闭适用于普通包，复现取证必须明确使用开启的开发包。

## 最小复现与读取

1. 诊断包启动后按第4步完整读取一次，确认schema=2、enabled=1、build和启动计数，保存160word为正常基线，
   然后断开有线和BLE轮询。旧schema=1只能使用原有RAM阶段和计数，不能提供新增DeepSleep标记。
   使用设备实际参数，不改保护性休眠配置来“帮助复现”。故障发生多久由实测记录，不假设固定周期。
2. 观察 LED 停止后，记录墙钟时间；不要先重启、重新烧录、清故障或写参数。
3. 发起 485 唤醒，允许丢弃首帧，重发一次正常读取。立即保存下表全部160个word和常驻休眠状态
   `0x3300` 窗口；不要让上位机自动循环读取。之后可再读一次 header，确认未跨启动。
4. 用既有 Modbus 03 任意寄存器读取工具分两段读取：`start=0x3A00,count=80` 和
   `start=0x3A50,count=80`。将返回数据按地址递增拼成160个0..65535整数的 JSON 数组。
   也可保存纯寄存器数据320字节大端 `.bin`，必须去掉 RTU 头和 CRC。
5. 离线解码，不访问设备：

```powershell
python -B bms_tools/decode_stall_diag.py C:/Users/Administrator/Documents/CodexOutputs/d014-stall-words.json
# 同一诊断固件的故障后数据，与正常基线比较；输出为证据提示，不替代现场判定。
python -B bms_tools/decode_stall_diag.py C:/Users/Administrator/Documents/CodexOutputs/d014-stall-words.json --baseline C:/Users/Administrator/Documents/CodexOutputs/d014-normal-words.json
```

解码器保留原始word；输出打印到终端，按需保存到树外。输出解释必须结合 header 和基线。
新地址只允许03，06/10及跨窗口写入由已有诊断地址门禁拒绝，不增加专有功能码。
两次读取期间 live 会继续更新，不能当作同一时刻的原子全局快照；冻结区只写一次，不因读取清除。

若485仍不能恢复通信，RAM已更新的最后阶段和不对称计数可供后续只读调试器取证，但本轮没有附加调试器。
要区分“仍在物理 Suspend”与“已唤醒却卡在 PM 内部等待”，还必须取得停顿时 PC/调用栈和
`0x80074C/0x80074F/0x800750` 等时钟状态；这些读数不能由恢复后的快照倒推出。
不能以新增记录为理由忽略这一证据边界。

## 寄存器 schema 2

窗口 `[0x3A00,0x3AA0)`，160word。word内大端，所有u32均高word在前；计数自然回绕。

| header word偏移 | 内容 |
|---|---|
| 0/1/2/3 | magic=0x5354 / schema=2 / enabled / words=160；除word13启用外，布局与schema 1相同 |
| 4/5 | 首次长间隔类别 / 恢复后观察阶段 |
| 6..7 | build ID |
| 8 | 高8位=启动前REG1值，低8位=本次递增值 |
| 9 | `cpu_wakeup_init()` 前数字寄存器0x72原始值，只读不清；WDT指示为bit0 |
| 10/11 | 启动前DEEP_ANA_REG0 / REG6原始值；REG0 bit1为SDK晶振启动失败标记 |
| 12 | 初始化后 `pm_get_wakeup_src()`；这是启动缓存，不是最后一次Suspend原因 |
| 13 | schema 2：`cpu_wakeup_init()`前捕获的REG7原始值；schema 1保留0，无此证据能力 |
| 14..15 | 首次观察长间隔的ms，按对应时钟模周期计算 |
| 16..79 | live，32个u32 |
| 80..143 | 首次长间隔冻结副本，布局同live |
| 144..159 | 首次AFE错误/WDT观测，8个u32 |

live/冻结副本按下表u32序号排列，每项占2word：

| u32序号 | 字段 |
|---|---|
| 0/1 | 最后阶段 / 该阶段system tick |
| 2/3/4 | 主循环次数 / SDK返回次数 / 采样尝试次数 |
| 5/6/7/8 | 最近采样开始/结束、SDK进入/返回tick |
| 9 | 应用唤醒回调次数，不覆盖外层SDK/PM阶段 |
| 10/11/12/13 | 底层PM进入次数/返回次数/进入tick/返回tick |
| 14/15/16/17 | 原PM mode / 原wake source mask / 原deadline / 完整返回值 |
| 18 | 最近一次已有bms_time调用取得的原始32K tick |
| 19/20 | 最近记录的sample_due / 应用布防期限；撤销时期限为0，非SDK内部状态回读 |
| 21/22/23 | IRQ进入次数/退出次数/最近进入时IRQ源 |
| 24 | bits7..0=0x74C，15..8=0x74F，31..16=0x750的16位值；PM返回后才读取 |
| 25 | PM返回后的pm_timRecover.recover_flag |
| 26/27 | AFE事件次数 / 最近stage高16位+error低16位 |
| 28/29/30 | guard诊断bit / 静默已过ms / 目标等待ms |
| 31 | 阶段detail；bit31表示正执行原有32K读取；其余位在SAMPLE_END为原sample result、DEEP_COMMIT为原发布原因、DeepSleep PM_ENTER为REG7标记、SH事务后为最近操作(0x100读/0x200写 OR 寄存器地址) |

AFE首次事件：stage/error打包、system tick、缓存raw32K、有效mask高16位+FLAG1中8位+FLAG2低8位、
CRC累计、retry累计、guard bits、当时阶段。`stage=0x0100` 表示有效FLAG2已观察到WDT，未必是SPI错误；
其余stage见 `bms_diag.h` 的 `DIAG_AFE_FAIL_*`。有效字段与错误码必须一起解释；未读到不等于0。
首次事件可能发生在启动阶段；RAM中的最近stage和累计事件仍继续更新，不能把事件计数当成失败采样次数。

阶段0..14：BOOT、LOOP、SDK_ENTER、SDK_RETURN、UART、SAMPLE_BEGIN、SAMPLE_END、STORAGE、
PROTOCOL、POWER、PM_ENTER、PM_RETURN、APP_WAKE、DEEP_COMMIT、BOOT_HOLD。BOOT_HOLD仅由明确的启动保持入口标记，
不把初始化阶段的所有未知DeepSleep调用都归为boot hold。

长间隔类别0..5：NONE、PHASE、SDK、PM、RAW_32K、SAMPLE。超过400ms才冻结，恰好400ms不冻结；
这是取证阈值，不是新保护或ADC活性门禁。首次超过阈值不一定就是用户最终看到的故障，需核对时间、阶段和计数。

REG7标记为 `0xD0 | reason`：reason 0..8沿用 `BMS_SLEEP_REASON_*`（命令1、开关3、低压5/6、AFE7、过放8）；
14表示没有应用提交记录的DeepSleep调用，15表示业务初始化前的boot sleep hold。普通Suspend不写REG7。
原因仅复制PM已发布的值，DeepSleep前保存的是调用意图，不是物理进入成功反馈；原驱动返回即撤销标记。
若驱动未返回而下次启动仍捕获该标记，结合官方保留规则和PAD启动状态，支持DeepSleep唤醒链。
word13是本次启动之前的缓存；若故障前后启动计数未变，它仍是基线之前的历史，不能作为本次故障DeepSleep证据。
无标记不能排除DeepSleep后又发生WDT/复位/掉电、其它未经过wrapper的入口或标记未成功写入。
BOOT_HOLD只说明最近一次启动保持，不能恢复更早那次保护深睡的原始原因。

PM完整返回位也要一起读，不能只看mode：B85 `pm.h`定义bit30=`STATUS_ENTER_SUSPEND`、bit8=`STATUS_GPIO_ERR_NO_ENTER_PM`，
低位bit1/2/3为Timer/Core/PAD。当前 `liblt_825x.a` 的 `cpu_sleep_wakeup_32k_rc+0x2F8..0x2FC` 在正常
sleep_start之后的返回路径OR入bit30；另有+0x94直接返回PAD值8、+0x3CA返回0x100等路径。
bit30存在支持经过SDK正常入睡/唤醒返回，仍不能说明整个长间隔都处于物理Suspend；mode=Suspend本身只是请求参数。
解码器只在非零的PM进入/返回次数配对时解释最近一次返回，否则返回值可能属于上一次调用。

## 如何区分候选链

| 观察 | 可以支持的判断和限制 |
|---|---|
| 启动计数较基线改变，冻结区为空或是本次启动新现场 | 期间重新进入main，包含DeepSleep唤醒和复位；不能再称原调用续跑，也不能仅凭计数称异常复位 |
| 新启动捕获有效REG7 DeepSleep标记及匹配PAD状态 | 支持DeepSleep唤醒，进一步区分命令/低压/AFE/过放/开关或boot hold；标记本身不是物理入睡反馈 |
| 计数变化且WDT位相对基线由0变1 | 支持期间发生过WDT复位，但不保证它就是最后一次启动原因；可能还经历其它启动 |
| PM长间隔、mode为Suspend、source含Timer，485后PM正常返回 | 长间隔跨越原底层睡眠调用；支持睡眠/唤醒恢复链。无法仅由此区分定时唤醒未发生或PM内部时钟等待 |
| SDK长间隔，但PM次数配对、底层PM没有对应长间隔 | 优先检查SDK外层调度/等待；结合IRQ和应用回调计数，不能简单归为遗漏Timer |
| SAMPLE_BEGIN后长间隔，SDK先已返回 | 检查AFE/保护/外设调用；不再把首次停顿归于SDK前置调用 |
| STORAGE/PROTOCOL/POWER阶段停留 | 定位到该任务后继续取PC；AFE持续错误本身不应阻塞LED翻转 |
| DEEP_COMMIT或PM mode为DeepSleep | 主动保护睡眠路径；普通485是否恢复仍需核对期间reset及实际原因 |
| 首次AFE有效WDT晚于长间隔起点，之后进入静默 | 支持“MCU先停顿、AFE随后WDT”的先后；计时分辨率/跨启动要一致，不能据此证明唯一硬件原因 |
| 首次AFE通信错误早于MCU长间隔 | 支持AFE先异常的候选，继续看当时guard、阶段及后续恢复；不强行合并两问题 |

system tick约268s回绕，raw32K约37.3小时回绕；差值是模周期，不保证更长停顿的真实时长。
raw32K只在本来就有的读点保存，不能作为独立外部时基证明自身时钟正常。
普通Suspend保留RAM；DeepSleep/reset/掉电丢失RAM。REG1计数8位回绕、掉电清零，无签名，不是跨固件/跨掉电唯一boot ID。
计数不变仅支持同一现场，不能排除256次回绕或掉电后的数值碰撞；要排除掉电需同步被动记录MCU供电。
部署诊断包后第一次读取必须作为基线，读取两段还须确认同一启动；原始0x72不是完整复位原因枚举。
TLSR8251-E13 v1.0.1 原PDF物理页62说明0x72 bit0为WDT复位标志且W1C；本诊断不清它，可能是此前复位遗留，
连续读取为1不能证明每次启动都由WDT导致。detail的最后SPI操作也不证明此事务仍在阻塞，精确停点仍需PC。

## 验证状态

本轮完成源码调用链、安全路径和Git差异审查，补充真实control逐读取失败、BMS部分状态/WDT待办优先级、
诊断PM透传/冻结/回绕/关闭无副作用及production拒绝的回归源码。
schema 2另补充真实休眠状态发布到诊断原因、DeepSleep调用返回撤销、模拟跨启动捕获、B85复位清REG7模型、
旧schema兼容及基线判读向量。host的longjmp只模拟驱动不返回；手工清REG7只表达头文件的模型，均不是硅片证明。
**未执行**这些host测试、sources --check、静态分析、目标编译/link/resources；未生成BIN，未烧录/OTA/实板验证。
按仓库AGENTS，实施请求不自动授权执行这些验证。后续至少覆盖公共改动的四产品默认检查、D014诊断开启的
link/resources、相关host，再做无有线/无持续BLE轮询复现；host桩不能证明真实Suspend长期可靠。
