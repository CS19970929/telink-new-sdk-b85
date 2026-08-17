# BMS Cppcheck 静态分析与 Excel 报告流水线

## 1. 目的与唯一入口

本流程将固件真实构建配置、Cppcheck、机器可读结果、范围审计、问题分类和
《XXX-BMS 软件静态分析报告.xlsx》自动填表串成一次可重复执行的操作：

```powershell
python bms_tools/bms.py static --jobs 4
```

命令不修改产品源码，也不覆盖原始 Excel 模板。每次执行都会在
`project/tlsr_tc32/B85/825x_ble_sample_cli/static/run_<UTC时间>/` 下生成独立证据目录。

## 2. 配置真实性

静态分析不维护另一套手写的源文件、`-I` 或 `-D` 清单。

1. `bms_tools/source_order.txt` 是真实参与构建及链接顺序的权威输入。
2. `bms.py` 先生成与正式构建相同的 `sources.mk`，再执行
   `make -B -n --no-print-directory -f bms_tools/build.mk ... all`。
3. 从 dry-run 中逐条提取 TC32 的 C 编译命令，形成
   `compile_commands_build.json`，并核对其顺序与 `source_order.txt` 一致。
4. 从这些命令提取实际 Include Path、宏定义、`gnu99`、编译选项和源文件；
   选定分析范围后生成 `compile_commands_analysis.json`。
5. 使用同一条 TC32 编译命令的 `-MM` 模式解析每个分析单元的真实头文件依赖，
   以便判断应用层是否存在漏扫。
6. 通过 `tc32-elf-gcc -dM -E -x c -` 获取编译器预定义宏，仅把条件依赖真正
   使用到的宏传给 Cppcheck。

Cppcheck 使用 `bms_tools/static_analysis/tc32-platform.xml` 描述 TC32 的基本类型和
指针宽度。目标仍按工程原配置记录为 `TLSR8251 + MCU_STARTUP_8258`；这个既有风险
只报告、不静默修改启动宏或链接脚本。

## 3. 范围策略

产品基线固定为 `b9d4d0790cd7f163867872bf6ce7980a71dfee76`。

- 直接分析：所有实际参与编译的 `vendor/ble_sample/*.c` 翻译单元。
- 条件纳入：相对产品基线已修改、且实际参与固件构建的 SDK/第三方 C 文件。
- 随翻译单元解析：上述 C 文件真实引用的项目及 SDK 头文件。
- 不直接分析：相对基线未修改的官方 SDK C 翻译单元；原因会逐项写入范围审计，
  但被应用层引用的 SDK 头文件仍会被 Cppcheck 解析并产生可追溯告警。
- 不适用：启动汇编 `.S`；Cppcheck 是 C/C++ 分析器，汇编文件仍保留在完整构建输入清单。

若产品基线不可读取，流程不会缩小范围，而会保守分析全部实际编译的 C 翻译单元。

## 4. 检查策略与 MISRA 边界

原生 Cppcheck 策略在 `bms_tools/static_analysis/cppcheck.cfg` 中，启用：

- `warning`、`style`、`performance`、`portability`、`unusedFunction`、`information`；
- `inconclusive` 和 exhaustive check level；
- C99 解析器。真实工程仍是 GNU99，GNU 扩展和实际宏/Include Path 由编译数据库提供。

初次接入不启用 suppression，也不接受源码内 inline suppression，因此不会为了降低数字
隐藏问题。

MISRA 检查与 Cppcheck 原生检查严格分开：只有检测到安装目录中的官方 `misra.py` addon
时才运行 MISRA，并且只把 `misra-c2012-X.Y` 形式的结果计为 MISRA Rule。若 addon 缺失，
报告显示“未执行”，普通 Warning/Style/Advisory 不计为 MISRA 违规。本流程不声明完整
MISRA C 合规；即使安装 addon，也只能声明工具实际自动覆盖的规则子集。

## 5. 证据与报告文件

每个 `run_*` 目录至少包括：

