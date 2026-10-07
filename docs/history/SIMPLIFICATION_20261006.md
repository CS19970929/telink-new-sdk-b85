> 历史记录：仅代表本文标注的日期和固定提交。当前实现见 [文档导航](../README.md)，当前验证以本轮报告为准。

# 第二轮简化：一条主流程、显式产品差异

审查起点为 `codex-bms-monorepo` 的 `f8047d168afcdc4844f6228a713cc447631d3536`。实际工作树是 `D:/telink/bms-monorepo`，四产品实际编译清单未改变。原厂 AFE 资料、硬件参数、当前 CFG2/State/Event schema、九类更新编号、wire 地址、Boot/APP 边界和既有 MCU reset/watchdog/OTA 重启策略保持。

## 复杂度与本轮减法

最大的偶然复杂度是 SH 三份近乎相同的配置、app 两份主循环、无消费者的公共接口，以及测试对函数文本/历史 SHA 的绑定。SOC 的主要复杂度属于实际算法与持久状态，不能按默认开关判断是否可删除。

- `bms/products/sh3673510_defaults.h` 只保存三块 SH 板共同接线和 AFE 默认；产品文件先显式给出串数、Rsense、balance/heater/NTC 能力，再直接 include 一次公共默认。没有宏覆盖继承链。D011 加热 PB4/PB5 仍只在 D011 文件。
- `app.c` 共用 `main_loop()`、`mos_update()`、FIFO 定义和 event 秒门禁；D008 关机保持、UART/SIF 复用、各板 GPIO 和低功耗流程继续独立。
- 将相同的 CHG/DSG 配置无效、open-wire 硬阻断查询合并为 `bms_features_outputs_blocked()`；heater 的方向阻断继续由后端处理，不混入硬阻断。
- SOC 删除始终与 `g_soc_input_valid` 同步的 `g_soc_input_ready`；`set_calsoc()` / `set_dispsoc()` 收回模块内部。
- 删除无业务消费者的 `storage_record_format()`、`storage_record_crc32()`、`bms_state_store_write_learning()`、`bms_features_charge_blocked()`、`bms_features_get_openwire_result()`、`SH3673520_SetCellCount()`、`SH3673520_ReadStatus()` 及其数据类型、`DVC1124_ReadCoreOtThresholdCode()`、`DVC1124_ClearCoreOtEventLatched()`。初始化串数仍由真实 SCONF4 配置写入；CRC、学习元数据事务和核心过温 RC 事件锁存保留。
- 删除只包含未使用标记和重复声明的 `dvc1124_boot.h`；后端原语的边界仍为 `bms_afe_driver.h`。SH Modbus 不再编译四个虚假的 DVC no-op 函数。guard 删除当前两个后端均不可能执行的第三后端条件分支。
- 测试共用既有 `validation_support.function()`，删除十份独立函数体扫描。工具单测内部已枚举产品配置，因此由四次相同执行改为一次；真实产品行为测试仍按产品执行。

## 当前产品差异

| 产品 | AFE / 串数 / Rsense | 保留的板级差异 |
|---|---|---|
| D008 | DVC1124；16S LFP / 20S NMC / 24S LFP；200 µΩ | I2C、PB1 负载恢复证据、AUTO_DIODE、LDO/shutdown、UART/SIF 复用、heater/balance/NTC |
| D011 | SH3673510；10S；250 µΩ | balance、TS3 heater、TS4 MOS NTC、PB4/PB5 安全约束 |
| D013 | SH3673510；4S；100 µΩ | 无 balance/heater/MOS NTC；板级映射及原图/BOM待验证 |
| D014 | SH3673510；8S；667 µΩ | balance、无 heater/TS3、实装 TS4 MOS NTC及图纸差异 |

预处理核对包含四产品 414 个相关宏：413 项 token 相同，D013 debug LED 的 `0` 与公共 `0u` 数值相同。三 SH 产品的真实默认构造、校验和板级能力由行为测试核对；没有更改继承容量或未签核阈值。

