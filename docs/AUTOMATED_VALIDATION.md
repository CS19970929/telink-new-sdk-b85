# 多产品自动化验证

目标是让公共保护、参数、AFE 编码、SOC、存储和协议的常见回归在 PC 上被发现，并留下可复现证据。绿色结果只证明已执行的软件断言，不证明整机安全、参数已签核或物理 MOS 已动作。

## 当前仓库审计结论

当前有效源码由四份 `bms/products/<product>/sources.txt` 选择。四产品 MCU 都是 TLSR8251；当前仓库没有 STM32/BQ769xx 产品，也没有进入四产品清单的 CAN 实现，因此没有为这些名字建立空测试。D008 使用 DVC1124，D011/D013/D014 使用 SH3673510。

真实主链为 `app` 调度 → `bms_afe_guard` → 所选 AFE 后端采样 → 共享报告/保护 → feature 与 MOS 仲裁；SOC 消费有资格的样本和时间补偿。配置由 `bms_config_store` 所有，运行态、事件由各自 store 所有，底层共享 `storage_record` journal。软件保护参数与 AFE requested profile 是独立数据，不把二者强行合并。

| 产品 | 装配 | Rsense | 加热/均衡 | 通信差异 |
|---|---|---|---|---|
| D008 | 16S LFP、20S NMC、24S LFP | 200 µΩ | 支持/支持 | UART/SIF 复用 |
| D011 | 10S | 250 µΩ | 支持/支持 | RS485 |
| D013 | 4S，硬件资料待核 | 100 µΩ | 禁用/禁用 | 直连 UART |
| D014 | 8S | 667 µΩ | 禁用/支持 | 隔离 RS485 |

公共业务已是一份源码，新增测试不再复制保护/SOC/状态机实现。板级能力、串数、Rsense、ADC 和恢复动作存在真实差异，应由产品配置/AFE 后端表达。现阶段不值得为了测试再增加 manager/interface 层或全面搬文件。保留 SDK/寄存器边界，优先直接链接可移植 TU；耦合模块暂以生产函数体编译，并显式列出替身。

审计发现原 runner 仅按脚本名字分配产品，公共存储/SOC 没有全部展开；部分脚本内部硬编码产品，日志标签与实际运行配置不一致；SOC 回放按不完整的 mtime 清单复用共享 EXE，可能测试旧代码。新入口解决这些证据缺口。旧源码 contract 继续作为结构约束，不能充当动态行为证明。

## 一条命令与报告

在仓库根目录执行，需 Python 3.11+ 和 GCC 兼容的 host C 编译器。Windows 示例：

```powershell
$env:CC = 'C:/qp/qtools/MinGW32/bin/cc.exe'
$env:PATH = 'C:/qp/qtools/MinGW32/bin;' + $env:PATH
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONUTF8 = '1'
python tests/run_host_regression.py
```

完整入口执行四产品源码清单、工具自测、已登记的适用测试和可移植核心三套断言。默认报告写入用户 `Documents/CodexOutputs/bms-monorepo/validation/<时间戳>`。`--output` 或 `BMS_TEST_OUTPUT` 可指定外部目录；拒绝源码树内路径和非空目录，避免覆盖旧证据。中间 C/EXE 在系统临时目录。退出码非零就是未通过，包括漏跑、超时、启动失败、源码变动和公共保护差分不一致。

```powershell
# 公共源修改后的本机软件 + 四产品 TC32 ELF/MAP/resource（不生成 BIN）
python tests/run_host_regression.py --with-targets
# 仅用于定位；报告明确写“部分验证”，不替代完整回归
python tests/run_host_regression.py --product d014 --only sh_register_scenarios_host_check
# 比较上一个已保存的机器报告，不会自动接受差异或改基线
python tests/run_host_regression.py --baseline C:/reports/previous/report.json
```

`--with-targets` 使用开发模式，D008 为开发默认 16S。发布门禁仍按 [构建指南](BUILD_AND_TEST.md) 使用干净提交、生产模式及 D008 三装配；本入口不调用 `rebuild/check-fw/objcopy`。单检查默认 300 秒 timeout，可用 `--timeout` 显式调整，超时保留日志并终止子进程树。

