# BMS 深度减法重构交付报告

> 固定提交记录：本文是当时源码与验证的快照，不代表当前 HEAD；当前规则见 [CURRENT_RULES](CURRENT_RULES.md)。文中提交、行数与测试数字只用于追溯。

完成日期：2026-10-17。验收代码 HEAD：`9845fca310e3aaca0800fbe38cf6a736efa23dd5`。
后续交付提交只归档本报告、审计附录、验证摘要和 README 导航，不改变验收代码。
17 个实现/测试提交独立保留，没有 squash；本轮未推送、合并或创建 PR。

已完成四产品源码范围审查和相关减法重构；最终完整回归 116/116、六配置生产
ELF/MAP/资源检查、四产品无 SDK Core CTest 和 30 组相关 ASan/UBSan 均通过。
Cppcheck 已执行，保留提示及覆盖缺口见后文。以上是软件交付，实板验收尚未完成。

完整导航：[文件、函数、变量与函数式宏审计](DEEP_SIMPLIFICATION_20261016_AUDIT.json)；
[验证收据摘要、资源/hash、静态提示与逐提交文件](DEEP_SIMPLIFICATION_20261016_EVIDENCE.json)。

## 起点和隔离

用户指定起点 `dd7115033e9e6d73808fdf303d78f37196ca7b40`。原工作区分支
`codex-remove-openwire` 的本地 HEAD 为 `3cb223237917ac6951302446b6b7e5e592656003`，
远端为指定起点，原工作区存在未提交修改。本轮未复制、丢弃或提交它们。
独立工作区分支为 `codex-bms-deep-simplification-20261016`；已有 `codex` 引用使
`codex/bms-deep-simplification` 无法创建。明确 SHA 优先于附件中“最新 HEAD”的一般描述。
交付前只读复核时，源工作区 HEAD 已由其他工作推进至
`cf0f7664c13c1b95996c6887dd23fbcd7b463adc`，仍有未提交修改；本任务未写入或恢复该工作区，
也没有把其新增提交混入本分支。

## 最小架构及审查原则

五类职责足够：Core 保存保护、SOC、功能策略与安全资格；AFE 保存寄存器、转换完成、
芯片锁存和物理恢复证据；Platform 保存 GPIO/总线/Flash/时钟/PM；Product 选择能力、
参数及源码；Application 用固定顺序调度。编译期选择一个 AFE，不增加运行时注册、
动态分配、任务框架或统一大状态机。

先判断接口是否承载独立事务，再判断是否有消费者。输出意图、已成功命令、AFE 回读、
转换事件和物理证据不能合并。SC/OCD/OCC 的零电流不能作为移除证据；有效缓存可供
业务计算，但不能推进三次新样本资格。协议、61001、CFG2/schema 3、OTA/APP 分区保持。

AFE 原页依据：DVC1124-2 RM V1.2 PDF p6–8 的读清转换标志、R6 输出状态和 CST；
DS V1.1 p13–15 的转换周期、p22 的唤醒；SH36735XX CV1.0A PDF p16–20 的
C+/LOAD、转换、watchdog 和自主 MOS，p32–36 的模式、flag 与保护开关。
使用现有型号配置；未核验硅修订和缺失的三款 SH 原始图纸/BOM，不扩大原资料保证范围。

## 基线和第 0 轮：恢复验证入口

原始完整回归：116 组，80 通过、36 失败。原始四产品生产链接：SH 三产品通过，
D008 在 profile 验证时被错误的摘要比较拦截。当前源码装配摘要本身有效。

修复：工程 profile 覆盖先验证源码装配的固定参数包，再返回工程装配；镜像交付仍要求
与源码选择一致。没有改参数、更新组选项或固定标识。修复诊断关闭时两个 SOC 局部量
的编译警告；夹具显式选择诊断宏，预处理与编译保持一致；补实际 profile-store TU；
不再调用 D013 不编译的 heater/balance 后端；更新已取消固定装配白名单的旧断言。
没有以关闭 warning 或放宽生产保护门禁使测试通过。

