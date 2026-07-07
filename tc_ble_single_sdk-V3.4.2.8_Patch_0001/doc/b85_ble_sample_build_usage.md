# B85 ble_sample 编译入口说明

本文档说明当前分支迁移自 `renzheng` 分支的通用编译入口。

## 命令行编译

从仓库根目录执行完整编译：

```powershell
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1"
```

增量编译：

```powershell
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1" -SkipClean
```

只执行 make，不手动重新生成 bin：

```powershell
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1" -NoPostBuild
```

## VSCode 任务

仓库根目录包含 `.vscode/tasks.json` 后，在 VSCode 中使用：

1. `Ctrl + Shift + P`
2. `Tasks: Run Task`
3. 选择 `build: B85 ble_sample full` 或 `build: B85 ble_sample incremental`

## 环境要求

- TC32 工具链默认位置：`C:\TelinkSDK\opt\tc32\bin`
- 可通过 `TELINK_TC32_TOOLCHAIN` 环境变量或 `-ToolchainDir` 指定其他工具链目录。
- `make` 需要能从 `PATH` 找到；如果 `make.exe` 位于 TC32 工具链目录，脚本会先把该目录加入 `PATH`。
- 如果工具链安装在其他目录，使用 `-ToolchainDir` 指定。
- B85 工程需要 `project/tlsr_tc32/B85/825x_ble_sample/boot/B85/subdir.mk` 纳入版本控制，否则启动汇编对象不会进入链接，可能触发 `boot.link` 中的 `_rstored_` 未定义错误。

示例：

```powershell
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1" -ToolchainDir "D:\Tools\tc32\bin"
```

## 分支复用方式

- 新分支从包含本次提交的分支创建时，会继承 `AGENTS.md`、`.vscode/tasks.json` 和构建脚本。
- 旧分支需要合并或 cherry-pick 本次提交，才能获得同样的命令行与 VSCode task 编译入口。
- 如果旧分支的 B85 工程路径或输出文件名不同，需要同步调整 `build_b85_ble_sample.ps1` 和 `.vscode/tasks.json`。
