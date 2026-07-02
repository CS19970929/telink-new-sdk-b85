# BMS 软件静态分析使用与报告填写指南

## 1. 适用范围

本文用于说明 BMS 工程后续如何执行 cppcheck 静态分析、如何读取输出结果，以及如何填写《XXX-BMS 软件静态分析报告.xlsx》模板。

当前已配置的分析对象：

- 工程：`tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample`
- 源码范围：`tc_ble_single_sdk/vendor/ble_sample/*.c`
- 关联配置：B85 编译宏、B85 include 路径、SSR-MCU 自检契约检查
- 工具：Cppcheck 2.21.0

## 2. VSCode 中如何使用

在 VSCode 打开仓库根目录后：

1. 按 `Ctrl + Shift + P`
2. 输入并选择 `Tasks: Run Task`
3. 选择以下任务之一：

| 任务名 | 用途 |
| --- | --- |
| `static-analysis: BMS cppcheck report` | 生成 Markdown 报告和 XML 原始结果，适合归档和提交评审 |
| `static-analysis: BMS cppcheck problems` | 将 cppcheck 问题输出为 VSCode Problems 面板可识别的格式，适合开发时定位代码 |

VSCode 工作区已配置：

- `.vscode/tasks.json`
- `.vscode/settings.json`

`settings.json` 会把 `C:\Program Files\Cppcheck` 加入 VSCode 终端 PATH，因此 VSCode 终端里可以直接执行 `cppcheck --version`。

## 3. 命令行中如何使用

也可以在仓库根目录直接运行：

```powershell
python "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\run_bms_static_analysis.py"
```

输出 VSCode Problems 格式：

```powershell
python "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\run_bms_static_analysis.py" --vscode-problems
```

## 4. 输出文件

每次执行 `static-analysis: BMS cppcheck report` 后生成：

| 文件 | 说明 |
| --- | --- |
| `doc/static_analysis_report_YYYYMMDD.md` | 人可读静态分析报告，填写 Excel 时主要引用这个文件 |
| `build/static_analysis/cppcheck_bms_ble_sample.xml` | cppcheck 原始 XML 结果，可作为审查证据附件 |
| `build/static_analysis/cppcheck_bms_ble_sample.stderr.txt` | cppcheck 原始 stderr 输出 |
| `build/static_analysis/cppcheck_bms_ble_sample.stdout.txt` | cppcheck 原始 stdout 输出 |

## 5. 报告结果如何判定

建议按以下规则使用：

| 项目 | 建议判定 |
| --- | --- |
| SSR-MCU 契约检查全部 PASS | SSR-MCU-001/002 静态接入检查通过 |
| cppcheck `error = 0` | 静态分析可判为通过 |
| cppcheck `error > 0` | 应判为不通过或有条件通过，必须修复或填写 Deviation |
| cppcheck `warning > 0` | 优先评审，能修复则修复；不能修复需说明原因 |
| `style` / `information` | 一般不阻断，但应在报告中统计并说明 |

当前 2026-07-02 结果：

- BMS 范围问题总数：103
- error：1
- warning：9
- style：83
- information：10
- SSR-MCU 契约检查：全部 PASS

当前不建议直接写“完全通过”，因为仍有 1 条 `error`：

- `vendor/ble_sample/sif_send.c:919`
- 规则：`arrayIndexOutOfBounds`
- 描述：`arrVoltage[5]` 被按索引 `9` 访问，cppcheck 判定数组越界。

## 6. Excel 模板怎么填

模板文件：

`C:\Users\Administrator\xwechat_files\wxid_t7amffgm31vj22_db58\msg\file\2026-06\功能安全 - 副本\13849模板\XXX-BMS 软件静态分析报告.xlsx`

建议另存为实际项目文件，例如：

`XXX-BMS 软件静态分析报告_20260702.xlsx`

### 6.1 Cover

| 模板字段 | 建议填写 |
| --- | --- |
| 文件编号 / Document ID | 公司文档编号，例如 `BMS-SW-SA-001`，按公司规则填写 |
| 版本 / Versions | 文档版本，例如 `V1.00` |
| 项目名称 / Project Name | `XXX-BMS` 或实际 BMS 项目名称 |
| 项目ID / Project ID | 公司项目编号，没有则填 `N/A` |
| 编制 / Author | 编写人姓名 |
| 审核 / Reviewer | 审核人姓名 |
| 批准 / Approver | 批准人姓名 |
| 日期 / Date | 报告日期，例如 `2026-07-02` |
| 版本 / Version | `V1.00`，后续更新递增 |