验证收据根：`%LOCALAPPDATA%/CodexTemp/bms-deep-simplification/20261016/`。
`baseline-host` 为原始完整失败记录；`round0-host`、`round0-repaired`、
`round0-last`、`round0-final` 保留逐次修复结果。所有原失败项均在后续针对性重跑通过；
工具单测 55 项通过。最终另行执行的完整回归为 116/116，见最终验证章节。
六配置生产 ELF/MAP 已完成；D008 数据含第 0 轮验证入口修正且 dirty=1，
SH 三产品数据来自 clean 原始起点，资源比较须标明这一证据差别。

## 逐轮记录

| 轮次 | 提交 | 修改范围 | 删除函数 | 删除状态 | 验证 |
|---|---|---|---:|---:|---|
| 0 | `06dd68b89b966041c8f992b2a5d07eb312d0d8cd` | 验证入口、诊断夹具及工程 profile | 0 | 0 | 原失败项针对性复测、55 项工具单测；六配置 ELF/MAP |
| 1 | `caba4286f841ffef8dc1d66da233a1fade02303d` | DVC 功能并入后端、删除转发和无入口休眠取消 | 5 | 0 | 12 组相关检查及修复后两项 contract；四产品源码/生产链接和资源 |
| 1 补充 | `d9468667d2418ed464aa7b07688f850c2fb6e84a` | D008 恢复夹具改用合并后的公开 FET 事务 | 0 | 0 | `d008_recovery_host_check` 的 HW=0/1 及电压恢复通过 |
| 2 | `e49142af078a0754f87b7f6a9df2b7d1b6399769` | SH NTC 有效性与加热命令状态去重 | 0 | 2 项 | 15 组 feature/SH/温度检查；四产品源码、生产 ELF/MAP 和资源 |
| 3 | `d70599e08f43ee6b2e00f78ccec0ea06d904db26` | DVC 初始化与采样分离，删除复位重试和重复快照 | 4 | 2 | D008 30 组；16S LFP/20S NMC/24S LFP 生产 ELF/MAP 和资源 |
| 4 | `52458ef6429a7fdb6b387ea248c91d82b702f428` | SH 失败阶段直接返回、均衡事务并回后端 | 2 | 2 | 22 组及修复后的三产品恢复重跑；四产品源码、生产 ELF/MAP 和资源 |
| 5 | `6f3ef232f4b1e2b8e3a26b82a04ae42032521d9d` | Guard 唯一授权，SH 直接执行已裁定请求 | 2 | 3 | 26 组及修复后 contract；四产品源码、生产 ELF/MAP 和资源 |
| 6 | `97968cad6efbbaadc9c05573b9732a749c9d6356` | 低压条件统一位表示，关闭诊断时不执行诊断查询 | 0 | 1 | 六组相关检查（D008 夹具宏修正后复测）；四产品源码、生产 ELF/MAP 和资源 |
| 6 诊断 | `c0196f2e1fea43e0f896516bc1510120e0668993` | 诊断采集合并至 app，删除独立 runtime 文件与公开接口 | 0 | 0 | 13 组相关检查（四项诊断夹具补头文件后复测）；四产品生产 ELF/MAP 和资源 |
| 7 SOC | `24cb86f63108bfd31bd0de728c22507bfc698394` | 当前输入沿同步链传递，删除整帧静态副本和积分薄包装 | 1 | 1（32 B） | 13 组及 D008 SOC 独立回放；10080 点七天轨迹逐字节一致；四产品生产 ELF/MAP |
| 7 均衡 | `d3dbec2920fbba1b9eb5c86988c4c118544bc4ba` | DVC 直接均衡事务，删除两份请求和暂停状态 | 4 | 3 | 八组相关检查；D008 16S LFP 生产 ELF/MAP 和资源 |
| 8 Core | `2c96d60b08580c9bb93084d69563c3aaf623e8e4` | 标准 C 类型边界、公共 32K 时钟和无 SDK 授权事务 | 0 | 0 | 21 组相关检查；无 SDK CMake/CTest 四项；四产品生产 ELF/MAP |
| 8 BLE | `c15522c344fb9e76cba45bf75efc60b221665025` | 名称应用事务移至现有 BLE 平台文件 | 0 | 0 | 九组相关检查；四产品生产 ELF/MAP |
| 8 API | `41dfac6d9591aebe7cef1762112be7389dcebac0` | 删除 SH 无生产消费者的 ready 查询和统计清零 API | 2 | 0 | 八组相关检查；四产品生产 ELF/MAP |
| 9 审查修复 | `84c0729b317f4c27b26362da6943bb2596061935` | 恢复 SH 首次读失败后的均衡 OFF 尝试及确认 | 0 | 0 | 新注入先复现 D011/D014 失败；修复后十组及四产品生产 ELF/MAP 通过 |
| 8 PM 边界补充 | `2daaf794342671a8ac6cd8340901aeef78a5ec52` | 公共休眠策略并入现有 Core，应用保留硬件收尾 | 净增加 1 | 0 | 十组（四项 enum 检查显式转换后复测）；无 SDK CTest 五项；四产品生产 ELF/MAP |
| 8 Event API | `9845fca310e3aaca0800fbe38cf6a736efa23dd5` | 删除仅存储夹具使用的重复次数读取接口 | 1 | 0 | 八组存储检查；四产品生产 ELF/MAP |