| 产物 | 用途 |
|---|---|
| `report.md` | 产品/领域表、配置摘要、失败上下文、历史差异、盲区 |
| `report.json` | HEAD/tree/dirty、源码内容指纹（含 SDK 和新测试）、host 编译器版本/hash、命令、耗时、结构化观察、近期提交 |
| `results.json` | 每项完成即保存，进程被中断时仍有已完成记录 |
| `junit.xml` | CI 消费失败；完整性失败也有独立失败项 |
| `<检查>-<产品>.log` | 编译器/断言原始输出；新增夹具编译或运行失败另保留生成 C 和编译命令 |

失败日志中的 `expected/actual`、保护组/等级/阶段/样本序号、随机种子、总线操作序号用于复现。`source_hints` 和近期提交只是排查线索，不是假定根因。部分旧夹具仍只输出 C `assert` 行号，应结合保存日志定位。

行为比较是结构化配置、寄存器轨迹和场景摘要的差异，不宣称没有变化就全程序语义等价；新增/缺失观察也会列出。未指定历史报告不作历史一致性结论。报告不自动下载旧 CI artifact，不把有保留期的 CI 当永久档案；维护者应将接受的报告目录与发布记录一起归档。

## 测试范围和可信度

| 领域/入口 | 执行的生产实现 | 关键自动验证 | 明确的替身/未覆盖边界 |
|---|---|---|---|
| `protection_scenarios_host_check` | 完整 `bms_sw_protection.c`、`bms_state.c` | 12 组×3 等级，等号、禁用、触发/恢复滤波、最大延时、泄漏式累计、1024 多故障组合逐项清除、无效温度；四产品相同输入输出强制一致 | 参数有效标志为替身；SOC Low 在 SOC 模块；不等于电流恢复物理证据 |
| `product_configuration_host_check` | 真实产品头、默认构造与 validator | 四产品及 D008 三装配，65 个 SW/35 个 AFE 字段，真实默认校验 | 开发默认，不代表设备已存参数或产品签核 |
| `sh_register_scenarios_host_check` | 完整 SH control 函数体、真实默认/量化/NTC | 0x40..0x54 实写轨迹；独立解码电压/电流/延时，串数/TS 门禁、初始化 MOS OFF；逐总线访问失败、写后读回不一致、wake 重配 | SPI/GPIO/RAM 寄存器；尚非官方手册逐位认证，也不模拟模拟量比较器 |
| `dvc_register_scenarios_host_check` | DVC 实际写入/量化/RMW/verify 函数 | 三装配保护写轨迹，电压/电流/延时独立解码、保留位、SC 禁用/启用、每访问后持续总线故障及读回错 | 非完整 DVC 芯片模型，未覆盖每个校准/状态寄存器 |
| `sh_safety_chain_host_check` | SH backend + guard + SW 联合运行 | 新样本资格、OV/UV、并发 SC、恢复不误开、重初始化保留 SC、NTC 无效、ADC 过期、tick 回绕至最终 FET 命令 | control/芯片、feature、校准/持久状态为替身；不是物理 Gate |
| 既有 AFE transaction/recovery/sleep/atomic 检查 | 生产事务、后端恢复、guard、采样函数 | 配置失败/回滚、总线静默/watchdog 窗口、恢复证据、新样本原子发布、wake 失败关闭 | 多个专门夹具，尚未合成完整系统 SIL |
| `features_scenarios_host_check` | 完整公共 feature 函数体、真实板级能力/错误状态 | 均衡资格和迟滞、61001 非实装槽位、异常样本、通信未知读回保持、heater ARMING/ACTIVE、停热/单次 fuse 命令、断线隔离 | snapshot/配置读取/总线/GPIO；不发出任何真实 fuse 动作 |
| `soc_scenarios_host_check` | 当前 SOC 生产函数体及样本入口 | 时间补偿、长时库仑/OCV/静置/显示、方向切换、小电流、30 天随机与 90 天浅循环、确定性轨迹 | 受控 100Ah 环境；非各产品默认电池实测模型，不把 generic OCV 当实际电芯 |
| `current_calibration_host_check` | 当前 K/B 整数函数 | 独立 int64 公式，INT32 边界、正负/饱和、100300 组边界/随机输入 | AFE ADC 误差、开机真实零电流由已有后端场景和台架另证 |
| `storage_host_check`、`flash_quick_check` | 公共 Config/State/Event/参数访问/journal | 保存加载、CRC/损坏、字节写中断、reset、跨产品/schema 拒绝、各更新编号保留/重置及回退、校准 | 四产品都跑；默认/validator 在该夹具有桩，由独立产品配置测试补充；非电源瞬态 |
| `modbus_host_check` | 真正 `modbus_on_frame`、CRC | 10 万确定性随机帧、有效 CRC 畸形输入、长度/半包/坏 CRC、广播/重复/跨域写、canary | 寄存器业务所有者是计数替身；不证明完整存储提交链 |
| UART/SIF/诊断检查 | 真实 UART ownership/IRQ 函数、封包/日志代码 | 各产品 DMA 占用、超时、复用/响应、非法地址/只读范围 | UART/RS485 PHY、BLE RF/SDK 并发、任意粘包吞吐仍有缺口 |
| `validation_mutation_check` | 外部临时副本的故意变异 | 保护触发等号、恢复等号、滤波取整、MOS 阻断四类变异必须得到业务断言失败 | 不是全项目 mutation score；编译失败/任意崩溃不能算抓住变异 |