### 6.2 ChangeHistory

首次填写一行：

| 字段 | 建议填写 |
| --- | --- |
| 版本 | `V1.00` |
| 状态 | `Draft` 或 `Released` |
| 编制人 | 编写人 |
| 编制日期 | `2026-07-02` |
| 审核人 | 审核人 |
| 审核日期 | 审核完成日期，未审核可留空 |
| 内容描述 | `首次建立 BMS 软件静态分析报告，基于 cppcheck 2.21.0 对 ble_sample BMS 源码进行静态分析，并补充 SSR-MCU-001/002 契约检查。` |

后续每次重新分析或修复问题后，新增一行，例如：

| 版本 | 状态 | 内容描述 |
| --- | --- | --- |
| V1.01 | Draft | 修复 cppcheck error 项并更新静态分析结果 |

### 6.3 General

#### 1. 范围

在“静态代码检查的 Source Code 文件”表格中填写参与分析的源码文件。建议至少列出 `vendor/ble_sample` 下所有 `.c` 文件。

示例：

| 文件名称 | 版本 |
| --- | --- |
| `vendor/ble_sample/app.c` | Git `e3913d5` |
| `vendor/ble_sample/main.c` | Git `e3913d5` |
| `vendor/ble_sample/bms_mcu_selftest.c` | Git `e3913d5` |
| `vendor/ble_sample/sif_send.c` | Git `e3913d5` |
| `vendor/ble_sample/sh367309_datadeal.c` | Git `e3913d5` |

如果表格行不够，可以增加行，或写：

`详见附件 doc/static_analysis_report_20260702.md 中“命令记录”的源码列表。`

#### 2. 工具

| 工具名称 | 工具版本 |
| --- | --- |
| Cppcheck | 2.21.0 |
| Python 静态分析包装脚本 | `run_bms_static_analysis.py`，Git `e3913d5` 后新增 |

模板里原有 `QAC` 示例可删除或改为 `Cppcheck`。如果公司要求保留 QAC，而实际未使用，应填写 `未使用 / N/A`，不要写虚假版本。

#### 3. 工具设定

填写本次 cppcheck 配置摘要：

```text
使用 Cppcheck 2.21.0，对 B85 825x_ble_sample 工程的 vendor/ble_sample/*.c 执行静态分析。
启用规则集：warning, style, performance, portability, information。
语言标准：C99。
平台近似配置：unix32。
工程宏：__PROJECT_8258_BLE_SAMPLE__=1，CHIP_TYPE=CHIP_TYPE_825x。
include 路径：project/tlsr_tc32/B85，SDK 根目录，vendor/common，common，drivers/B85。
配置限制：--max-configs=1，--check-level=normal，用于固定 B85 工程配置并避免 SDK 条件编译枚举超时。
```

附件建议：

- `doc/static_analysis_report_20260702.md`
- `build/static_analysis/cppcheck_bms_ble_sample.xml`
- `.vscode/tasks.json`
- `tc_ble_single_sdk/script/run_bms_static_analysis.py`

#### 4. 静态代码分析结果汇总

模板字段和当前建议填写：

| 模板字段 | 当前填写 |
| --- | --- |
| 违反代码指标的数量 | `103` |
| 违反MISRA-C的数量 | `N/A` 或 `未执行 MISRA-C 专项检查` |
| 接受的Deviation数量 | 若暂不修复 1 个 error 和 9 个 warning，则至少填写对应 Deviation 数量；否则填 `0` |

注意：当前 cppcheck 配置不是 MISRA-C 专项检查。如果公司要求 MISRA-C，需另行启用 cppcheck MISRA addon 或使用 QAC/PC-lint 等工具。

#### 5. 静态代码分析结论

当前建议不要直接填“通过”，建议填：

| 字段 | 当前建议 |
| --- | --- |
| 判定结果 | `有条件通过` 或 `不通过，待修复` |
| 说明 | `SSR-MCU-001/002 自检契约检查全部通过；cppcheck 在 BMS 范围内发现 103 条问题，其中 error 1 条、warning 9 条、style 83 条、information 10 条。error 项为 sif_send.c:919 数组越界告警，需修复或经 Deviation 批准后关闭。` |

