# BMS monorepo：D008 / D011 / D013 / D014

四产品固件的主开发分支是 `codex-bms-monorepo`。公共代码只维护一份，产品通过编译配置和 `sources.txt` 选择硬件输入与 AFE 后端。Windows 上位机仍在 `feature/windows-afe-hw-protection-editor-v2` 分支的 `bms-tool-windows/` 维护。

**新员工从 [交接与上手指南](docs/ONBOARDING.md) 开始。** 本 README 是全部项目文档的总入口。

## 上手与日常开发

| 文档 | 何时阅读 |
|---|---|
| [交接与上手](docs/ONBOARDING.md) | 第一天：环境、阅读顺序、第一项任务和交接验收 |
| [配置与简单修改](docs/CONFIGURATION_AND_BUILD_GUIDE.md) | 改容量/名称/保护/日志；判断参数是否覆盖设备已有值 |
| [构建与验证](docs/BUILD_AND_TEST.md) | Windows 命令、产物、测试、失败排查、镜像交付 |
| [代码阅读指南](docs/CODE_READING_GUIDE.md) | 从实际调用链熟悉代码 |
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
| [四产品配置核对表](docs/FOUR_PRODUCT_CONFIGURATION_AUDIT.md) | 固定基线的完整默认字段、量化结果和替换限制 |
| [OTA 参数更新控制](docs/OTA_PARAMETERS.md) | 九类编号、保留/重置、回退和启动失败 |

## 专题与调试

| 文档 | 内容 |
|---|---|
| [软件保护](docs/SOFTWARE_PROTECTION.md) | 三级保护、单位、滤波及阻断 |
| [AFE 硬件保护](docs/AFE_HARDWARE_PROTECTION_V2.md) | 授权、35-word profile、requested/effective、回滚 |
| [SOC](docs/SOC.md) | 真实样本时基、OCV、学习与状态保存 |
| [Flash 与持久化](docs/STORAGE.md) | Config/State/Event、Factory 预留、地址和事务 |
| [运行阶段日志](docs/RUNTIME_DEBUG_LOG.md) | 开发日志、只读协议、开关及固定提交资源记录 |
| [SH 低功耗失败处理](docs/SH_LOW_POWER_FAILURE_HANDLING.md) | 休眠/唤醒、通信与采样门禁 |
| [D014 诊断](docs/D014_DIAGNOSTICS.md) | 温度断线、MOS 阻断、诊断快照 |
| [D014 RS485 发送诊断](docs/D014_RS485_TX_DIAG.md) | 台架模式、DMA/DE 计数和波形定位 |
| [SOC 仿真轨迹说明](tests/soc/traces/README.md) | 回放数据与模型边界 |

## 验证、交接缺口与历史证据

| 文档 | 用途 |
|---|---|
| [资源预算与栈观测](docs/RESOURCE_BUDGET_AND_VALIDATION.md) | ELF/BIN 口径、Flash/SRAM 门禁、实测水位 |
| [GitHub Actions 运维](docs/GITHUB_ACTIONS_RUNBOOK.md) | 当前 CI 矩阵、runner 条件与排查 |
| [硬件验证清单](docs/HARDWARE_VALIDATION.md) | 四产品共同与各自的未关闭项 |
| [文档审核与交接记录](docs/DOCUMENTATION_AUDIT.md) | 本次修正/删除范围、验证和剩余资料缺口 |
| [初次迁移验证记录](docs/BMS_MONOREPO_VALIDATION.md) | 历史 `fd50730`；不是当前测试成绩 |
| [迁移后整改记录](docs/BMS_MONOREPO_REMEDIATION.md) | 固定提交的整改和六配置生产验证 |

硬件原件未随当前仓库交接，产品 reference 的原理图结论来自历史记录，本次没有重新核验原件。四产品容量/保护参数及实板验收仍需签核；D008 20S NMC 选择不会自动生成 NMC 保护值。开发板采用 CFG2/State/Event schema 2，不迁移旧格式，同格式按更新编号处理。源码、host、ELF、设备读回与实板波形分别留证。

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
