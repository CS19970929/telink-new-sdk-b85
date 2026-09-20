# BMS 工程续审：失败传播、采样周期与分支契约

## 审核范围与依据

本轮先fetch并核对本地branches/worktrees/最近提交，再检查实际编译输入与生产函数。在已有 `BMS_ENGINEERING_AUDIT_2026-09-21.md` 之后继续推进；不把此前的SOC方向、SC恢复、FET缓存修复重复计为本轮成果。

| 对象 | 本轮起点 | 本轮交付 |
|---|---|---|
| D008 `refactor/d008-common-bms-features` | `4f21379` | 审核报告、只读跨分支检查工具；不改固件 |
| D011 `refactor/d011-common-bms-features` | `527c5a6` | `2360eef` 休眠修复；`ecee050` Git provenance修复 |
| D013 `refactor/d013-common-bms-features` | `f265334` | `c0f4fd1` 休眠及采样调度修复；`1f062dd` provenance修复 |
| D014 `refactor/d014-common-bms-features` | `4c42a4b` | `50d19b9` 休眠修复；`7eee1c4` provenance修复 |
| Windows `feature/windows-afe-hw-protection-editor-v2` | `852452e` | `fcb577f` 修正SOC录制/回放文档与分支能力边界 |

D008原工作树的 `app.c`、`d008_product_profile.h`、`todo.md` 用户编辑保留，不进入提交。本轮D008测试和编译使用独立干净checkout的 `4f21379`（已提交16S配置），不覆盖/验收用户未提交24S修改。本轮没有烧录、OTA或控制真实MOS；提交保存在本地，未push。

## 最重要的系统问题

当前已经有公共SOC、保护、features、AFE guard和持久化模块。主要风险是调用方的时间/有效性/返回值契约，以及多个产品分支只同步部分修复。进一步增加运行时抽象不能解决这些问题。

本轮保留裸机C和编译期backend、固定source order，不增加动态内存、callback框架或统一GPIO层。只补真实失败出口、明确状态证据、复用已有采样调度，并用执行生产函数的Host测试约束行为。

### 1. 已修复：SH AFE拒绝休眠，MCU仍继续深睡

`sh3673510_control_sleep()`、backend、guard原先都为void。AFE未ready、wake已有效、关均衡/关FET/charger-wake配置/SLEEP写入失败都可直接返回，app却继续进入MCU deep sleep。

SH调用链现在传回结果；失败保持MCU服务既有guard恢复。所有休眠尝试均失效旧采样和FET命令缓存；恢复资格计数重来，故障锁存保留。SLEEP请求在回执丢失时可能已经到达AFE，因此发命令前即标记需要NORMAL/runtime/protection恢复，不能继续假定AFE在正常态。

guard处于WDT静默期时不发SPI、不重置其等待计数，也不把静默视为AFE休眠成功。该软件选择可能增加持续失联下的耗电；真正的强制低压断电策略依赖各板供电与WDT/Powerdown实测，仍未关闭风险。

### 2. 已修复：显式深睡绕过OTA/UART门禁

SH `blt_pm_proc()` 先处理开关/低压/通信异常的直接deep sleep，后面才判断OTA并禁用BLE suspend。后面的判断无法阻止已经发生的深睡调用。

现在全部显式入口先检查OTA、SDK Flash session、bus mux与UART TX，再做AFE准备；之后再次核对现有PAD唤醒条件。没有增加BLE连接永久禁止低压休眠的规则，没有改既有低压与开关/PAD优先级。

### 3. 已修复：一次失败抹掉整个休眠资格时间

旧代码在调用前清零低压等计数。失败后可能再等整段小时级时间。现在计数饱和、成功才清零；失败至少隔3秒重试，正常tick回绕不会突破限速，整数溢出不会延长到期时间。

### 4. 已修复：D013缺失PM采样唤醒

D011/D014已有200ms app wake deadline，D013仅主循环检查elapsed。BLE suspend可拉长采样间隔，但软件保护/硬件恢复仍以每帧200ms计数。