修复 error 后重新运行，如果 `error = 0`，可改为：

`通过。SSR-MCU-001/002 契约检查全部通过；cppcheck 未发现 error 级问题，剩余 style/information 已评审，不影响功能安全目标。`

### 6.4 公司软件审核单检查

这个 sheet 只有标题，需要按公司内部编码规则补充检查项。建议填以下内容：

| 检查项 | 检查方法 | 结果 | 证据 |
| --- | --- | --- | --- |
| SSR-MCU-001 启动期自检是否位于 C 初始化前 | 自定义契约检查 | PASS | `static_analysis_report_20260702.md` |
| SSR-MCU-001 启动期自检宏是否启用 | 自定义契约检查 | PASS | `boot/B85/subdir.mk` |
| SSR-MCU-002 主循环是否每轮调用运行期自检 | 自定义契约检查 | PASS | `main.c` |
| 运行期自检顺序是否符合需求 | 自定义契约检查 | PASS | `bms_mcu_selftest.c` |
| fail-safe 是否不可返回并关断输出 | 自定义契约检查 | PASS | `bms_mcu_selftest.c` |
| 是否存在 cppcheck error | cppcheck | FAIL | `sif_send.c:919` |

### 6.5 MISRA-Rules

当前未执行正式 MISRA-C 检查，建议如实填写：

```text
本次静态分析使用 cppcheck 通用规则集，未启用 MISRA-C 专项规则检查。
MISRA-C 检查结果不适用，后续如项目认证要求 MISRA-C，应使用公司指定工具或 cppcheck MISRA addon 另行输出 MISRA-C 规则报告。
```

如果评审方要求这个 sheet 不能空，可以列一行：

| Rule | Result | Description |
| --- | --- | --- |
| N/A | N/A | 本次未执行 MISRA-C 专项检查 |

### 6.6 DevitionList

只有当问题暂不修复时才填写。当前至少要处理 `sif_send.c:919` 这一条 error：

| 字段 | 当前示例 |
| --- | --- |
| 文件 | `vendor/ble_sample/sif_send.c` |
| 函数 | 根据代码定位填写；需要打开 `sif_send.c:919` 确认函数名 |
| 违反的规则号 | `cppcheck: arrayIndexOutOfBounds` |
| 不修正的原因 | 不建议直接豁免；应优先修复。若确认是误报，需写明数组真实长度、索引边界来源和证明依据 |
| 不修正的影响 | 若为真实问题，可能导致越界读写、数据上报错误或内存破坏 |
| 规避措施 | 修复数组长度/索引边界；或增加运行时边界检查；或经代码审查确认误报并附证据 |
| 责任工程师 | 对应模块负责人 |
| 结论 | `待修复` / `批准偏离` / `不批准` |
| 批准人 | 批准人 |
| 日期 | 批准日期 |

建议策略：

- `error`：优先修复，不建议 Deviation。
- `warning`：逐项评审；真实问题修复，确认误报才 Deviation。
- `style/information`：可批量说明“不影响安全目标”，但需要公司流程认可。

## 7. 每次发版前推荐流程

1. 更新代码后，先编译或至少运行源级测试。
2. 在 VSCode 执行 `static-analysis: BMS cppcheck report`。
3. 打开 `doc/static_analysis_report_YYYYMMDD.md`。
4. 确认 SSR-MCU 契约检查全部 PASS。
5. 处理 `error` 和 `warning`。
6. 重新运行静态分析。
7. 将最终 Markdown 报告、XML 原始结果、Excel 报告一起归档。
8. 在 Excel 的 ChangeHistory 中记录本次分析版本和结论。

## 8. 当前下一步建议

当前最优先修复：

1. `sif_send.c:919` 的数组越界 error。
2. `modbus_rtu.c:323` 的重复条件 warning。
3. `sh367309_datadeal.c` 中多处 `constStatement` / 可疑逗号表达式 warning。

修复后再次执行：

```powershell
python "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\run_bms_static_analysis.py"
```

当 `error = 0` 且 SSR-MCU 契约检查全部 PASS 后，Excel 的“判定结果”才建议填写“通过”。