第 1 轮删除 `dvc1124_feature_backend.c`，温度角色和有效性共用后端现有实现，
删除 `dvc_ntc_valid`、`dvc_temp`；共口 MOS 事务直接作为 `dvc1124_backend_set_fets`，
删除 `dvc_apply_common_port_fet_state` 转发层。删除 `storage_record::read_bytes` 的纯转发，
存储读错误仍由原调用方处理。已提交保护性休眠没有取消入口，删除无生产消费者的
`bms_event_log_cancel_sleep`，重复休眠仍只记录一次；持久重复次数和 payload 不变。

第 2 轮删除 `s_ntc_valid[4]`：原转换范围为 500..300000 Ω，0 从不代表有效值，
因此有效性直接由经过范围校验的 `s_ntc_ohm[]` 得到；-40°C 的温度编码 0 不参与判断。
删除 `s_feature.heater_on`，唯一成功设置入口仍是 `set_heater()`，读取其已经发布的
`g_bms_system_status.bits.heater_status`。删除两项存储（四元素数组加一个字节字段），
不是移动到新结构。实际 RAM 差值以最终 MAP 为准。

第 3 轮：固定运行配置与硬件保护在一次初始化内完成，全部成功才清 `s_need_config`。
失败后的正常采样只返回无效，交 guard 的两次失败及 5 秒 watchdog 静默处理；
采样不再配置或发 shutdown-wake/reset。新增固定配置失败注入，以及 100 次采样不重配断言。
合并 `DVC1124_BmsApp_AFEGet` 与 `dvc1124_backend_sample`，每轮保护只取一次快照；
方向直接读取唯一 CC2 缓存，删除 `s_direction_allow_mask` 和 `s_project_config_pending`。
保护配置和输出授权直接由驱动实现后端契约，删除两层纯转发；初始化内联原来的
`dvc_project_reset_with_shutdown_wake` 单次包装。初始化仍保留有界唤醒等待、配置验证及
两次 270 ms 启动电流检查，未把这些硬件阶段伪装成完全非阻塞；实板 BLE 时序仍待验收。

第 4 轮删除 `sample_failed` 与全局 `s_sample_failure_stage`，采样/保护函数直接返回
失败阶段，调用者一次处理失败。失败阶段、原子发布和清 flag 失败锁存仍由故障注入验证。
删除 `sh3673510_feature_backend.c`，均衡留在同一 SH 后端；有效通道掩码是编译常量，
不再通过 `sh_valid_cell_mask()` 取值。删除 `s_balance_requested`：实际寄存器已经等于请求
且未到续期时无需再保存并比较一份请求；回读不符、写/读失败和 20 秒续期仍独立处理。
没有删除 ADC seen/age、1.2 秒启动温度资格、C+/LOAD 切换等待、短路清除后确认或
`s_flag_clear_failed`；它们分别证明新转换、模式稳定、物理移除及清除事务结果。