D013复用另两项目已有的scheduler：callback只置位，AFE/SOC/MOS/diagnostics在主循环执行，overrun合并为一次，不制造追赶帧。没有移植D011板级IO。实际BLE广播/连接/latency下的采样抖动仍需仪器或日志确认。

### 5. 已修复：干净构建身份误记为未知

SH工具把 `git status --porcelain` 成功且空输出当成失败，导致干净manifest为 `dirty:null`。D008已有 `allow_empty` 修复而SH漏同步。本轮复用相同实现与4项测试：clean=false、modified=true、Git失败=null，并核对Build ID来源。最终SH默认固件从提交后的干净树重建，Build ID对应上述交付HEAD，manifest的dirty=false。

## 新增测试与工具

- `tests/sh3673510_sleep_host_check.py` + C fixture：编译并执行control/backend/guard/app真实函数；逐次SPI失败、回执丢失、唤醒竞争、SDK拒睡、恢复失败、旧样本/命令失效、WDT静默、连续故障、限速、饱和与tick回绕。原代码43项断言失败；最终66项断言通过。
- `tests/sh3673510_sample_schedule_host_check.py`：执行真实采样scheduler，覆盖deadline、重复callback合并、无效采样、overrun、tick回绕。三产品均运行，不把D011集成测试当作D013/D014的板级依据。
- 既有 `tests/run_host_regression.py` 自动发现新检查；本轮没有另建测试框架。测试临时产物使用用户临时区。
- `tools/audit_product_branches.py`：只读指定Git refs，输出机器可读JSON，记录commit、固定source order哈希、公共文件哈希、SH采样接入、Git provenance AST与开发录制能力。`--check` 对意外分叉返回1，读取失败返回2；不checkout、不fetch、不自动同步文件。

该工具要求四产品SOC/storage核心与SH的features/guard保持当前应有的一致性；D013软件保护分叉仅报告，不强制覆盖。产品GPIO、NTC、AFE控制寄存器实现不做跨板等同。只检查已提交refs，不验证用户脏文件和实际硬件。

```text
python tools/audit_product_branches.py --check
python tools/audit_product_branches.py --ref D013=f265334 --check
```

第一条在本轮交付分支通过；第二条作为负例能检出旧D013采样接入和provenance遗漏。新增测试被本机既有 `.git/info/exclude` 的 `/tests/` 规则隐藏，本轮逐文件显式纳入Git，未修改用户exclude规则。

## 验证与资源

| 产品 | Host回归组 | 默认正式BIN bytes | data+bss bytes | `_ram_use_end_` | 扣600B栈预留后的地址余量 |
|---|---:|---:|---:|---|---:|
| D008，已提交16S | 23 | 121092 | 11480 | `0x8461b8` | 7152 |
| D011 | 18 | 111252 | 10472 | `0x845ccc` | 8412 |
| D013 | 18 | 110628 | 10456 | `0x845dbc` | 8172 |
| D014 | 18 | 110932 | 10472 | `0x845ccc` | 8412 |

data+bss不包含所有RAM code/保留区，故同时核对MAP。各产品startup均为TLSR8251，SRAM上界 `0x848000`；余量不是实测栈高水位。相对本轮起点，SH正式BIN分别增加240/400/240字节，data+bss增加8/12/8字节。

- SH：各18组Host通过；SW/HW=1/1、1/0、0/1、0/0全部TC32 clean build、check-fw、MAP、manifest、verify通过，编译0 warning/error。最后恢复默认1/1并从已提交干净树重建。非默认组合不是发布固件。
- SH默认cppcheck：各114项style，0 warning/error，35个应用C编译单元、coverage gap=0；结果为PASS_WITH_FINDINGS，未执行MISRA。工具provenance追加修复后只需重新运行tooling测试和身份重建，固件生产C未再次变化。
- D008：23组Host及TC32 clean build/check-fw/size/MAP/manifest/verify通过；26项已有compiler warning，本轮没有清理。没有重跑D008静态分析或四组合，不能将SH验证范围套给它。
- Windows：客户WPF、内部WPF、CLI Release构建均0 warning/error；`test-diagnostics.ps1`、`test-ota-protocol.ps1`、`test-cli-offline.ps1`通过。只涉及离线模拟和构建，没有真实BLE/UART/OTA证据，Android本轮未重新构建。
- 本机日志：`%LOCALAPPDATA%/CodexTemp/bms-pm-audit-20260921/`；各产品标准CLI输出保留最终BIN/MAP/manifest。CI报告对应修复时的dirty树，最终clean rebuild日志与manifest提供提交身份，不把两者混成同一release证明。

