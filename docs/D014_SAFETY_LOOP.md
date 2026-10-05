# D014 保护与恢复的软件闭环

## 已复现的问题与修复

基线 `464b6ee53c94d9bed2d3d994fc93c5302c937fac` 上，软件 Third OCD 触发并关闭 DSG 后，只要测得电流降到 Recover 以下，即使负载仍连接，也会按 Filter 自动恢复。新的联合场景先得到 `continuous_load_zero_current expected=DSG_OFF actual=DSG_ON`，再修改生产逻辑。

SH 后端现在向公共软件保护提供明确的恢复输入：是否要求物理证据、充/放电侧解除条件、本次是否有完整新 ADC 样本。软件保护继续独占 Third 状态与滤波计数；SH 后端独占寄存器检测模式、物理证据及硬件故障。没有新增第二份软件过流锁存。

- 软件 OCD 也会选择负载检测。稳定的 `LOADOFF && !LOADON` 或既有 `CHGING` 反向状态，才允许软件放电过流按电流回差恢复。
- 软件 OCC 使用本次有效 C+ 拔除证据或既有 `DSGING` 反向状态。C+ 与负载检测的互斥、切换等待及 900/2100 mV 判据沿用 SH 硬件恢复策略；这些板级判据仍需实测签核。
- 新鲜、完整的电流/电压样本才能推进恢复计数。正常 CADC 等待只暂停；解除条件消失会清零。通信错误与重新建立采样窗口也清零部分恢复计数。
- AFE 初始化、feature 初始化、通信恢复、Sleep/Wake 保留已经触发的软件 Third OC。其他故障仍按各自所有者阻断，软件 OC 消失不等于允许最终 MOS 导通。
- First/Second 告警行为不变；关闭某软件保护项仍按现有参数语义清除该项。D008 尚未提供这组物理输入，保持原行为，不能据此声称 DVC 软件过流恢复已闭环。

`bms_sw_protection_init()` 在启用物理恢复输入后保留两个 OC active 位，但重新建立其恢复窗口；`bms_sw_protection_clear()` 仍是显式清除全部软件状态的入口。所有状态留在主循环，无 ISR、Flash schema、协议或产品默认参数变更。

## 可重复执行的测试

```powershell
$env:CC = 'C:/qp/qtools/MinGW32/bin/cc.exe'
$env:PYTHONDONTWRITEBYTECODE = '1'
python tests/run_host_regression.py --product d014 --only d014_safety_loop_host_check
```

`tests/d014_safety_loop_host_check.py` 直接编译产品 sources.txt 中的 22 个原始生产 TU，不抽取或重写函数体：参数/Config、State、Event、journal、CRC、SOC 默认与校验、软件保护、feature、guard、板级能力、SH control/backend/feature/NTC/SPI 驱动等。公共回归入口已登记；CI 的普通回归与插桩场景均覆盖本测试。

夹具仅实现 Flash 端口、逐字节 SPI 对端、GPIO 和时钟；SDK host 头文件替换平台声明，产品配置仍为真实 D014。Modbus 完整帧回调未进入本场景，只有一处调用即终止的链接占位，不返回伪成功。SOC TU 的链接只为执行真实默认/校验，不能由此推断覆盖 SOC 整个算法。

| 场景 | 观察与约束 |
|---|---|
| 空白 Flash、启动保存失败 | 真实启动门禁阻断请求；未签核默认不会被测试声称为合适的电池参数 |
| 参数提交、非法回差、部分写入失败 | 校验拒绝与持久提交失败均不发布候选；保留旧参数 |
| 持续负载/充电器与零电流 | 软件 OCD/OCC 保持阻断；迟滞区不能作为 C+ 移除 |
| 新采样与重新连接 | 未完成 CADC 不推进恢复；重新连接丢弃原计数 |
| AFE 重初始化与 Sleep/Wake | 重新满足启动等待和采样资格，原 OC 状态仍在 |
| 温度断线、硬件短路并发 | 软件 OC 恢复后，其他保护仍独立关断 |
| 保护寄存器读回错误 | 进入 guard inhibit，重初始化不能清软件过流 |
| 总线失联、watchdog 静默 | 静默期间无 SPI；到期重初始化后仍保留 OC |
| ADC 过期、tick 回绕 | 过期后输出阻断，回绕后重新建立资格 |
| 新进程重启 | 真实 journal 重新加载之前提交的参数；不沿用测试进程中的缓存 |