第 5 轮删除 SH 的 `s_output_enabled`、`s_requested_charge_on`、
`s_requested_discharge_on`，不再保存第二份授权/请求。公开 `sh3673510_bms_afe_set_fets`
直接执行原仲裁事务，删除私有调用转发和 `sh3673510_bms_afe_set_output_enabled`。
Guard 在提交前统一检查参数、输出许可和三帧资格；SH 正常执行不再反查 Guard 资格。
诊断只读取公开 Guard 快照，不能修改授权；实际 FET 状态仍通过唯一发布接口交给 Guard。
软件保护、加热阻断、芯片锁存和共口方向规则仍在各自所有者，成功命令缓存及物理回读不合并。
联合生产场景覆盖远程许可、配置回滚、启动/失联三帧资格、保护并发、ADC 过期及 tick 回绕。

第 6 轮把低压档位与上档资格合为两个独立条件位，删除一份长期状态；下档与上档
计时仍独立，无效样本保留最后有效条件，可靠充电仅影响上档。诊断仍派生原 2/3 编码。
诊断关闭时不再执行只为诊断服务的总线/Flash 查询。保护性深睡的提交门禁、命令应答
有界等待、AFE 异常计时、过放持续计时、Suspend 与唤醒路径均保留。

## 架构前后与关键调用链

原工程已经有 Core/AFE/Platform/Product/Application 分区，问题集中在边界内部：
DVC boot 包装又进入 BMS 采样并再次取快照；功能后端散在独立文件；SH 保存第二份
授权和请求；SOC 将同步输入复制为静态整帧；保护性 PM 策略与硬件收尾同处应用文件。
本次沿五类真实职责整理这些路径，没有新建调度框架或运行时驱动注册。

| 路径 | 基线 | 交付 | 实际消除 |
|---|---|---|---|
| DVC 采样 | `main_loop → app_sample_task → bms_afe_sample → dvc1124_backend_sample → DVC1124_BmsApp_AFEGet → DVC1124_App_AFEGet` | `main_loop → app_sample_task → bms_afe_sample → dvc1124_backend_sample → DVC1124_App_AFEGet` | 6→5 个入口节点，5→4 次调用；节点间文件切换 4→3，删除 boot 中的采样/重配置中转 |
| DVC MOS | Guard → 后端包装 → 私有共口仲裁 → 驱动 | Guard → 后端共口仲裁 → 驱动 | 一层无独立事务的转发；芯片实际写/反馈仍保留 |
| SH MOS | Guard → 后端保存请求/授权 → 私有仲裁 | Guard 裁定 → 后端仲裁/硬件事务 | 私有转发和三个镜像；不消除芯片锁存/自主 MOS 规则 |
| DVC 均衡 | feature → Guard → feature backend → Set/Service/Get | feature → Guard → 同驱动 set/get 事务 | 独立文件和三份请求/暂停状态；失败仍回读确认 |
| 运行诊断 | app → 独立平台文件的单函数 | app → 私有同帧采集 | 删除文件/公开接口；关闭时不执行仅诊断查询 |
| SOC | app 样本 → 静态整帧 → 各策略 → 积分转发 | app 样本 → `const sample` 同步传递 → 直接积分 | 一份 32 B 长期副本和一个薄函数 |
| 保护性 PM | app 内计时/选择/收尾 | app 取得输入 → Core 纯策略 → app 收尾/平台睡眠 | 业务策略可用实际生产 TU 独立测试；没有重复 committed |

这里的深度统计限定到表内明确端点，不把 CRC、寄存器、SDK 中断/回调展开成虚假的
全图最大深度。函数式宏按编译分支解释；回调表/启动入口不是无消费者。
`main_loop()` 顺序仍为低功耗前置、BLE、到期采样、事件、产品通信、State checkpoint、
低功耗评估。采样中 Guard 负责新帧资格和 feature 服务，SOC 只接收本次合格上下文；
诊断不新增一次 AFE 采样。

## 全部函数、文件与变量审查口径

