# subdir.mk 构建问题复盘与统一开发环境建议

本文记录本次命令行编译失败的原因、已采取的修复、后续新增源文件时的处理规则，以及跨平台、跨 MCU 统一开发环境的可行方案。

## 背景

当前 B85 工程路径：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample
```

当前推荐命令行入口：

```powershell
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1"
```

本次迁移目标是让当前分支继承 `renzheng` 分支中非 MCU 自检相关的命令行编译、VSCode task、环境说明和辅助脚本能力。

## 现象

命令行构建时曾出现链接错误：

```text
../boot.link:131: undefined symbol `_rstored_' referenced in expression
make: *** [makefile:45: tc_ble_single_sdk_B85.elf] Error 1
```

同一工程在 Telink IDE 中可以编译成功，因此最初容易误判为命令行工具链路径或 Telink IDE / Telink IoT Studio 工具链差异导致。

## 根因

真正根因不是工具链路径，而是构建清单漏迁移。

主 `makefile` 通过 `-include` 引入各目录的 `subdir.mk`：

```makefile
-include vendor/common/subdir.mk
-include vendor/ble_sample/subdir.mk
-include drivers/B85/subdir.mk
-include common/subdir.mk
-include boot/B85/subdir.mk
```

每个 `subdir.mk` 负责把该目录贡献的源文件加入构建变量，例如：

```makefile
C_SRCS += ...
S_UPPER_SRCS += ...
OBJS += ...
```

最终链接命令只链接 `$(OBJS)`。也就是说，源码文件即使存在于仓库中，只要没有进入对应 `subdir.mk` 的 `OBJS`，命令行 `make` 就不会编译和链接它。

本次缺失的是：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample/boot/B85/subdir.mk
```

该文件负责把启动汇编文件加入构建：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/boot/B85/cstartup_825x.S
```

缺少它时，`cstartup_825x.o` 不会进入链接，最终可能在 `boot.link` 中暴露为 `_rstored_` 未定义。

## 为什么 Telink IDE 能成功

`subdir.mk` 是 Telink IDE 基于 Eclipse/CDT Managed Build 机制生成的工程片段，文件头通常会出现：

```text
Automatically-generated file. Do not edit!
```

Telink IDE 内部知道工程包含哪些源文件，并能生成或更新对应的 `subdir.mk`。而命令行 `make` 只读取当前磁盘上的 `makefile` 和 `subdir.mk`，不会重新扫描 IDE 工程配置，也不会自动发现新增源码。

因此会出现：

- Telink IDE 本地可编译：IDE 已生成或持有完整构建清单。
- 命令行编译失败：仓库中缺少某个被 Git 忽略或未提交的 `subdir.mk`。

## 已采取的修复

