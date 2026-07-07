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
- B85 编译脚本：`tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/script/build_b85_ble_sample.ps1`
- 静态分析脚本：`tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/script/run_bms_static_analysis.py`

## 推荐编译方式

优先使用封装脚本，避免手动进入错误目录或漏掉 bin 后处理。

完整 clean + build + bin 生成 + 固件检查：

```powershell
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1"
```

不 clean，增量编译：

```powershell
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1" -SkipClean
```

只生成 ELF/LST，不手动生成 bin：

```powershell
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1" -NoPostBuild
```

如果 TC32 工具链不在默认位置，可以显式传入：

```powershell
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1" -ToolchainDir "C:\TelinkSDK\opt\tc32\bin"
```

## 手动编译命令

TC32 工具链目录优先使用 Telink IDE 自带版本：

```powershell
C:\TelinkIoTStudio\opt\tc32\bin
```

旧 SDK 工具链目录 `C:\TelinkSDK\opt\tc32\bin` 可作为备用，但该目录中的旧版链接器可能无法正确处理当前 B85 `boot.link`。

从仓库根目录执行完整编译：

```powershell
$env:PATH = "C:\TelinkIoTStudio\opt\tc32\bin;" + $env:PATH
cd "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\project\tlsr_tc32\B85\825x_ble_sample"
make clean
make all -j4
tc32-elf-objcopy -v -O binary tc_ble_single_sdk_B85.elf 825x_ble_sample.bin
..\..\..\..\script\tl_check_fw\tl_check_fw2.exe 825x_ble_sample.bin
tc32-elf-size -t tc_ble_single_sdk_B85.elf
```

也可以不切目录：

```powershell
$env:PATH = "C:\TelinkIoTStudio\opt\tc32\bin;" + $env:PATH
make -C "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\project\tlsr_tc32\B85\825x_ble_sample" clean
make -C "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\project\tlsr_tc32\B85\825x_ble_sample" all -j4
```

如果改了 `.h` 文件或构建依赖不确定，必须先 `make clean` 再 `make all`。

## 编译产物

成功后重点确认：

- `tc_ble_single_sdk_B85.elf`
- `825x_ble_sample.lst`
- `825x_ble_sample.bin`

Windows 下 `make all` 的 `post-build` 会直接执行 `tl_check_fw.sh`，可能报 `Error 193`，该错误在 makefile 中被忽略。为确保 `.bin` 是本次新生成的，需要继续执行：

```powershell
tc32-elf-objcopy -v -O binary tc_ble_single_sdk_B85.elf 825x_ble_sample.bin
..\..\..\..\script\tl_check_fw\tl_check_fw2.exe 825x_ble_sample.bin
```

不要默认提交 `.o`、`.elf`、`.lst`、`.bin` 等构建产物，除非用户明确要求。

## VSCode 任务

VSCode 中执行：

1. `Ctrl + Shift + P`
2. `Tasks: Run Task`
3. 选择任务：
   - `build: B85 ble_sample full`
   - `build: B85 ble_sample incremental`
   - `static-analysis: BMS cppcheck report`
   - `static-analysis: BMS cppcheck problems`

## 静态分析

命令行执行：

```powershell
python "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\run_bms_static_analysis.py"
```

报告输出：

- `tc_ble_single_sdk-V3.4.2.8_Patch_0001/doc/static_analysis_report_YYYYMMDD.md`
- `tc_ble_single_sdk-V3.4.2.8_Patch_0001/build/static_analysis/cppcheck_bms_ble_sample.xml`

## 分支复用

- 新分支只要从包含本文件、`.vscode/tasks.json` 和 `build_b85_ble_sample.ps1` 的分支创建，就会自动继承命令行和 VSCode 编译入口。
- 旧分支需要合并或 cherry-pick 对应提交，或者手动复制同一组文件后，才能使用相同入口。
- `AGENTS.md` 只负责约定和说明；真正让 VSCode task 可执行的是 `.vscode/tasks.json`，真正执行编译的是 SDK 内的 PowerShell 脚本。

## 提交原则

- 每次提交只包含当前任务相关文件。
- 提交信息用中文说明变更目的和关键内容。
- 若工作区已有无关修改或删除，保持未暂存状态，不要擅自恢复或提交。