以四产品 `sources.txt` 的 C 文件并集、include 闭包和 BMS 头文件建立清单。
附录逐条记录原/现路径、位置、职责、保留/删除/合并判定、调用者、宏调用、回调注册、
下游、硬件访问及状态写入候选。共 1531 条函数审查记录、2708 条对象/字段记录、
1061 条基线及交付函数式宏记录；重复条件分支和两个 HOST_TEST 专用函数也明确索引，
不能把这些数当作一个 production ELF 的可达实体数。

| 同口径指标 | 基线 | 交付 | 差值 |
|---|---:|---:|---:|
| BMS C/H 文件 | 119（48/71） | 117（45/72） | -2（C -3，H +1） |
| BMS 函数定义 | 805 | 785 | -20 |
| BMS 函数式宏 | 156 | 155 | -1 |
| BMS 可变全局/static 对象 | 274 | 263 | -11 |
| BMS 结构字段定义 | 591 | 596 | +5 |
| BMS C/H 行数 | 26470 | 26256 | -214 |
| SDK C/H 文件/函数定义 | 271/723 | 271/723 | 0/0 |

三个合并文件为 `dvc1124_feature_backend.c`、`sh3673510_feature_backend.c`、
`bms_runtime_diag.c`；新增 `bms_types.h` 承载真实的主机/TC32 类型边界。字段数包括
协议、配置、测量和局部策略输入，不等于运行状态数。对象 -11 与长期事实 -14 不同：
两个被删除事实原为结构字段；PM committed 字段移为唯一应用 static，不能算作减少。

真正删除的 14 项长期事实如下。数组计一项，不按元素夸大：

| 范围 | 删除项 | 唯一保留事实/推导方式 |
|---|---|---|
| SH 温度/加热 | `s_ntc_valid[4]`、`s_feature.heater_on` | 已校验电阻非零；成功 heater 发布状态 |
| DVC 配置/方向 | `s_project_config_pending`、`s_direction_allow_mask` | 初始化配置结果；唯一 CC2 反馈缓存 |
| SH 采样/均衡 | `s_sample_failure_stage`、`s_balance_requested` | 失败阶段直接返回；实际掩码和成功续期时间 |
| SH 输出 | `s_output_enabled`、`s_requested_charge_on`、`s_requested_discharge_on` | Guard 唯一授权/请求；后端保留实际成功命令 |
| PM | `normal_voltage_active` | 两个独立电压条件位；原 region 表示调整，不另算删除 |
| SOC | `g_soc_input` | 同步 const 输入；保留上次接受的电流与时间 |
| DVC 均衡 | `s_balance_request`、`s_balance_requested_mask`、`s_balance_suspended` | 直接事务及真实回读；需要时写入、成功后续期 |

没有删除具有不同到期语义的安全计时器。无普通 caller 的剩余条目已补查启动入口、
全局回调表、宏和诊断分支；`bms_afe_test_enter_shutdown/wake` 仅 HOST_TEST，
`bms_storage_platform_get_diagnostics` 仅开发诊断。函数有调用者也不能自动保留：
本轮已据单次转发、重复快照、无事务 getter 和状态所有者逐轮合并上述接口。

清单是工具辅助的必要性与源码导航审查。AST 同时提取赋值和 `++/--`；聚合赋值、
指针别名、同名字段、条件编译和间接调用仍需按所属类型/源码解释，不是完整数据流或
形式化正确性证明。23 个解析诊断中三个为 BMS aligned 属性声明，余项为 SDK 语法；
BMS 函数正则交叉检查未发现漏项。SDK 源码保持原厂体系，二进制库/汇编内部不作
C 函数计数或硅行为审查；不能由此声称已审计闭源 BLE 实现。

## 时序、状态和异常保留依据