模拟输入使用 8 路 3300 mV、10 kΩ NTC、受控电流，测试用过流阈值经真实参数提交入口写入模拟 Flash。它们不是修改后的产品出厂值。

## 证据不能跨越的边界

这是 D014 的保护采样任务闭环，尚未纳入 `app_sample_task()` 的真实主循环调度、ISR、BLE/Modbus 入站和真实 MCU ABI。SPI 模型不实现完整硅片：ADC 标志由场景注入，open-wire 完成被脚本化为完整线束，MOS 检查针对实际写出的 SCONF2 命令。模型不证明 Gate/Vgs、负载断开、AFE 比较器、转换时延、通信失效时硬件自主关断或真实掉电行为。

**MCU 复位会清除 RAM 中的软件 OC 状态**，与 AFE 通信重初始化不同。本测试明确验证该边界；未增添跨 MCU 复位的故障持久化，也未决定掉电重启后的锁存/人工恢复策略。该策略与实际 C+/LOAD 判据列为产品/台架待签核项，不能把 host 通过当作解除发布阻断。

历史自动化基线见 [自动化验证](AUTOMATED_VALIDATION.md)。实板未完成项继续见 [硬件验证](HARDWARE_VALIDATION.md)。

## MCU 复位边界探针（2026-10-06）

固件输入仍为 `3f2fe78272f48a9570d4b31252386287a6c01c92`，本次只增加测试和说明。`tests/d014_reset_boundary_probe.py` 检验候选策略“已触发的软件过流，在物理解除前跨普通 MCU 初始化保持相应输出关闭”。**该策略尚未产品签核；探针失败明确表示当前代码不能提供此保证，不表示已经修复。**

探针直接链接上述 22 个生产 TU，分别在两个 OS 子进程内触发故障和重新启动，只通过文件传递 RAM Flash 模型的字节。充/放电两侧各测故障历史尚未 checkpoint、已经 checkpoint 两种情况。启动资格等待期间及之后继续保留负载或充电器输入，电流置零；检查的是 SPI 模型收到的 SCONF2 开通命令，不是实际 Gate。

| 已触发故障 | 历史 checkpoint | 新进程读回历史 | 首次重新开通对应 FET 的采样序号 |
|---|---|---|---|
| 放电过流 | 未完成 | 无 | 8 |
| 放电过流 | 已完成 | 有 | 8 |
| 充电过流 | 未完成 | 无 | 8 |
| 充电过流 | 已完成 | 有 | 8 |

以上为 Windows host 和 WSL ASan/UBSan 一致的固定观察，采样序号不能换算成实板恢复时间。原有 AFE 重初始化测试继续保留同进程 RAM，与这里的新进程重启不同。该探针没有模拟 watchdog 电气复位、掉电波形、完整主循环调度或 AFE Reset 的全部硅片行为。

源码依据：

- `bms/platform/telink/main.c` 的普通路径调用 `user_init_normal()`；`bms/app/app.c` 加载参数、初始化 AFE，随后调用 `mos_update()` 并允许输出请求。
- `bms/core/bms_sw_protection.c` 的软件 Third OC 与恢复证据状态是静态 RAM；`bms_sw_protection_init()` 的保留动作只能保留仍存在于当前进程中的状态。
- `bms/core/param.c` 在 `bms_parameters_startup()` 加载 event journal；`bms/core/bms_event_log.c` 恢复的是历史记录，没有把历史事件还原成当前保护锁存。旧事件也不能直接当作尚未解除的故障。
- `bms/afe/sh3673510/sh3673520.c` 的 `SH3673520_Init()` 发送 `SH3673520_Reset()`。因此不能直接假设仅 MCU 复位就会让 AFE 故障位一直保留；具体位和电气行为仍需官方资料及实板验证。

