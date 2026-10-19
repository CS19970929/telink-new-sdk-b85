# BMS monorepo：D008 / D011 / D013 / D014

基线分支为 `codex-bms-deep-simplification-20261016`；当前精简分支为 `refactor/full-project-simplification-20261018`。此前 `codex-remove-openwire` 从 `ee284189` 裁剪主动断线检测，见 [删除与回归说明](docs/OPENWIRE_REMOVAL.md)。

四产品固件的主开发分支是 `codex-bms-monorepo`。公共代码只维护一份，产品通过编译配置和 `sources.txt` 选择硬件输入与 AFE 后端。Windows 上位机仍在 `feature/windows-afe-hw-protection-editor-v2` 分支的 `bms-tool-windows/` 维护。

2026-10-18 全工程复杂度审查、逐轮精简提交与基线测试修复见 [全工程精简记录](docs/DEEP_SIMPLIFICATION_20261018.md)。

四产品的 AFE 配置、软/硬件保护与恢复、异常处理、休眠和电流换算见 [AFE 配置与保护/休眠整理](docs/AFE_PROTECTION_SLEEP_CURRENT.md)。

批次 A 的测试与源码解耦基线、目标与验收口径见 [测试解耦计划](docs/TEST_DECOUPLING_PLAN_20261018.md)。

D008 无放电启动 SCD、CBC 与三级放电过流的现场定位及配置顺序修复见 [启动 SCD 修复记录](docs/D008_STARTUP_SCD_FIX.md)。

四产品 AFE1 保留 1 秒通信异常确认，删除 ADC 活性、停更故障和三次新样本资格，见 [AFE 通信确认与活性删除](docs/AFE_COMMUNICATION_POLICY.md)。

SH 通信恢复保护锁存、分组恢复证据及后续等价精简，见 [2026-10-18 保护恢复与实现精简](docs/PROTECTION_RECOVERY_SIMPLIFICATION_20261018.md)。

MOS 失败补偿、DVC 恢复事务、SH 配置归属与本轮均衡回读共享的逐轮提交及保留裁决，见 [AFE 控制分轮重构](docs/AFE_CONTROL_REFACTOR_20261018.md)。

四产品反向电流立即恢复、D008 PB1 负载移除恢复及 D011 充电器加热门禁见 [恢复与加热说明](docs/CURRENT_RECOVERY_AND_CHARGER_HEATING.md)。

断线删除后的状态、采样接口及 SOC 继续精简见 [采样与 SOC 解耦](docs/SAMPLING_SOC_DECOUPLING.md)。

允许冷启动静置均衡、删除充电会话及 SH 未消费采样，以及后续高电压竞争选串，见 [均衡与采样精简及回归资源对比](docs/BALANCE_IDLE_SIMPLIFICATION.md)。

当前专题与阅读顺序见 [文档导航](docs/README.md)。

四产品采样、统一计时与低功耗主链重构见 [2026-10-17 主链重构记录](docs/MAIN_CHAIN_REFACTOR_20261017.md)。

存储五域收敛、分轮提交和新格式验证见 [2026-10-17 存储精简记录](docs/STORAGE_SIMPLIFICATION_20261017.md)。

第二阶段的驱动接口、重复事务与 SOC 状态裁决见 [2026-10-17 重构记录](docs/REFACTOR_PHASE2_20261017.md)。

后续四项减法、逐步提交与资源比较见 [2026-10-17 四项优化记录](docs/REFACTOR_PHASE3_20261017.md)。

有效保护配置、SOC 表示、参数块读取及 SH 恢复证据复用见 [2026-10-17 后续四项简化](docs/REFACTOR_PHASE4_20261017.md)。

冗余接口、版本缓存、AFE 请求读取与 SOC 提交的后续简化见 [2026-10-17 三项优化记录](docs/REFACTOR_PHASE5_20261017.md)。

**新员工从 [交接与上手指南](docs/ONBOARDING.md) 开始。** 本 README 优先列出当前系统；固定提交报告与 generated evidence 见末尾历史入口。