| 领域 | 交付规则与保留理由 |
|---|---|
| 应用采样/时钟 | 200 ms 调度是项目业务节拍；32K 为跨 Suspend 业务时间，system timer 留给 SDK/微秒外设时序。公共授权事务改走 `bms_time_now_32k()`；5 秒分片/60 秒会话仍严格 `>` 到期，量化单位 31.25 μs。不同用途的两个硬件时间源不机械合并 |
| DVC ADC | CC2/VADC 的 RC 完成事件、pending、seen、独立时间戳保留；200 ms 轮询不能保证新转换。项目最大年龄 512 ms、公共轮询连续性 400 ms 分别限制转换缓存和采样中断，不能合成一个 valid |
| SH ADC | CADC/VADC 完成、最大年龄 400 ms、1.2 秒温度启动资格及采样重启保留。可用缓存供普通计算，旧帧不能推进三帧恢复/均衡资格 |
| Guard | 两次失效进入总线静默；DVC 4 秒 watchdog 配置等待 5 秒，SH 约 32 秒配置等待 35 秒再探测。静默期间 feature、诊断、配置不得访问 AFE；恢复后仍须三次有效新帧和配置一致，不能只清故障位 |
| MOS/软件保护 | 请求、成功命令、硬件反馈、软件滤波、芯片锁存、物理移除证据独立。SC/OCD/OCC 不以关 MOS 后零电流解除；远程许可和加热阻断不能绕过 Guard |
| SH 恢复 | LOAD/C+ 互斥及切换稳定、SC clear 下一帧确认、flag clear 失败锁存保留。一般失败直接返回阶段，物理证据/锁存失败不能简化成成功 |
| DVC 初始化 | 固定运行配置仅初始化，全部成功才清 need_config；正常采样失败不再 shutdown-wake/reset/重配。保留有界启动等待、写后验证及两次 270 ms 零点检查；未证明启动完全非阻塞或 BLE 时序合格 |
| 均衡/加热 | DVC 45 秒软件续期和 SH 20 秒续期、真实 mask、写/读失败保留；OFF 首读失败仍尽力写零并确认。历史电压可信度、温度回差、加热故障确认/熔断 once 对应不同事实；D013 两功能均不编译 |
| SOC/SOH | 重复时间戳不更新前一次电流；合格时间积分及余数、OCV 静置、满/空端点、Estimate/Display、压降/低 SOC 过滤保留。没有实车 ground truth，不能据合成轨迹宣称复杂策略提升体验或删去它们 |
| UART/RS485/SIF/BLE | RX/DMA 缓冲所有权、TX DMA 完成与 UART 发完、最小线空闲/50 ms 中止、SIF 活动包/待发包均不合并。BLE SDK callback/OTA 生命周期保留；ISR 不新增 Flash/协议解析，普通 Suspend/唤醒握手不改 |
| Flash/OTA | pending 与 committed、序号/CRC/提交标记、失败 backoff、检查点和一次性更新标记分别保留。Config 启动安全门禁不等同 State/Event 降级；无界失败总线重试和 Flash 擦写循环不新增 |
| 保护性 PM | 四个饱和计时器对应低压下档/上档、AFE 异常、固定过放；无效样本保持已知电压条件，上档充电门槛 `>= -200 mA`、过放双恢复条件及同到期后者优先不改。保护性收尾只有一次 committed；正常通信/日志不得无限阻止深睡 |

## 可移植边界与兼容性

Core 的保护、状态、CRC、journal、ETA、授权事务、公共头和保护性休眠策略以标准 C
输入/输出契约运行；根 CMake 实际编译九个 Core TU，四产品默认配置均可独立运行。
AFE 仍编译期选择，后续 BQ 后端须自行实现转换事件、watchdog、锁存恢复、MOS 反馈及
均衡契约；没有预先编造 BQ/STM32/EFR32 寄存器或驱动。新 MCU 替换时间、总线、Flash、
GPIO、UART、中断、睡眠与 BLE 生命周期，业务时序和协议单位无需随 MCU 重新定义。

仍有明确适配点：`bms_state_store.c` 直接用 `drivers.h/pm_get_32k_tick`；完整正常
Suspend/唤醒编排位于 `app_power.c` 并依赖 Telink；DVC 低层 I2C/boot、SH SPI port
的位置仍在 AFE 目录；SOC 和完整板级路径通过生产函数/端口替身测试，未全部纳入九 TU
无 SDK 库。这些是后续移植清单，不能把局部脱离 SDK 写成整个 Core 已完全独立。
State 源文件是固定 OTA 参数包摘要输入，本轮保持受控输入，未为整理头文件重算更新 ID。
生产 `bms_types.h` 保留 TC32 与 SDK 的精确类型 ABI，host 分支无需 SDK；旧 TC32 GCC
不接受重复 typedef，已用明确编译条件处理，没有切换工具链/启动/链接体系。