虚拟时间通过推进生产状态机的样本/tick 实现，30/90 天场景不等待真实天数。随机场景固定种子，失败可重复。断言数和测试组数只是执行规模，不是行覆盖率、分支覆盖率或安全概率。

## 参数关系与行为统一的判断

相同受控输入下公共软件保护应一致；测试会比较输出摘要并拒绝不一致。真实产品默认可以不同：D008 的单体欠压 Third/filter 与 SH 不同，pack 阈值随串数变化，ADC/Rsense 和硬件量化分辨率不同，D013 禁用均衡/加热，TS4 MOS NTC 已按用户确认启用。不能把四次绿色理解为四块板物理行为一致。

AFE 测试区分 requested、编码、effective；受限的电流步长/延时向上量化不会自动成为产品允许误差。SH 电压按 5mV 编码，电流分辨率随 Rsense 改变；DVC 有不同的 OC1/OC2/SC 量化。软件保护每 200ms 调度，100ms 软件延时实质为一个采样周期，与 AFE 延时不能按字段值直接视为相等。

当前配置仍存在必须签核的高风险：D008 20S NMC 沿用公共软件电压保护默认、D008 默认 SC 关闭；SH 容量及部分 OCD 值是继承默认；D013 缺受控原理图/BOM。测试报告保留这些风险，不擅自改阈值，也不因 validator 通过而说配置适合真实电池。完整字段解释见 [四产品配置核对表](history/FOUR_PRODUCT_CONFIGURATION_AUDIT.md)，其固定基线值不能替代本轮报告。

## 扩展、CI 与维护约束

测试架构只有 runner、显式 catalog、薄编译辅助和场景夹具。新增 `*_check.py` 必须登记适用产品、领域、证据层级；漏登记/登记不存在的文件使 runner 失败。添加产品时更新产品清单、catalog、CI/生产矩阵和能力预期，并先证明相同公共场景输出一致，再补真实硬件差异。不得复制公共算法进 tests，不能用 stub 替换被测业务函数。

采用真实 TU 时优先直接链接；必须抽取函数时，辅助只替换 include 边界，不改函数体，源码签名变化应显式失败。现有部分夹具仍抽取函数，这弱于正式 TU 的 ABI/宏组合证据，因此四产品 TC32 link/resource 门禁继续保留。下一步宜把最有价值的联合夹具逐步连接到完整 TU，而不是先建设通用仿真平台。

CI 的 `host-contract` 跑完整入口并归档人/机报告，`host-sanitizers` 在 Ubuntu 对使用新公共编译辅助的核心/AFE/feature/协议/校准场景启用 ASan/UBSan。旧测试自带编译命令，不因设置 `BMS_SANITIZE=1` 就假称全部插桩。Linux ASan/UBSan 支持需实跑确认，本机 Windows host 通过不能代替。六配置 TC32 生产 job 不生成 BIN。host artifact 保留 90 天；长期证据需另归档，详见 [CI 运维](GITHUB_ACTIONS_RUNBOOK.md)。

新增功能的工作顺序：先给出可观察的预期和失败路径 → 使真实产品代码在场景中失败 → 最小修复 → 对应产品回归 → 完整四产品/目标门禁 → 人工审查配置与行为差异。修改 golden 或参数预期必须解释产品理由，不应为了绿色批量更新结果。

## 本次发现与修复