已从 `renzheng` 分支迁移并强制纳入版本控制：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample/boot/B85/subdir.mk
```

迁移时删除了 `renzheng` 中 MCU 自检相关宏：

```text
BMS_MCU_STARTUP_SELFTEST_ENABLE
```

保留普通 B85 启动汇编编译规则：

```makefile
tc32-elf-gcc -DMCU_STARTUP_8258 -c -o"$@" "$<"
```

修复后，命令行脚本已能使用默认 TC32 工具链完成完整编译，并生成：

```text
tc_ble_single_sdk_B85.elf
825x_ble_sample.lst
825x_ble_sample.bin
```

## 为什么容易漏掉

仓库 `.gitignore` 中存在：

```gitignore
*.mk
```

这会让新出现的 `subdir.mk` 默认处于忽略状态。已有被 Git 跟踪的 `subdir.mk` 修改后仍能显示变更，但新目录或之前未跟踪的 `subdir.mk` 很容易被隐藏。

因此，新增目录、新增启动汇编、新增模块源文件时，必须特别检查 `subdir.mk` 是否已经被版本控制。

## 以后新增 C 源文件或汇编文件怎么办

推荐流程：

1. 在 Telink IDE 中把 `.c` 或 `.S` 文件加入工程。
2. 用 Telink IDE 编译一次，让 IDE 更新对应目录的 `subdir.mk`。
3. 用 `git status --short` 检查源码和 `subdir.mk` 是否都出现变更。
4. 如果新增 `subdir.mk` 被忽略，用 `git add -f 路径/subdir.mk` 强制加入。
5. 再运行命令行构建脚本做完整 clean build。
6. 提交时同时提交源码、对应 `subdir.mk` 和必要文档。

如果只新增 `.h` 头文件，一般不需要新增对象文件，但修改头文件后建议执行 clean build，避免旧对象文件未重新编译。

## 能否由脚本自动生成 subdir.mk

可以自动生成，但不建议默认静默生成。

原因是 `subdir.mk` 不只是文件列表，还包含：

- 编译器命令。
- include 路径。
- 宏定义。
- 优化参数。
- C 标准。
- 汇编文件特殊参数。
- 输出对象路径。

如果脚本生成规则与 Telink IDE 不一致，可能出现两类风险：

- 直接编译失败，例如 include 路径、宏或汇编参数缺失。
- 更危险的是编译通过但固件行为不同，例如宏定义不同导致功能分支、Flash 布局、低功耗逻辑或协议行为不同。

更稳妥的策略是：

- 默认只检查，不自动改工程文件。
- 显式传入参数时才生成或修复缺失 `subdir.mk`。
- 生成规则优先复用同目录或同工程已有 `subdir.mk` 的编译参数。
- 生成后必须执行命令行 clean build。
- 关键版本还应与 Telink IDE 构建产物的大小、符号、map/list 关键项对比。

建议后续在 `build_b85_ble_sample.ps1` 中增加两类能力：

```powershell
# 默认检查：发现源码未进入任何 subdir.mk 时直接报错
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1"

# 显式修复：需要用户明确传参才自动生成或补齐 subdir.mk
powershell -ExecutionPolicy Bypass -File "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\script\build_b85_ble_sample.ps1" -RegenerateSubdirMk
```

其中默认检查应覆盖：

- 工程目录下所有被主 `makefile` 包含的 `subdir.mk` 是否存在。
- SDK 关键源码目录中的 `.c` / `.S` 是否进入对应 `subdir.mk`。
- `boot/B85/cstartup_825x.S` 是否生成 `boot/B85/cstartup_825x.o`。
- 新增 `subdir.mk` 是否被 `.gitignore` 忽略但未跟踪。

## 能否跨平台统一开发环境

可以统一“使用方式”和“入口”，但不一定能在所有平台直接使用同一套底层工具链。

### 当前 Telink B85 工程

当前工程是 Telink TC32/B85 SDK，现有构建链条明显偏 Windows：

- 使用 `tc32-elf-gcc`、`tc32-elf-ld`、`tc32-elf-objcopy`。
- 默认工具链路径是 Windows 风格路径，例如 `C:\TelinkSDK\opt\tc32\bin`。
- post-build 使用 Windows 可执行文件 `tl_check_fw2.exe`。
- IDE 生成的 `subdir.mk` 中包含 Windows 绝对路径。
- PowerShell 脚本当前主要面向 Windows。

因此，换到 macOS 后不能简单认为同一套脚本立即可用。需要满足以下条件之一：

- Telink 提供 macOS 可执行的 TC32 工具链和固件校验工具。
- 使用 Docker/虚拟机/Windows 构建机封装 Windows 工具链。
- 重写构建系统，消除 IDE 生成文件中的 Windows 绝对路径，并替换 post-build 工具。

实际推荐方案是统一入口，而不是强行要求所有芯片、所有平台使用同一个裸工具链。

### 推荐统一方式

仓库层面统一成下面的结构：

```text
scripts/
  build.ps1
  build.sh
  check_sources.py
  toolchain_probe.py
.vscode/
  tasks.json
