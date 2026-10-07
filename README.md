# BMS monorepo：D008 / D011 / D013 / D014

四产品固件的主开发分支是 `codex-bms-monorepo`。公共代码只维护一份，产品通过编译配置和 `sources.txt` 选择硬件输入与 AFE 后端。Windows 上位机仍在 `feature/windows-afe-hw-protection-editor-v2` 分支的 `bms-tool-windows/` 维护。

当前专题与阅读顺序见 [文档导航](docs/README.md)。

**新员工从 [交接与上手指南](docs/ONBOARDING.md) 开始。** 本 README 优先列出当前系统；固定提交报告与 generated evidence 见末尾历史入口。

AFE原厂审查后的代码修复、新写入规则、回归与尚未签核边界见 [AFE修复记录](docs/AFE_AUDIT_REMEDIATION.md)。

## 上手与日常开发

| 文档 | 何时阅读 |
|---|---|
| [交接与上手](docs/ONBOARDING.md) | 第一天：环境、阅读顺序、第一项任务和交接验收 |
| [配置与简单修改](docs/CONFIGURATION_AND_BUILD_GUIDE.md) | 改容量/名称/保护/日志；判断参数是否覆盖设备已有值 |
| [构建与验证](docs/BUILD_AND_TEST.md) | Windows 命令、产物、测试、失败排查、镜像交付 |
| [多产品自动化验证](docs/AUTOMATED_VALIDATION.md) | 一条命令、真实代码场景、配置/行为报告、CI、证据层级和盲区 |
| [代码阅读指南](docs/CODE_READING_GUIDE.md) | 从实际调用链熟悉代码 |
| [第三轮简化与保留边界](docs/SIMPLIFICATION_20261007.md) · [固定提交证据](docs/SIMPLIFICATION_20261007_EVIDENCE.json) | 主流程、产品差异、删除项、状态与验证口径 |
| [第四轮维护入口简化](docs/SIMPLIFICATION_ROUND4_20261007.md) · [固定提交证据](docs/SIMPLIFICATION_ROUND4_20261007_EVIDENCE.json) | 参数命名、配置头、诊断状态与协议可读性 |
| [启动、诊断与产品命名收口](docs/MAINTENANCE_SIMPLIFICATION_20261007.md) | CI 证据、发布批准、单一启动/诊断入口及字段命名 |
| [架构与状态所有权](docs/ARCHITECTURE.md) | 查模块责任、数据流和失败路径 |
| [单一源码组织说明](docs/BMS_MONOREPO.md) | 查产品边界、参数格式、移植及导入范围 |
| [根协作规则](AGENTS.md) | 开始修改前确认仓库约束 |

在根目录 PowerShell 执行（先按上手指南准备环境）：

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONUTF8 = '1'
python bms_tools/bms.py --all-products sources --check
python bms_tools/bms.py --product d014 env
python bms_tools/bms.py --product d014 link --jobs 4
python bms_tools/bms.py --product d014 resources
```

每条命令确认退出码为 0 再继续。`link` 只生成 ELF/MAP/LST，不生成 BIN。默认产品是 D014，日常命令仍应显式写产品；公共代码修改按构建指南验证四产品。输出默认在 `%LOCALAPPDATA%/CodexTemp/bms-monorepo-build/`。打开 `bms.code-workspace` 可聚焦业务代码；被隐藏的 SDK 仍参与编译，旧 Eclipse 示例工程不再是本分支构建入口。

## 产品与参数

| 产品/文档 | 重点 |
|---|---|
| [D008 产品 reference](docs/D008_PRODUCT_REFERENCE.md) · [产品规则](bms/products/d008/AGENTS.md) | DVC1124，16S LFP / 20S NMC / 24S LFP，200 µΩ |
| [D011 产品 reference](docs/D011_PRODUCT_REFERENCE.md) · [产品规则](bms/products/d011/AGENTS.md) | SH3673510，10S，250 µΩ，PB5 fuse 安全低电平 |
| [D013 产品 reference](docs/D013_PRODUCT_REFERENCE.md) · [产品规则](bms/products/d013/AGENTS.md) | 4S/100 µΩ 为代码输入；专属原理图/BOM 尚缺 |
| [D014 产品 reference](docs/D014_PRODUCT_REFERENCE.md) · [产品规则](bms/products/d014/AGENTS.md) | 8S/667 µΩ，无 heater，TS4 MOS NTC |
| [AFE 原厂资料库](references/afe/README.md) · [开发使用指南](docs/AFE_REFERENCE_GUIDE.md) | 六份原 PDF、型号/版本边界、检索工具、独立审查及已知问题 |
| [OTA 参数更新控制](docs/OTA_PARAMETERS.md) | 八类编号及一个保留槽、保留/重置、回退和启动失败 |

## 专题与调试

| 文档 | 内容 |
|---|---|
| [软件保护](docs/SOFTWARE_PROTECTION.md) | 三级保护、单位、滤波及阻断 |
| [AFE 硬件保护](docs/AFE_HARDWARE_PROTECTION_V2.md) | 授权、35-word profile、requested/effective、回滚 |
| [SH 恢复证据与采样资格](docs/SH_RECOVERY_AND_FRESHNESS.md) | C+/负载检测互斥、转换完成标志、清除失败与验证边界 |
| [D014 保护与恢复软件闭环](docs/D014_SAFETY_LOOP.md) | 真实参数/存储到 SPI MOS 命令、软件过流物理恢复、重初始化与测试边界 |
| [SOC](docs/SOC.md) | 真实样本时基、OCV、循环 SOH 与状态保存 |
| [Flash 与持久化](docs/STORAGE.md) | Config/State/Event、SOC 持久化、低功耗及事务 |
| [运行阶段日志](docs/RUNTIME_DEBUG_LOG.md) | 开发日志、只读协议、开关及固定提交资源记录 |
| [SH 低功耗失败处理](docs/SH_LOW_POWER_FAILURE_HANDLING.md) | 休眠/唤醒、通信与采样门禁 |
| [D014 诊断](docs/D014_DIAGNOSTICS.md) | 温度断线、MOS 阻断、诊断快照 |
| [D014 RS485 发送诊断](docs/D014_RS485_TX_DIAG.md) | 台架模式、DMA/DE 计数和波形定位 |
| [SOC 仿真轨迹说明](tests/soc/traces/README.md) | 回放数据与模型边界 |

## 验证与交接缺口

| 文档 | 用途 |
|---|---|
| [资源预算与栈观测](docs/RESOURCE_BUDGET_AND_VALIDATION.md) | ELF/BIN 口径、Flash/SRAM 门禁、实测水位 |
| [GitHub Actions 运维](docs/GITHUB_ACTIONS_RUNBOOK.md) | 当前 CI 矩阵、runner 条件与排查 |
| [硬件验证清单](docs/HARDWARE_VALIDATION.md) | 四产品共同与各自的未关闭项 |

2026-10-06 已导入六份 AFE 原始手册及 D008 24S 原理图，见 [资料使用指南](docs/AFE_REFERENCE_GUIDE.md)；D011/D013/D014 原始图纸/BOM仍缺，本次未重新核验这些原件。产品 reference 中相应历史结论保留其证据边界。四产品容量/保护参数及实板验收仍需签核；D008 20S NMC 选择不会自动生成 NMC 保护值。开发板采用 Config/State/Event 内部 journal schema 3，不迁移旧开发布局；CFG2 payload 和通信版本值保持，同格式按更新编号处理。老化模式及事件清空命令已删除，详见 Flash 与持久化。源码、host、ELF、设备读回与实板波形分别留证。

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