## 维护者需要掌握的调用链

`main()` → normal/retention 初始化 → `main_loop()`：BLE SDK → Runtime → 到期 `app_sample_task()` → event 秒任务 → D008 mux/SIF → Modbus → State checkpoint → 板级 PM。

每 200 ms 到期只补采一次：`bms_afe_sample()` → guard/选定后端 → AFE 原始采集、校准、有效性和 SW/HW 保护 → `app_update_soc_from_sample()` → `mos_update()`。SH CADC 的转换完成与 400 ms 新鲜性窗口独立于轮询周期，不能把每次调用当作新 ADC。

MOS 链为应用请求 → guard 授权/通信/配置/样本门禁和双向硬阻断 → 后端方向保护/物理恢复/硬件命令 → AFE 反馈。请求、已发命令和物理反馈不能合并成一个 bool。SC/OCD/OCC 仍要求物理恢复证据，关 MOS 后零电流不能解除保护。

参数链为 Modbus/BLE 候选 → 参数或 AFE 所有者的校验/授权 → Config/State/Event → `storage_record` → Telink Flash。Config 候选与已提交缓存、State 待保存与已落盘值有不同掉电含义，继续保留；九类 OTA 更新编号仍按域处理，三域不是跨域原子事务。

## SOC 与其他必须保留的复杂度

SOC 积分、OCV、真实/显示 SOC、满空端点、ETA、校准及学习状态继续使用原算法。学习默认关闭，但当前 CFG2 可合法开启，已学习容量可影响有效容量，不能删除。新输入与上一个接受样本的电流/时间也不相同，方向切换和重复帧需要分别判断。本轮 SOC 仅减少一个同义状态、两个公开修改入口，未宣称算法大幅缩小。

AFE guard、器件后端、hardware profile 和特殊 RC/W0C 寄存器处理具有不同安全责任；不合并。Modbus parser 已按完整范围预检并限制单一状态所有者，本轮保留直接 if/switch。UART DMA/IRQ 所有权、RS485 DE 超时、SIF 双缓冲、BLE/OTA 与 Flash/PM 互锁也保留。

## 测试与证据

原 DVC 固定策略失败来自 Windows CRLF 与 Linux LF 的文本 hash 差异。核对当前原厂资料与寄存器实现后，删除历史 hash fixture，改为实际事务顺序、保留位、RC 事件、全部初值和逐访问失败/回读损坏断言，覆盖三 profile × HW on/off。没有改 hash 来掩盖业务差异。修复提交 `a7b5430a618b510c97f78f2f19a4f5d1fbdffd1d` 的 GitHub CI 已全部通过。

应用 MOS 请求以真实函数执行和参数 spy 验证，代替冻结 `chg_target` / `dsg_target` 局部变量名。SH BSTATUS1/BSTATUS2 的原字节向量与事务断言改走生产实际使用的 `ReadRegs()`；不是删除状态读取测试。仍需要提取函数的 SDK 相关夹具明确属于软件证据，不能冒充原始 TU 或实板。

本轮日志、预处理快照、ELF/MAP、资源结果和完整机器报告位于源码树外：`C:/Users/Administrator/Documents/CodexOutputs/bms-monorepo/simplification-20261006-f8047d16/`。日常阅读入口与历史 evidence 分开，原厂原件和历史认证/安全证据保留。

### 固定代码提交的验证

本机最终代码快照为 `e48bd259e7f46aaf545d24945bdf23c12b143904`，验证期间原始输入未变化。后续本报告及构建说明的文字补录不改变该代码快照。各项原始报告和失败诊断保留在上述外部目录，不能把历史运行的结果移记到其他代码提交。