doc/
  build_system.md
```

统一用户操作：

```powershell
# Windows
.\scripts\build.ps1 -Target b85_ble_sample

# macOS/Linux
./scripts/build.sh --target b85_ble_sample
```

VSCode 中统一任务名：

```text
build: current target
build: b85 ble_sample
build: stm32 app
static-analysis
```

每个芯片族只在底层 adapter 中差异化：

```text
scripts/toolchains/telink_tc32.ps1
scripts/toolchains/stm32_gcc.ps1
scripts/toolchains/stm32_cubeide.ps1
scripts/toolchains/keil.ps1
scripts/toolchains/iar.ps1
```

这样用户看到的是统一入口，底层工具链按 MCU 类型选择。

## 能否跨 MCU 统一，例如 Telink 换 STM32

可以统一工程管理方式，但不建议强行共用 Telink 的 `subdir.mk` 模式。

STM32 常见构建方式包括：

- STM32CubeIDE 生成的 Makefile。
- CMake + arm-none-eabi-gcc。
- Makefile + arm-none-eabi-gcc。
- Keil/IAR 工程。

更适合跨 MCU 的长期方案是：

- 新项目优先使用 CMake 或清晰维护的手写 Makefile。
- 源文件清单由仓库维护，不依赖 IDE 自动生成的临时片段。
- VSCode task 只调用统一入口脚本。
- 不同 MCU 的工具链探测、编译参数、烧录方式放到 adapter 层。
- CI 或本地脚本执行同一套 clean build 和源文件清单检查。

对 Telink 老工程，短期保留 IDE 生成 Makefile 是成本最低的选择；对 STM32 或新工程，建议从一开始就使用 CMake/Makefile 作为主构建系统，IDE 只作为编辑、调试入口。

## 风险分级

| 方案 | 优点 | 风险 | 建议 |
| --- | --- | --- | --- |
| 继续依赖 Telink IDE 生成 `subdir.mk` | 与 IDE 一致，风险低 | 新文件可能忘记提交 `subdir.mk` | 短期推荐 |
| 构建前只检查 `subdir.mk` 完整性 | 不改变工程文件，能提前发现漏编 | 不能自动修复 | 应尽快加入 |
| 显式参数自动生成 `subdir.mk` | 减少手工操作 | 生成参数可能和 IDE 不一致 | 可做，但默认关闭 |
| 统一重写 Makefile/CMake | 可跨平台、可维护、适合 CI | 初始迁移成本高，需要充分验证固件一致性 | 中长期推荐 |
| Docker/虚拟机封装 Windows 工具链 | 跨主机统一结果 | 依赖授权、镜像维护和 Windows 工具链 | 适合团队构建机 |

## 后续建议

1. 在当前 B85 构建脚本中增加源文件清单检查，默认启用，只报错不改文件。
2. 增加显式 `-RegenerateSubdirMk` 选项，作为人工确认后的修复工具。
3. 修改 `.gitignore` 策略，至少允许工程目录下的 `subdir.mk` 被跟踪，降低漏提交概率。
4. 为 B85 构建增加一次 clean build 验证记录，避免只验证增量构建。
5. 新 MCU 或新平台项目优先使用统一入口脚本 + CMake/Makefile adapter，不再把 IDE 生成文件作为唯一事实来源。

## 结论

本次失败的本质是构建清单缺失，不是 Telink IDE 与命令行工具链必然不一致。`subdir.mk` 可以自动生成，但默认静默生成会引入与 IDE 编译参数不一致的风险。

最稳妥的短期路线是：保留 Telink IDE 生成结果，仓库强制跟踪构建必需的 `subdir.mk`，并在命令行构建前做完整性检查。

中长期路线是：统一用户入口和 VSCode task，把 MCU 差异收敛到工具链 adapter；Telink 老工程先兼容 managed Makefile，STM32 或新工程优先采用 CMake/Makefile 作为主构建系统。