AFE原厂审查后的代码修复、新写入规则、回归与尚未签核边界见 [AFE修复记录](docs/AFE_AUDIT_REMEDIATION.md)。

## 上手与日常开发

| 文档 | 何时阅读 |
|---|---|
| [源码导览](docs/CODE_TOUR.md) | 第一次读代码：8 个停靠点、断点与练习 |
| [当前规则](docs/CURRENT_RULES.md) | 先判断"哪条规则现在仍然有效"；区分当前规则与固定提交记录 |
| [交接与上手](docs/ONBOARDING.md) | 第一天：环境、阅读顺序、第一项任务和交接验收 |
| [配置与简单修改](docs/CONFIGURATION_AND_BUILD_GUIDE.md) | 改容量/名称/保护/日志；判断参数是否覆盖设备已有值 |
| [构建与验证](docs/BUILD_AND_TEST.md) | Windows 命令、产物、测试、失败排查、镜像交付 |
| [多产品自动化验证](docs/AUTOMATED_VALIDATION.md) | 一条命令、真实代码场景、配置/行为报告、CI、证据层级和盲区 |
| [代码阅读指南](docs/CODE_READING_GUIDE.md) | 从实际调用链熟悉代码 |
| [全模块审查与简化（2026-10-11）](docs/SIMPLIFICATION_20261011.md) | 对话建议逐项核对、断线/均衡恢复、SOC 电压资格、状态职责及未执行验证 |
| [变量与大函数简化（2026-10-11）](docs/STATE_AND_FUNCTION_SIMPLIFICATION_20261011.md) | 无消费者数组、Config 缓冲、采样/Modbus/OCV 拆分及状态保留依据 |
| [第三轮简化与保留边界](docs/SIMPLIFICATION_20261007.md) · [固定提交证据](docs/SIMPLIFICATION_20261007_EVIDENCE.json) | 主流程、产品差异、删除项、状态与验证口径 |
| [第四轮维护入口简化](docs/SIMPLIFICATION_ROUND4_20261007.md) · [固定提交证据](docs/SIMPLIFICATION_ROUND4_20261007_EVIDENCE.json) | 参数命名、配置头、诊断状态与协议可读性 |
| [启动、诊断与产品命名收口](docs/MAINTENANCE_SIMPLIFICATION_20261007.md) · [固定提交证据](docs/MAINTENANCE_SIMPLIFICATION_20261007_EVIDENCE.json) | CI 证据、发布批准、单一启动/诊断入口及字段命名 |
| [最后一轮启动与电源职责简化](docs/FINAL_MAINTENANCE_SIMPLIFICATION_20261007.md) | Config 单次尝试、CI 选择、D008 固定输入及低功耗职责；本轮未运行测试 |
| [架构与状态所有权](docs/ARCHITECTURE.md) | 查模块责任、数据流和失败路径 |
| [单一源码组织说明](docs/BMS_MONOREPO.md) | 查产品边界、参数格式、移植及导入范围 |
| [根协作规则](AGENTS.md) | 开始修改前确认仓库约束 |