1. SOC 回放旧 EXE 复用：缓存遗漏产品、头文件和工具身份，未来时间戳可跳过当前编译。现在每次从当前输入编译到独立临时目录；自动回归故意创建未来时间旧 EXE，确认仍调用编译且不复用原路径。
2. SOC 夹具耦合 D008 PM：编译 SOC 时原先仍急切生成无关 D008 PM 函数，SH 产品可能因此无法回放。按实际模式选择 TU，增加四产品公共 SOC 入口。
3. 电流校准极端不对称：`raw_ma=INT32_MIN, offset=0, gain=999999` 进入原有对称饱和公式的特殊漏限幅路径，结果比统一公式多负 1mA。先统一 delta 到 ±INT32_MAX，再执行原有整数缩放。仅该极端边界改变；不改实际传感器缩放、协议、Flash 布局或正常校准路径。100300 组独立公式比较长期保留。
4. 测试产品归属和报告失真：SH 恢复脚本改为服从 `BMS_PRODUCT`；UART 所选产品实际运行且 D013 保持直连；公共存储/SOC/校准等展开四产品；旧 RS485 特定 contract 仅用于真正 RS485 产品，D013 由直接 UART 场景覆盖。

本次不包含并行任务已经提交的 SH 恢复修复；本验证基线包含它，不能把别的提交归入本次修复成果。

## 仍需投入的高风险验证

1. 软件下一优先：[D014 保护与恢复闭环](D014_SAFETY_LOOP.md) 已连接 22 个真实 TU，并修复 SH 软件过流的零电流误恢复。继续补 DVC、真实主循环调度、跨 MCU reset 的产品策略和实际 Gate/负载证据；不能把 D014 的采样任务闭环当作整机验收。
2. 协议下一优先：真实 Modbus 寄存器所有者/Flash 提交链和 UART 任意分片粘包压力，再接 BLE 应用协议层；不要伪造不存在的 CAN 产品。
3. AFE 下一优先：以受控官方手册逐位核准寄存器模型，增加随机合法 profile 的全域编码/解码与容差检查；现有默认轨迹与故障注入不是全配置空间证明。
4. 实板必做：芯片转换标志时序/ADC 误差、Gate/负载/充电器证据、SC/OCD/OCC 波形、Flash 掉电、电源复位/watchdog、Sleep 电流/唤醒、RS485 PHY/BLE RF、温度与 EMC。按 [硬件验证清单](HARDWARE_VALIDATION.md) 留独立证据。

HIL 可先从一块可控台架开始：受限供电、可编程单体/温度/电流输入、串口只读诊断、逻辑分析仪观测 Gate/DE/wake，复用软件场景的输入和预期。首期只闭合最危险的恢复和新样本资格链，不让台架建设阻塞 PC 回归，也不自动操作实物 fuse 或高能量短路。

## 2026-10-05 固定提交验证

实现提交为 `146c3ff686df5f667deb67b22e894a17fd06b132`，验证时 Windows/WSL Git 均干净。机器证据与完整报告哈希见 [AUTOMATED_VALIDATION_EVIDENCE.json](history/AUTOMATED_VALIDATION_EVIDENCE.json)。本节是该提交的历史记录，不自动代表后续 HEAD。

| 范围 | 实测结果 |
|---|---|
| Windows 完整回归 | 165/165；D008 39、D011 38、D013 36、D014 50、共享工具 2；0 失败 |
| Linux ASan/UBSan | 本机 WSL Ubuntu GCC 13.3，28/28；选定场景与 Windows 的输入指纹和结构化观察全部一致 |
| CMake Release/CTest | 3/3，断言保持启用 |
| Windows TC32 生产 | 六配置 link/resource 全通过；编译器 0 警告；未生成 BIN |
| CI | workflow 已更新并通过 YAML 解析；没有推送或冒称远端 run 已通过 |

六配置生产 Flash 余量：D008 三装配均 8460 字节，D011 12348，D013 13356，D014 12524。D008 距 8KiB 最小余量仅 268 字节；后续改公共代码应继续关注资源门禁。RAM 增长预算不等于实际栈水位，精确值与 ELF/MAP hash 在机器证据中。

正式完整报告位于本机 `C:/Users/Administrator/Documents/CodexOutputs/bms-monorepo/validation-20261005/final/`，插桩报告在同级 `sanitizers-final/`，生产 ELF/MAP/收据在同级 `targets-production/`。报告比较保留了本次开发报告到最终报告的字段命名变化；未把它伪称为与旧固件的全行为等价验证。当前软件回归已能自动阻止多类公共保护/参数/编码/算法/存储/协议回归，前述整机闭环和实板盲区仍然成立。