## 产品差异与未关闭事项

| 产品 | 当前证据支持的差异 | 关键未决 |
|---|---|---|
| D008 | DVC1124-2/I2C，提交默认16S LFP；低边AUTO_DIODE、ACC深睡、PC4断电 | 用户本地24S编辑的profile身份一致性；Gate/WDT/COW/供电实测；编译warning |
| D011 | SH3673510/SPI，10S/250µΩ，RS485、heater；PB5 fuse安全低 | 电流零漂/增益、SC/LOADOFF、NTC、sleep/wake与通信供电 |
| D013 | 当前代码SH3673510/4S/100µΩ、direct UART，仍继承D011身份/IO | 专属原理图/BOM缺失；软件保护First/Second及温度策略仍分叉；不能仅改名视为完成适配 |
| D014 | SH3673510/SPI，8S/667µΩ、RS485；heater/TS3禁用 | RN4/MOS NTC、容量与保护参数签核、独立numeric product ID兼容策略 |

Protection/MOS方面，本轮降低了失败后丢失监督、旧采样/命令证据残留与采样周期分叉风险。AFE硬件保护和软件保护继续独立，common-port续流与芯片特有恢复条件保留。

Heating/Balance已有ARMING、可信电压/温度、Open-Wire互锁和requested/actual边界；本轮没有改变这些策略。真实断线、关断写失败、反向续流和温升仍是硬件验证任务。

Boot Current Zero Calibration仍是SH主产品缺口：当前采样使用CADC换算，未发现历史boot-zero分支对应的完整运行调用链。SDK `ext_calibration.c` 和SOC deadband不是BMS零流校准。需要确认真实无流条件、CADC新鲜性、零点稳定度、超时和MOS-safe fallback；不能按别的AFE寄存器猜实现。

Flash/参数的现行journal/独立参数域继续沿用，本轮回归通过不代表长期Flash寿命或所有掉电位置实测；OTA协议离线通过不等于无线断电恢复成功。MCU Self-Test仍需单独建立实际编译owner、执行周期、失败策略、资源与watchdog证据，不能把SDK checksum当完整CPU/RAM运行自检。本轮不是全系统认证或所有模块穷尽审核。

## 上位机与下一阶段闭环

复用既有 `bms-cli capture` 绑定明确MAC/串口，保存身份/Build ID、raw frames、gap、错误和ZIP；通过 `--json` 作为自动化输入。它是观察证据，不是完整算法Replay。

`record soc --inputs` 的对应固件目前仅在 `codex-soc-framework-validation`、`codex-soc-d011-validation`、`codex-soc-d013-validation`，没有进入四个主产品分支。只给主产品分支传 `BMS_SOC_RECORD_ENABLE=1` 不会增加不存在的实现。Windows旧文档把普通CSV和GUI查看误称为直接Replay，本轮已纠正。

最高价值的后续顺序：

1. 将SOC validation分支的学习方向修正/输入契约与本轮恢复、低功耗补丁有控制地整合，扩到D014前验证资源。执行真实生产C的record → audit → replay → A/B，禁止把缺失输入补成正常值。没有独立电量参考时不输出SOC精度结论。
2. SH零流校准与实板电流/温漂闭环；D013先补硬件资料，D014先确认NTC/参数。
3. 实板故障矩阵：SPI失联与WDT/低压耗电、SLEEP回执丢失、PAD竞争、OTA中开关变化、BLE采样周期、SC持续故障和Gate/Vgs。记录板号/BOM、commit、参数、环境与仪器。
4. 给D013保护分叉补生产输入差分测试，再决定语义是否统一；先证明差异，避免整文件覆盖。
5. 在以上闭环稳定后再补MCU Self-Test和长期Flash/OTA断电/噪声测试，避免先增加框架而继续遗漏状态边界。