| 验证 | 本轮结果与边界 |
|---|---|
| Windows 完整 host | 起点 178/178，最终 175/175；减少三次重复工具单测，72 个 check 文件仍全部登记；同输入保护观测无变化，完整运行无失败或输入完整性错误 |
| 主循环前后对照 | 四产品 × debug 开/关 × 160 次调用，动作顺序和 event 秒门禁相同；含 tick 回绕、D008 两种已提交关机保持；提取真实函数并替代外设，属于软件等价证据 |
| TC32 开发 ELF | 四产品 link/resource 全通过，0 编译警告；源码清单及顺序未改变 |
| TC32 生产 ELF | D008 三 profile + D011/D013/D014 共六配置全部通过，0 编译警告；每个 Flash 余量均超过原 8 KiB 门槛 |
| CMake portable | Windows Release 的 storage_journal、portable_core、soc_eta：3/3 通过 |
| ASan/UBSan | WSL 显式插桩范围 31/31 组通过；含寄存器语义、故障注入和 mutation；原始输入 SHA 与 Windows 最终 host 一致，不宣称未插桩夹具已覆盖 |
| fuzz / mutation / 长时序 | 完整 host 中的四产品协议 fuzz、四类保护 mutation、SOC/simulator、Flash 故障注入及 D014 22 个生产 TU 链全部通过 |
| Cppcheck 2.21 | error/warning/performance 均为 0；D008 37 TU，style 120→111；SH 各 36 TU，style 104→89；SH 的两个未编译头覆盖缺口仍记录，MISRA 未执行 |
| GitHub CI | [代码提交 e48bd259 的运行](https://github.com/CS19970929/telink-new-sdk-b85/actions/runs/37398813520) 九项全部成功；包含完整 host/CMake、ASan/UBSan、Windows TC32/static 和六个生产配置 |

WSL 运行记录的 Git 脏文件数受 Windows checkout 的换行配置影响；原始输入 SHA 为 `6bc10ae9b9cbd8cbeaac2972dd6b2bff03271ef4d659e04330ffe21b2977290d`，与 Windows 最终 host 相同。对齐 `core.autocrlf=true` 后 `bms` / `bms_tools` / `tests` 对代码 HEAD 的 diff 为空；不把该元数据差异当作源码变化，也不将 WSL 验证替代 Windows 生产或实板证据。

### 前后指标

| 指标 | f8047d16 | e48bd259 |
|---|---:|---:|
| BMS 自有 C/H 物理行 | 24,644 | 24,031 |
| C/H 文件 | 115 | 115 |
| 非 static 函数定义候选 | 383 | 369 |
| 公共头唯一函数名 | 359 | 347 |
| 顶层变量声明候选（含 const 数据） | 262 | 261 |
| 条件编译指令 | 450 | 448 |
| 单次调用 wrapper 候选 | 61 | 60 |
| SH 产品头两两相同宏名/文本值交集 | 298 | 3 |
| `*_check.py` 文件 | 72 | 72 |
| 测试 Python 物理行 | 6,137 | 6,168 |
| 完整 host 执行组 | 178 | 175 |

公共 SH 默认只计一份，产品头交集的分母由 315 项降为 18 项，因此不把这个词法指标称为整仓库重复率。测试 Python 增加 31 行来自语义/故障覆盖；历史 JSON hash fixture 已删除。C/H 与测试 Python 合计减少 582 行。`app.c` 1,738→1,574 行；SOC 2,038→2,034 行，没有以文件长度冒充算法简化。

| 生产配置 | Flash 字节：前→后 | RAM 地址跨度字节：前→后 | 最终 Flash 余量 |
|---|---:|---:|---:|
| D008 16S LFP | 118,756→118,644 | 25,032→25,028 | 8,332 |
| D008 20S NMC | 118,756→118,644 | 25,032→25,028 | 8,332 |
| D008 24S LFP | 118,756→118,628 | 25,032→25,028 | 8,348 |
| D011 | 115,204→115,092 | 24,204→24,196 | 11,884 |
| D013 | 114,180→114,052 | 24,184→24,176 | 12,924 |
| D014 | 115,028→114,900 | 24,204→24,196 | 12,076 |

计数口径为 `bms/**/*.c,h` 物理行；函数/状态/wrapper 是词法候选，不能当作编译后安全所有权数量或覆盖率。Flash 为 ELF 投影，RAM 为地址跨度，均不是实板栈水位或 BIN 实测。资源收益为 Flash 112–128 字节、RAM 跨度 4–8 字节；本轮主要收益是少记一套流程、少维护重复配置和无消费者接口。

本轮没有生成 Telink 固件 BIN；CMake 的 `CMakeDetermineCompilerABI_C.bin` 是 PC 编译器探测产物。实际 D014 ELF 位于外部证据目录的 `final-production/build/fcdfbbf61247/production/d014/825x_ble_sample.elf`。自行编译从 `D:/telink/bms-monorepo` 使用统一 `bms_tools/bms.py`；`link` 不生成镜像，明确需要 BIN 时才用 `rebuild` 及镜像验收链，详见 [构建、测试与交付](../BUILD_AND_TEST.md)。本轮未调整优化参数：开发全 `-O2`，生产仅公共 core `-Os`；app/AFE/平台/SDK 保持 `-O2`，ABI 及 Vendor 库沿用原体系。

实板仍须验证：四板真实 Gate/恢复窗口、C+/LOAD/PB1、ADC/NTC 时序和校准、watchdog 静默、重配置失败、reset/OTA、Flash 掉电、Sleep/Wake、RS485 DE/DMA 与 SIF 波形，以及产品容量/阈值签核。软件通过不解除 [硬件验收清单](../HARDWARE_VALIDATION.md) 中的阻断项。

## 继续审查：开发位置、镜像交付与剩余方向

按后续用户要求，`D:/telink/bms-monorepo` worktree 已移除，`codex-bms-monorepo` 直接检出到原项目目录。本节之前的路径和成绩属于固定历史快照；后续构建使用当前目录和新提交，不能复用旧收据冒充新镜像。原目录未跟踪的 D008 原理图与分支版本 SHA256 相同，已原样保留并另作树外备份。

最终 BIN 和 manifest 放当前项目 `firmware/<mode-profile>/<product>/`；编译对象、ELF/MAP、raw BIN 和 checker 处理中间文件仍在树外。六种生产配置的输出路径互不覆盖。批量入口沿用 `--all-products`，依次执行四产品，不新增 runner 或并发调度框架；命令及 D008 profile 选择见 [构建说明](../BUILD_AND_TEST.md)。CRC 失败保留上一个已发布镜像，成功发布后旧 manifest 失效，重新生成并校验清单。

本次继续完成三个接口收口：删除只有测试使用的 `bms_afe_hw_profile_init()`，测试改用生产实际使用的 getter；profile setter 与系统默认构造函数收回 `static`，减少三个公共声明。启动授权仍由 `LoadParam()` / `bms_config_store_validate_startup()` 负责；提交/回滚、Flash 字节布局、硬件配置和故障恢复不改变。D014 默认测试复用真实产品头和既有 builder/validator 夹具，删除手工复制的宏、类型和重复编译流程，原恢复阈值边界断言保留。

| 后续方向 | 当前代码依据 | 实施前必须证明 |
|---|---|---|
| 协议整帧读取减少重复工作 | `modbus_rtu.c` 逐地址调 getter，AFE profile 读窗口重复取得/校验同一请求配置 | 保持地址、字节序、错误码及失败时每个寄存器的行为；快照与失效语义不能猜测 |
| 参数编解码减少重复维护 | `bms_parameter_access.c` 与 Config 的 wire/Flash 编解码分别维护字段范围和单位 | 协议大端与持久化小端、字段布局和掉电事务仍各自明确；不引入通用反射/表驱动框架 |
| 测试逐步减少源码切片 | `validation_support.profile_prefix()` 及 SDK 相关夹具仍提取生产函数 | 优先链接原 TU，只替代平台 I/O；行为/故障覆盖和结构化失败信息保留 |
| 构建输入扫描按实际依赖收敛 | `_capture_compile_inputs()` 每次扫描全部 SDK 头及工具；当前方式保守可靠 | 以 TC32 实际依赖闭包证明不漏头、`.inc`、库、工具或宏，跨四产品/profile验证增量失效 |
| Flash/RAM 继续按 MAP 审核 | 删除未引用源码不必然减少已链接 section；当前 production core 已使用 `-Os` | 固定输入、实际分配 section、相同场景行为；SDK/AFE 时序及 ABI 不因体积目标更改 |

学习容量可由当前 CFG2 开启，真实/显示 SOC、有效样本与上一个接受样本、请求/命令/反馈、故障 latch 与物理恢复窗口，以及 Config/State/Event 掉电域，仍是必要复杂度。本轮不以删除这些机制换取行数。上述后续方向是待验证候选，不宣称已完成。

## 继续简化 Modbus 读路径

起点为 `a1f69829629f2181ae4515559ba157657b0671bd`，直接在当前项目分支开发。日志、diag、历史事件和普通寄存器读取现在只填写 payload，再共用一处响应头、长度、CRC 和广播返回；历史事件仍仅在原起始地址接受最多 100 words，普通地址仍先完整预检再读取。删除 `read_event_log_frame()`、三个只返回板级常量的 `afe_hw_profile_product_*()` 和重复的 SOC extern 声明。产品串数直接使用已有 `SeriesNum`，Rsense/watchdog 直接使用对应产品配置，没有新增配置层、公共接口或全局状态。

AFE requested/effective 逐字读取经过审核后保留：Config 未 ready 时 getter 会重试初始化，effective 会查询后端诊断；整帧缓存失败会改变后续字的重试结果。授权 session 的到期查询也保持原入口。响应整理不改变这些 getter、提交/回滚、保护资格、Flash/OTA 或 MOS 行为。

`modbus_address_host_check.py` 从两个函数的源码切片改为编译完整生产 `modbus_rtu.c`，复用原产品头和既有 host SDK 边界，并链接真实 CRC/State/diag/logger。覆盖所有 profile 子区间、每个字的失败及后续重试、元数据、SH 有效/无效映射、日志/diag 最大 125-word 响应、历史事件 1..100 words、跨界、广播和缓冲区哨兵。配置/事件/AFE 所有者仍是可控替身；这不是全系统或实板证明。diag 夹具继续覆盖四种日志/trace 开关组合，并复用已有 `run_c`，支持 sanitizer。

六配置在同一新夹具下分别编译起点源码和修改后源码，12,303 个读帧全部通过独立字节/CRC 断言，前后响应摘要及逐字 getter 调用顺序相同。摘要只用于此次前后对照，不设历史源码 hash 期望。

本机完整 host 175/175 通过、运行期间源码未改变，CMake 3/3 通过；既有随机协议场景仍为每产品 100,000 帧，现有 mutation 检查通过。另在树外注入错误 CRC、历史事件越界和串数偏移，新整帧测试均正确拒绝。新增读帧观测会在旧 host 报告对比中显示四条新增记录；它们是测试证据扩充，wire 等价由上述独立前后对照证明。两个相关整帧/diag 检查已加入现有 ASan/UBSan job，成绩以本提交 CI 收据为准。

| 指标 | 起点 → 修改后 |
|---|---:|
| BMS 自有 C/H 文件 | 115 → 115 |
| BMS 自有 C/H 物理行 | 24,017 → 23,944 |
| `modbus_rtu.c` 物理行 | 863 → 790 |
| 本模块内部函数 | 减少 4 |
| 本模块条件预处理指令（含 elif/else/endif） | 33 → 24 |
| 公共声明、全局/static 状态、产品默认 | 未改变 |

源码指标、六配置前后对照、本机完整 host、TC32 六 production 资源及四产品镜像/静态收据统一放在树外 `C:/Users/Administrator/Documents/CodexOutputs/bms-monorepo/protocol-simplify-20261006-a1f69829/`；以各报告的实际输入指纹和 Git commit 为准。最终镜像仍在当前项目 `firmware/`，编译优化保持原配置。资源与 CI 结果按实际提交留证，不复用本节前面的固定历史数值。后续仍可审查参数编解码与构建输入扫描；requested/effective、存储事务和物理恢复证据继续保留。

## 继续简化 Config 固定字段编解码

起点为 `8594eb5ac07c828c769aac53422a6c5f8122eea7`。Config 内的软件保护、AFE requested profile、七个业务参数和六个更新编号，共用两个局部 16 位编解码函数；蓝牙后缀直接复制固定字节。默认值构造 `bms_config_user_defaults()` 只有本模块消费者，收回 `static` 并删除公共声明。没有增加文件、公共 API 或运行状态。

共用范围只包括连续 `u16`。业务参数前缀长度以编译断言约束，字段顺序以独立按名称构造的字节期望校验；校准 offset/gain 和 SN 仍逐字段放到原位置。PC 默认 ABI 的用户结构为 56 字节、offset 字段位置为 16；packed 用户结构为 54 字节、offset 位置为 14。结构体 padding 不落盘，CFG2 schema 2 的 payload 始终为 322 字节。编译断言使用 TC32/GCC 已有的 `__builtin_offsetof`，因为 TC32 `<stddef.h>` 与 SDK `size_t` 定义冲突；SDK 类型、ABI 与工具链不改动。

固定位置的独立测试覆盖 10,000 组记录：零值、全置位、最高位/负 offset、`INT32_MIN`、随机字段、六个编号、预留字节忽略、蓝牙末尾 NUL 和写入哨兵。同一新夹具分别编译起点与修改后完整 Config/State/Event/parameter 模块，在普通与 packed 用户结构下均验证；每个布局还运行现有编号策略和 101..109 编号策略。字节序错误、业务字段偏移错误和解码值错误三个 mutation 均由独立字节/字段断言拒绝，编译失败不计作发现 mutation。

存储测试复用现有 `run_c`，保留三个执行程序及原有逐字节写中断、缓存回滚、启动授权、64 种 Config 更新组合、State/Event 独立更新、SN 会话和 platform 排他/页写/重试场景，并接入既有 ASan/UBSan job。fixture 中 Flash、部分产品默认构造和参数 validator 仍为替身；它证明生产存储代码的 host 行为，不证明实板掉电、设备参数实读或 OTA 验收。

修正后 Windows 完整 host 175/175、CMake 3/3 通过；存储检查在 WSL 的四产品 ASan/UBSan 运行全部通过，输入文件指纹与最终 Windows host 一致，运行期间源码与 HEAD 未改变。完整 sanitizer 范围和九项 CI 的成绩单另以最终提交收据为准。

本轮自有 C/H 文件仍为 115 个，总物理行 23,944→23,939，Config C 为 576→573 行。主要收益是共用八段重复字段处理和减少一个公共接口，不把少量行数变化称为整仓库重复率改善。生产优化参数保持不变；整机 Flash/RAM、编译警告和镜像身份以最终 clean commit 的六配置链接收据为准。

正式证据统一位于树外 `C:/Users/Administrator/Documents/CodexOutputs/bms-monorepo/config-codec-20261006-8594eb5a/`：`codec-differential.json`、`source-metrics.json`、`host-final/`、`sanitizers-storage-final/`、`resources-differential.json`、`images-report.json` 和 CI 收据。最终四产品 BIN/manifest 继续放当前项目 `firmware/`，没有另建 worktree。早期 SDK 头冲突和全 fixture packing 探针的编译失败保留为探索记录，最终验证仅采用修正后冻结输入，不抑制这些警告或以探索结果冒充通过。

协议 wire 的大端 word 顺序与 Flash 小端布局仍分别处理；保存事务、产品 tag/schema 拒绝、更新编号、校准算法和保护输出语义不变。构建扫描暂时保留全部输入哈希：收窄到依赖缓存还需证明新头文件遮蔽、`.inc`、工具/库变化及跨 profile 的失效规则，目前不为减少扫描引入新的缓存状态。