### 运行与结果解释

```powershell
$env:CC = 'C:/qp/qtools/MinGW32/bin/cc.exe'
$env:PATH = 'C:/qp/qtools/MinGW32/bin;' + $env:PATH
$env:BMS_PRODUCT = 'd014'
$env:PYTHONDONTWRITEBYTECODE = '1'
python tests/d014_reset_boundary_probe.py --output "$env:USERPROFILE/Documents/CodexOutputs/bms-monorepo/reset-probe.json"
```

输出必须是源码树外尚不存在的文件。四场景全部满足候选门禁才退出 0；当前四场景准确命中 `RESET_POLICY_GAP`，退出 1。编译失败、崩溃、历史读回不匹配和插桩报错直接失败，不能作为已确认策略缺口收录。JSON 保存源文件指纹、seed 输出、重启观察和断言；运行期间源码变化使结果失效。

该显式 `_probe.py` 尚未纳入默认绿色回归：它是未关闭的产品策略门禁，不能通过“期望退出 1”把缺口变成通过。策略确定并实现后，应把相应场景纳入常规回归；即使该探针变绿，也只证明连续 12 次模型采样的约束，仍需补充解除、重连、再次复位等完整恢复场景。

本轮证据根目录：`C:/Users/Administrator/Documents/CodexOutputs/bms-monorepo/reset-policy-20261006/`。Windows 与 WSL 的输入指纹均为 `471856d7e1a9ec04307375a0d390836b2f11df1fb33f32d7998c83b2d4aef713`，四项结构化观察逐字段相同；二者均因策略断言退出 1，无插桩错误。

| 证据文件 | SHA256 |
|---|---|
| `windows.json` | `8b41d4a8a2c451d71b6bf341e5c29cc71600508b2713476056d25d2d36e578f9` |
| `sanitizers.json` | `5c86a17bf59a0f0716ce0ef22029b5fda4a153bd93cc99771e1529d0dc3abeb5` |
| `regression/report.json` | `25e8309537ea0457e7db981646bb7f38887503ff586032641ebde26182f4d2a9` |

相关普通回归 `d014_safety_loop_host_check`、`validation_mutation_check` 共 2/2 通过，验证工具单元检查 10/10 通过。本轮未重跑整个产品矩阵或 TC32 链接：`bms/`、SDK 和构建实现没有修改；下方六配置资源结论仍属于之前的固定实现提交，不能算本轮重新构建。

### 待确认的产品行为

| 选项 | 用户可见行为 | 实现必须解决的问题 |
|---|---|---|
| 每次冷启动先确认解除 | 即使上次没有过流，带负载或充电器启动也可能等待拔除；需定义两侧各自启用条件 | 不依赖新增 Flash 锁存，但会改变现有上电流程；须验证 C+/负载检测的启动可用性和互斥 |
| 正常带载启动，故障锁存跨复位和掉电 | 没有未解除故障时照常启动；有故障时保持对应侧关闭 | 持久状态所有者、置位/清除写入顺序、写失败、任意掉电窗口、擦写寿命、schema 及旧设备兼容；不能直接复用延迟 checkpoint 的历史作为锁存 |
| 仅热复位保留，整机断电允许重启 | watchdog/复位键不解除，真正断电重上电可以重新启动 | 必须可靠区分复位域并验证保留介质；SDK 某些 analog retention 位的注释不足以证明该产品方案成立 |