用户明确要求测试、编译或运行验证时，在根目录 PowerShell 按指定范围执行（先按上手指南准备环境）；默认执行条件见[根协作规则](AGENTS.md#本机验证约定)：

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONUTF8 = '1'
python bms_tools/bms.py --all-products sources --check
python bms_tools/bms.py --product d014 env
python bms_tools/bms.py --product d014 link --jobs 4
python bms_tools/bms.py --product d014 resources
```

每条命令确认退出码为 0 再继续。`link` 只生成 ELF/MAP/LST，不生成 BIN。默认产品是 D014，命令仍应显式写产品；获准测试公共代码且未限定更小范围时，按构建指南验证四产品。输出默认在 `%LOCALAPPDATA%/CodexTemp/bms-monorepo-build/`。打开 `bms.code-workspace` 可聚焦业务代码；被隐藏的 SDK 仍参与编译，旧 Eclipse 示例工程不再是本分支构建入口。

## 产品与参数

| 产品/文档 | 重点 |
|---|---|
| [D008 产品 reference](docs/D008_PRODUCT_REFERENCE.md) · [产品规则](bms/products/d008/AGENTS.md) | DVC1124，16S LFP / 20S NMC / 24S LFP，200 µΩ |
| [D008 保护参数配置](docs/D008_PROTECTION_CONFIGURATION.md) · [直接编辑源码](bms/products/d008/d008_protection_config.h) | D008 软件三级保护、独立 AFE 硬件保护与首次烧录 |
| [四产品参数配置与电池类型切换](docs/BATTERY_TYPE_SWITCH.md) | 独立参数文件、OTA 更新选择、App/Windows 密码与整套 LFP/NMC 切换 |
| [D011 产品 reference](docs/D011_PRODUCT_REFERENCE.md) · [产品规则](bms/products/d011/AGENTS.md) | SH3673510，10S，250 µΩ，PB5 fuse 安全低电平 |
| [D013 产品 reference](docs/D013_PRODUCT_REFERENCE.md) · [产品规则](bms/products/d013/AGENTS.md) | 默认 10S/100 µΩ 为代码输入；TS4 MOS 10K-3435 用户确认，含接入影响审查；专属原理图/BOM 尚缺 |
| [D014 产品 reference](docs/D014_PRODUCT_REFERENCE.md) · [产品规则](bms/products/d014/AGENTS.md) | 8S/2000÷3 µΩ，无 heater，TS4 MOS NTC |
| [AFE 原厂资料库](references/afe/README.md) · [开发使用指南](docs/AFE_REFERENCE_GUIDE.md) | 六份原 PDF、型号/版本边界、检索工具、独立审查及已知问题 |
| [OTA 参数更新控制](docs/OTA_PARAMETERS.md) | BIN 自带分组更新、一次性执行、持久均衡许可、旧记录保留、回退和启动失败 |
| [源码配置与固定 OTA 标识](docs/SOURCE_CONFIGURATION_WORKFLOW.md) | 四产品窗口写回源码、固定更新包、提交后构建及追溯 |
| [源码参数与 AFE 位配置窗口](docs/AFE_CONFIGURATION_EDITOR.md) | [双击打开](bms_tools/configure_parameters.cmd)：软硬件保护、精确并联分流器、原厂全寄存器目录与内部版临时调试 |

## 专题与调试

本轮独立分支的架构与交付见 [2026-10-16 深度减法重构](docs/DEEP_SIMPLIFICATION_20261016.md)，
完整条目见 [文件、函数与状态审计](docs/DEEP_SIMPLIFICATION_20261016_AUDIT.json)，
结果和追溯见 [最终验证收据摘要](docs/DEEP_SIMPLIFICATION_20261016_EVIDENCE.json)。

四产品共用两点电流校准、D008 持久零点、协议/Flash兼容和客户步骤见 [电流校准说明](docs/CURRENT_CALIBRATION.md)。本轮仅源码审查，未编译或实板验收。

| 文档 | 内容 |
|---|---|
| [2026-10-10 最小优化记录](docs/OPTIMIZATION_20261010.md) | 独立提交、状态所有权、验证范围与未决实板边界 |
| [软件保护](docs/SOFTWARE_PROTECTION.md) | 三级保护、单位、滤波及阻断 |
| [AFE 硬件保护](docs/AFE_HARDWARE_PROTECTION_V2.md) | 授权、35-word profile、requested/effective、回滚 |
| [SH 恢复证据与采样资格](docs/SH_RECOVERY_AND_FRESHNESS.md) | C+/负载检测互斥、转换完成标志、清除失败与验证边界 |
| [D014 保护与恢复软件闭环](docs/D014_SAFETY_LOOP.md) | 真实参数/存储到 SPI MOS 命令、软件过流物理恢复、重初始化与测试边界 |
| [SOC](docs/SOC.md) | 200 ms 应用时间积分、采样资格、OCV、循环 SOH 与状态保存 |
| [Flash 与持久化](docs/STORAGE.md) | Config/State/Event、SOC 持久化、低功耗及事务 |
| [运行阶段日志](docs/RUNTIME_DEBUG_LOG.md) | 开发日志、只读协议、开关及固定提交资源记录 |
| [SH 低功耗失败处理](docs/SH_LOW_POWER_FAILURE_HANDLING.md) | 开关/低压强制深睡、UART静默30秒Suspend及首帧唤醒 |
| [四产品保护性休眠](docs/LOW_POWER_POLICY.md) | 低压、AFE 异常到期 D008 断电、SH 深睡与唤醒 |
| [完整休眠逻辑与开关强制深睡](docs/SLEEP_LOGIC_REVIEW.md) | 各入口时间、通信门禁、PC4 与 MCU 深睡、唤醒条件及 D008 ACC 拒睡修正 |
| [两档休眠参数与固定过放休眠](docs/SLEEP_CONFIGURATION.md) | 上位机配置电压/延时、Config 保存、过放未恢复固定 3 分钟 |
| [MOS 无人值守监控](docs/MOS_MONITOR.md) | 请求、命令、AFE 状态、原因、开发历史及自动写盘 |
| [充放电 MOS 独立远程许可](docs/MOS_REMOTE_CONTROL.md) | App/Windows 独立控制、RAM 生命周期、协议、AFE 报告与安全边界 |
| [MOS 关断时序优化](docs/MOS_SHUTDOWN_TIMING_OPTIMIZATION.md) | 监控异常的证据边界、配置重复读取、D008 NTC 资格及 SH 辅助采样 |
| [历史：断线检测调研与双后端方案](docs/OPENWIRE_RESEARCH_AND_DESIGN.md) | 原厂检测目的、DVC/SH 差异、当前 MOS 隔离问题及待验证方案 |
| [历史：断线检测与实时监控](docs/OPENWIRE_MONITOR.md) | 检测过程不主动关 MOS、真实故障保护、只读状态和 Windows 自动记录 |
| [实时休眠原因与倒计时](docs/SLEEP_STATUS.md) | 四产品只读状态、剩余时间、阻止原因及 Windows 实时显示 |
| [D014 诊断](docs/D014_DIAGNOSTICS.md) | 温度断线、MOS 阻断、诊断快照 |
| [D014 RS485 发送诊断](docs/D014_RS485_TX_DIAG.md) | 台架模式、DMA/DE 计数和波形定位 |
| [SOC 仿真轨迹说明](tests/soc/traces/README.md) | 回放数据与模型边界 |

本次全仓减法重构见 [Issue #11 执行报告](docs/ISSUE11_SIMPLIFICATION.md) 和 [实际源码、函数及状态导航清单](docs/ISSUE11_SOURCE_INVENTORY.json)。报告固定到所列提交，不替代后续版本或实板验收。
继续精简的计数复用、模块内部调用与验证见 [Issue #11 后续精简](docs/ISSUE11_FOLLOWUP_SIMPLIFICATION.md)。
功能之间的必要影响、参数门禁职责调整及业务策略候选见 [功能耦合审查](docs/FUNCTION_COUPLING_REVIEW.md)。

## 验证与交接缺口

| 文档 | 用途 |
|---|---|
| [资源预算与栈观测](docs/RESOURCE_BUDGET_AND_VALIDATION.md) | ELF/BIN 口径、Flash/SRAM 门禁、实测水位 |
| [GitHub Actions 运维](docs/GITHUB_ACTIONS_RUNBOOK.md) | 当前 CI 矩阵、runner 条件与排查 |
| [硬件验证清单](docs/HARDWARE_VALIDATION.md) | 四产品共同与各自的未关闭项 |
| [项目待办](docs/TODO.md) | 待资料或后续开发项；D013 TS4 MOS 温度已接入，待实板验收 |

2026-10-06 已导入六份 AFE 原始手册及 D008 24S 原理图，见 [资料使用指南](docs/AFE_REFERENCE_GUIDE.md)；D011/D013/D014 原始图纸/BOM仍缺。四产品容量/保护参数及实板验收仍需签核；D008 20S NMC 已关联确认的三元锂开发默认。Ctrl+Shift+B 可选择板型、AFE、串数、类型、容量及 OTA 更新组；BIN 自带选择和一次性标记，无需查询客户编号，见 [配置指南](docs/CONFIGURATION_AND_BUILD_GUIDE.md)。Config/State/Event 内部 schema 4，低频参数统一由 Config 保存；有效旧 CFG2 整套恢复新默认，当前格式损坏保持输出禁止，详见 [存储说明](docs/STORAGE.md)。开发默认不代替产品签核，源码、host、ELF、读回及实板波形分别留证。

<details>
<summary>历史审计与固定提交证据（需要追溯时展开）</summary>

| 文档 | 用途 |
|---|---|
| [AFE 原厂文档与四产品审查](docs/afe-audit/20261006-0aaa8429/README.md) | 2026-10-06 固定提交 `0aaa8429`；原 PDF 对照、当前问题、配置矩阵及验证收据，未实施修复 |
| [文档审核与交接记录](docs/history/DOCUMENTATION_AUDIT.md) | 本次修正/删除范围、验证和剩余资料缺口 |
| [初次迁移验证记录](docs/history/BMS_MONOREPO_VALIDATION.md) | 历史 `fd50730`；不是当前测试成绩 |
| [迁移后整改记录](docs/history/BMS_MONOREPO_REMEDIATION.md) | 固定提交的整改和六配置生产验证 |


- [第二轮简化](docs/history/SIMPLIFICATION_20261006.md) · [存储优化实施记录](docs/history/STORAGE_REFACTOR_20261006.md)
- [四产品配置固定核对表](docs/history/FOUR_PRODUCT_CONFIGURATION_AUDIT.md)
- [第一轮简化](docs/history/BMS_SIMPLIFICATION.md) · [固定证据](docs/history/BMS_SIMPLIFICATION_EVIDENCE.json)
- [自动化验证固定证据](docs/history/AUTOMATED_VALIDATION_EVIDENCE.json)
- [D014 软件闭环固定证据](docs/history/D014_SAFETY_LOOP_EVIDENCE.json)
- [中文注释说明](docs/history/CHINESE_CODE_COMMENTS.md) · [历史检查摘要](docs/history/CHINESE_COMMENT_AUDIT.json)

这些记录只代表各自标注的提交；当前成绩以当前 HEAD 的 CI 和源码树外本轮报告为准。原厂 AFE PDF、原始硬件资料及认证证据没有删除。

</details>

## SDK 附带资料

以下是 Vendor SDK/遗留生成资料，不是 monorepo 的产品配置或构建真源；均保留原件：

- [SDK BLE Release Note](tc_ble_single_sdk-V3.4.2.8_Patch_0001/doc/tc_ble_single_sdk_Release_Note.md)
- [SDK Platform Release Note](tc_ble_single_sdk-V3.4.2.8_Patch_0001/doc/tc_platform_sdk_Release_Note.md)
- [SDK Patch Note](tc_ble_single_sdk-V3.4.2.8_Patch_0001/patch_note.md)
- [Patch 包 Platform Release Note](tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk_v3.4.2.8_Patch/doc/tc_platform_sdk_Release_Note.md)
- [Patch 包说明](tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk_v3.4.2.8_Patch/doc/patch_note.md)
- [历史客户端资产生成说明](tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/docs/generated/README.md)
- [历史寄存器目录 JSON](tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/docs/register_catalog.json)
- [历史协议测试向量 JSON](tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/docs/protocol_test_vectors.json)

新增或删除项目文档时必须同步本 README，并检查链接。当前说明按职责维护，历史结果注明日期/提交；过时阶段报告通过 Git 历史追溯。Git 提交信息和 PR 说明使用中文。

- [2026-10-17 全工程审查与分轮简化](docs/BMS_SIMPLIFICATION_REVIEW_20261017.md)
