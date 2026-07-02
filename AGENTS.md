# AGENTS.md

## 通用要求

- 全部使用中文回答。
- 进行较大的代码变更时，需要同步生成或更新说明文档，并自动创建描述清晰的 git 提交。
- 修改代码前先阅读相关工程结构和现有实现，优先沿用当前 SDK 的目录、命名和构建方式。
- 不要回滚或提交与当前任务无关的既有修改、删除文件、临时文件或构建产物。

## 当前工程

- BMS 应用目录：`tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample`
- B85 make 工程：`tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample`
- B85 启动文件：`tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/boot/B85/cstartup_825x.S`
- 静态分析脚本：`tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/script/run_bms_static_analysis.py`

## 编译

TC32 工具链目录：

```powershell
C:\TelinkSDK\opt\tc32\bin
```

B85 ble_sample 编译命令：

```powershell
$env:PATH = "C:\TelinkSDK\opt\tc32\bin;" + $env:PATH
make all -j4
```

工作目录：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample
```

注意：

- Windows 下 `post-build` 的 `tl_check_fw.sh` 可能出现 `Error 193`，该步骤在 makefile 中被忽略。
- 判断编译结果时，以 `tc_ble_single_sdk_B85.elf` 和 `825x_ble_sample.lst` 是否生成/更新为主要依据。
- 不要默认提交 `.o`、`.elf`、`.lst`、`.bin` 等构建产物，除非用户明确要求。

## 静态分析

VSCode 中执行：

1. `Ctrl + Shift + P`
2. `Tasks: Run Task`
3. 选择 `static-analysis: BMS cppcheck report`

命令行执行：

```powershell
python "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\run_bms_static_analysis.py"
```

报告输出：

- `tc_ble_single_sdk-V3.4.2.8_Patch_0001/doc/static_analysis_report_YYYYMMDD.md`
- `tc_ble_single_sdk-V3.4.2.8_Patch_0001/build/static_analysis/cppcheck_bms_ble_sample.xml`

## MCU 自检相关要求

- SSR-MCU-001：启动期自检必须在 C 初始化数据阶段之前执行。
- SSR-MCU-002：运行期自检必须在 `main` 死循环每轮执行。
- 修改 MCU 自检逻辑后，需要至少执行：

```powershell
python "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\vendor\ble_sample\tests_flash_quick_check.py"
```

如果 TC32 工具链可用，还需要执行 B85 工程编译。

## 提交原则

- 每次提交只包含当前任务相关文件。
- 提交信息用中文说明变更目的和关键内容。
- 若工作区已有无关修改或删除，保持未暂存状态，不要擅自恢复或提交。