选项尚未确定，当前没有修改生产初始化、持久化格式或输出行为。第二项更接近“保持正常带载启动，同时避免复位解除故障”，但持久化方案必须证明断电窗口，不能仅增加一次保存调用就宣称完成。

### 接板后的验收条件

当前用户确认没有连接实板，本轮没有打开串口、烧录或进行电气操作。准备台架时记录板号/BOM、固件 SHA、配置读回、电源/负载/充电器限制及测量通道；按最终签核策略填写期望结果。

| 台架场景 | 必须记录的证据 |
|---|---|
| 正常带载/接充电器启动、空载启动 | 正常启动行为与选定策略一致；两侧 Gate/Vgs、实际电流、C+/LOAD 状态及启动资格时序 |
| 软件 OCC/OCD 后保持外部连接，MCU 热复位且 AFE 供电保留 | 复位来源、SCONF2 命令与 Gate 波形；不能出现非预期短暂导通 |
| 整板断电重启，覆盖记录写入前/中/后 | Flash 读回、启动门禁、故障/历史状态；未提交或损坏状态的处理符合选定策略 |
| 拔除、重新接入、两侧交叉与恢复期间再次复位 | 只有有效解除证据及完整恢复窗口才能解除相应锁存；其他保护继续独立阻断 |
| 复位后总线失联、ADC 无新样本、TS4 断线、AFE 硬件故障并发 | 输出继续 fail-safe；重新连接不绕过采样资格和独立保护 |

## 固定提交验证记录

实现提交 `3f2fe78272f48a9570d4b31252386287a6c01c92`；验证时 Windows/WSL 工作树均干净。后续本页和证据文件的提交仅补充记录，不冒称重新编译。完整机器数据、报告哈希、ELF/MAP 哈希及工具身份见 [D014_SAFETY_LOOP_EVIDENCE.json](D014_SAFETY_LOOP_EVIDENCE.json)。

| 验证 | 实际结果 |
|---|---|
| Windows 全部 host | 166/166：D008 39、D011 38、D013 36、D014 51、共享工具 2；0 失败 |
| WSL Ubuntu ASan/UBSan | 29/29；与 Windows 输入指纹和对应结构化观察一致 |
| CMake Release/CTest | 3/3；断言启用 |
| 四产品 sources | 清单与源码顺序检查通过 |
| Windows TC32 生产 | D008 三装配 + D011/D013/D014，六配置 link/resource 全通过，编译 0 错误/0 警告 |
| D014 Cppcheck | 36 个实际应用 TU，103 项 style；无 error/warning/performance，两个头文件覆盖缺口，未执行 MISRA |
| 镜像与实板 | 未生成 BIN，未烧板，未操作实物 fuse；没有远端 CI 运行结论 |

相对 `146c3ff686df5f667deb67b22e894a17fd06b132` 的结构化报告，仅新增 D014 联合场景一项，现有产品默认、软件保护通用向量和 AFE 寄存器轨迹未变化。这不是整个固件行为等价证明：本轮软件 OC 恢复行为的改变由新增场景覆盖。

| 生产配置 | Flash 余量 / B | 相对原固定基线 Flash 增量 / B |
|---|---:|---:|
| D008 16S LFP / 20S NMC / 24S LFP | 各 8284 | 各 176 |
| D011 | 12060 | 288 |
| D013 | 13084 | 272 |
| D014 | 12236 | 288 |

D008 距 8 KiB 硬门槛仅剩 92 B，应继续以真实生产链接把关公共代码增长；本轮没有降低门槛。RAM 地址跨度各增加 4 B，不代表实际运行栈水位。

本机正式证据根目录为 `C:/Users/Administrator/Documents/CodexOutputs/bms-monorepo/d014-safety-loop-20261005/`：`final/`、`sanitizers-final/` 保存完整人/机报告，`targets-production/` 保存六配置 ELF/MAP 和资源收据；`red.txt` 保存基线零电流误恢复的失败输出。