- `make_dry_run.log`：真实构建命令展开；
- `compile_commands_build.json`：完整构建 C 编译数据库；
- `compile_commands_analysis.json`：本次 Cppcheck 分析数据库；
- `configuration_check.log`：Cppcheck 正式运行前的配置审计；
- `dependencies.log`：TC32 `-MM` 依赖解析记录；
- `compiler_predefines.txt`、`compiler_predefines_applied.json`：编译器宏证据；
- `cppcheck-native.xml`：Cppcheck 原始机器可读结果；
- `findings.json`、`findings.csv`：去重后的可追溯问题明细；
- `scope_audit.json`、`scope_audit.csv`：构建/分析/排除范围与 SHA-256；
- `cppcheck-checkers-report.txt`：本次可用 checker 信息；
- `misra_capability.json`：MISRA addon 能力与执行状态；
- `static_analysis_report_data.json`：Excel 报告的数据合同；
- `XXX-BMS_软件静态分析报告_已填写.xlsx`：基于原模板生成的新报告；
- `report_previews/verification.json`：关键区域数据和公式错误检查结果。

报告自动填写项目范围、Git Commit/脏工作区状态、工具版本、实际配置、日期、文件数量、
问题分类、状态统计、MISRA 能力边界、Deviation 候选和初步结论。Reviewer、Approver、
责任人、批准日期、最终签核与 Deviation 最终批准保持空白，必须由人工完成。

## 6. 问题分类与 Deviation

问题初始状态只用于分流，不代表审核结论：

- 应用层 `error/warning`：`真实代码问题（待确认）/待整改`；
- 应用层低严重度诊断：`代码质量建议/待评审`；
- 原始 SDK 诊断：`SDK问题/待评审（SDK）`；
- 明确与配置不匹配相关：`配置导致的问题/待修正配置`；
- 只有少量高影响或硬件/供应商约束类 SDK 诊断进入
  `Deviation候选（未批准）`，不会自动批准。

候选记录包含文件、函数、行号、规则/ID、问题描述、不修改原因建议、影响分析建议和
规避措施建议。最终是否接受必须由 Reviewer/Approver 判断。

## 7. 本次基线结果（2026-08-17）

- 真实构建输入：81 个（79 个 C、2 个汇编）。
- 应用层直接分析：18 个 C 翻译单元；修改且参与构建的 SDK C 文件：0 个。
- 经 TC32 `-MM` 核对的依赖头文件：170 个；覆盖缺口：0。
- Cppcheck：2.21.0。
- 原生问题：352 个去重问题，等于 352 次原始发生；9 warning、343 style。
- 初始分类：9 个真实代码问题、117 个代码质量建议、226 个 SDK 问题。
- MISRA：未执行，因为当前 Cppcheck 安装不含 MISRA addon；MISRA 违规数量不填 0，
  而是明确显示“未执行”。
- Deviation 候选：2 个，均为未修改 Telink SDK 头文件中的候选，状态均为未批准。

本次没有修改业务逻辑来消除告警；应优先人工复核应用层的 9 个 warning，再按风险和
可达性整理 style 与 SDK 问题。

## 8. 环境与故障处理

默认 Cppcheck 路径为 `C:\Program Files\cppcheck\cppcheck.exe`。Excel 填表使用桌面环境
自带的 `artifact_tool_v2` Python 运行时，工具会自动发现；需要显式覆盖时设置：

```powershell
$env:BMS_ARTIFACT_PYTHON = 'C:\path\to\python.exe'
python bms_tools/bms.py static --jobs 4
```

填表在隔离的系统临时目录内完成，结束后只复制工作簿和校验记录到证据目录；脚本不在
构建目录创建包目录联接，因而不会干扰 `rebuild` 的 clean，也不会让交互式 artifact
生命周期清理 Cppcheck 证据。
若只需要分析证据而暂不生成 Excel，可使用 `--no-report`；自定义模板可使用
`--report-template <path>`。