六配置的参数输入摘要、更新组/ID 与资源基线一致；产品参数头与 SDK 源码差异为空，
产品目录仅删去合并文件的 sources 条目。协议地址、CRC/BCC、字节序、61001、温度
`(°C+40)*10`、正放电负充电 mA、CFG2/schema 3、APP/Flash/OTA 边界保持。
未重引主动 Open-Wire。D011 PB5 fuse 安全低电平、D013 无 heater/balance、D014 无 heater、
TS3 NC/TS4 MOS NTC 保留。

## Standards

独立 Standards 代理按固定基线到 `41dfac6d9591aebe7cef1762112be7389dcebac0` 审查，发现一项 P2：取消 SH 重复授权时，
原直接均衡 OFF 动作消失，替代入口的首次读失败阻止尽力关闭。`84c0729b317f4c27b26362da6943bb2596061935` 恢复 OFF
写零尝试和回读确认；非零请求首读失败仍禁止写入。新增注入在旧代码先复现 D011/D014
失败，修复后相关检查通过。代理只读复核确认关闭，未发现其他具体新增问题。
代理没有执行测试，测试结果来自本报告收据。

## Spec

独立 Spec 代理发现上述 OFF 问题，以及公共 PM/Core 仍有 SDK 依赖的部分实现边界。
OFF 问题同上关闭；`2daaf794342671a8ac6cd8340901aeef78a5ec52` 将真实生产保护性 PM 策略并入现有 Core，代理复核四计时器、
无效条件保持、充电门槛、双恢复条件、选择顺序/同到期优先语义均保持，PM 子项关闭。
State/正常 Suspend/板级总线等剩余依赖在上一节明确列出，未宣称完整 SDK 隔离。
复核未发现新增代码问题；最终测试及全清单归档由主代理完成，实板行为仍未验收。

## 最终验证与资源

所有最终目标 ELF/MAP、完整回归和 Cppcheck 固定到 clean `9845fca310e3aaca0800fbe38cf6a736efa23dd5`。文档生成后执行
的 sanitizer/CMake 仍使用相同生产代码；sanitizer 开始/结束的 bms/tests 指纹完全一致。

| 验证 | 实际结果/范围 |
|---|---|
| 完整 host 回归 | 116/116，0 FAIL，约 474.8 秒；D008 27、D011 26、D013 24、D014 36、shared 3；source_changed=false，无 integrity error |
| 源码清单 | 四产品 `sources --check` 通过，最终完整回归包含实际预处理/装配检查 |
| 目标构建 | TC32 production D008 16S LFP/20S NMC/24S LFP、D011/D013 10S LFP、D014 8S LFP，六配置 link/resources 通过，image_generated=false |
| 无 SDK CMake/CTest | 九个 Core TU；四产品各 5/5，覆盖 journal、保护/状态/CRC、ETA、公开头/时钟、保护性睡眠策略 |
| ASan/UBSan | Windows x86_64 LLVM-MinGW UCRT 20261006 / Clang 23.1.3；11 个检查族、30 个产品调用全部通过。覆盖 Core、D014 联合生产 TU、DVC ADC/SOC、保护、SH 均衡/恢复、feature、Modbus、存储和授权事务 |
| sanitizer 工具证明 | 官方包 SHA-256 校验；clean 探针正常，越界触发 ASan，signed overflow 触发 UBSan。WSL 因宿主资源不足未执行用例，改用树外原生工具链；Windows 不支持 LSan，detect_leaks=0 |
| sanitizer 编译边界 | 初跑 21 PASS/9 编译失败，9 项经保留 `unused-but-set-global` warning、仅不升级 error 后通过；夹具重复同类型 typedef 使用 Clang 兼容选项。没有修改源码或生产 TC32/GNU 回归门禁；不是宣称新增 Clang 零 warning |
| SOC 前后对比 | 七天合成轨迹 10080 点逐字节一致，SHA-256 `e040f614d41b8106f4c1d02363c4b8c02b7f937fe68ebb4be8ee60ebb41f3827`；完整回归另含 30/90 天合成场景，不能替代实车体验/精度 |
| Git/输入 | SDK 和参数头未改；六配置参数摘要/更新组选项/ID 一致；逐轮 `git diff --check`，只提交相关文件；没有 BIN/烧录/OTA |

资源单位为 B；Flash 槽位 126976 B，SRAM 32768 B。RAM 列为地址跨度，包含 RAM code、
cache、IRQ 栈；另留主栈预算 3072 B，余量不是实测最高栈。D008 基线含第 0 轮验证入口/
诊断编译修复（基线 SHA、dirty=true），SH 基线为 clean 原始 SHA；二者不能混写为纯净同一快照。

| 配置 | Flash 前→后（变化） | Flash 剩余 | RAM 跨度前→后（变化） | 预留主栈后增长余量 |
|---|---:|---:|---:|---:|
| D008 16S LFP | 114324→114244（-80） | 12732 | 22932→22884（-48） | 6812 |
| D008 20S NMC | 114340→114260（-80） | 12716 | 22932→22884（-48） | 6812 |
| D008 24S LFP | 114340→114260（-80） | 12716 | 22932→22884（-48） | 6812 |
| D011 10S LFP | 111780→111828（+48） | 15148 | 22276→22224（-52） | 7472 |
| D013 10S LFP | 108292→108404（+112） | 18572 | 22184→22140（-44） | 7556 |
| D014 8S LFP | 110772→110900（+128） | 16076 | 22272→22220（-52） | 7476 |

Cppcheck 2.21.0：D008 分析 39 个 TU，82 个 style；D011/D013/D014 各 38 个 TU，
分别 72/73/76 个 style；无该范围 error/warning 级发现。逐条位置、ID 和保留理由已归档。
主要为参数名称/无名声明、作用域/const 建议、关闭能力/诊断引出的 unused、固定配置条件
及 unsigned 序号回绕守卫。未为消除 style 批量重命名或删掉回绕保护。
SH 的 coverage gap 为 D008 专用 `bus_mux.h/sif_send.h`；D008 覆盖它们。
SDK 仅作解析依赖，未对原厂库全面静态分析；当前工具没有 MISRA addon，MISRA 未执行。
所以退出码 0 不等于全仓库零问题、完整覆盖或 MISRA 合规。

完整回归与原始失败基线的结构化比较出现 14 个从 None 到有证据的条目，主要是原来
未运行成功/夹具失配的用例；不是 14 项生产业务策略变更，更不是全系统前后等价证明。
明确改变的 DVC 失败流程、SH 输出所有权和接口边界分别由本轮故障注入/联合场景验证。

## 交接、实板缺口与可理解性

现在跟踪 DVC 正常采样无需穿过 boot 重配置路径；SH 授权只看 Guard，硬件恢复只看
SH 后端；SOC 本帧输入可沿参数直接单步；feature 事务不再跨单函数文件；保护性 PM 的
输入、饱和计时和选择可以独立复现。不同事实仍有明确状态所有者，这比机械削减行数
更能减少维护时需要同时记住的条件。

尚未验证：MOS Gate 与保护/负载移除波形、AFE 实际 watchdog 和总线静默、转换周期及
温度启动、DVC 启动零点/共口模式、均衡电流/加热 fuse、Suspend/DeepSleep 电流与唤醒、
最坏栈/时序、真实 Flash 掉电、客户 OTA 和各硅修订。D011/D013/D014 原始图纸/BOM
缺口仍在。软件模型和 ELF/MAP 不关闭这些验收项，也不声明已经完成新 MCU/AFE 驱动。

原始日志、工具、对象、ELF/MAP、轨迹和故障注入中间文件均在报告所列用户临时区；
仓库只保存这份报告及两份正式附录。没有生成正式 BIN、烧录或执行 OTA。
交付分支可继续按原提交逐轮 review；如需集成，应在确认原工作区脏修改后另行处理。
