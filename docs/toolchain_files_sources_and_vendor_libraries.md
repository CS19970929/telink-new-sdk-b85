# 无 Telink IDE 工具链：文件、源码发现与 Vendor 库说明

本文记录 `codex-new-new-master-no-ide-toolchain` 分支建立命令行工具链时的关键事实和使用边界，避免这些信息只保留在对话记录中。

## 1. 分支范围

该分支基于本机 `new-new-master` 提交 `b9d4d0790cd7f163867872bf6ce7980a71dfee76`，工具链提交为 `b750a995a6ec523460526f5439c0be8075c2a6c9`。

本次只增加命令行开发工具、构建输入和维护文档，没有修改 `vendor/ble_sample` 产品固件源码，没有加入诊断协议、故障注入或运行期功能。

命令行构建仍使用：

- Telink TC32 GCC `4.5.1-tc32-1.3`；
- SDK `tc_ble_single_sdk V3.4.2.8_Patch_0001`；
- 原工程的编译参数、宏、启动汇编、链接脚本和预编译库；
- SDK 自带的 `tl_check_fw2.exe` 固件后处理程序。

Telink IoT Studio 当前仍作为本机 TC32 编译器和 BDT 的安装来源，但构建不需要启动 IDE GUI。

## 2. 新增文件的作用

| 文件 | 作用 |
|---|---|
| `.gitattributes` | 固定 Python、Make 等文本文件的换行格式，减少 Windows CRLF/LF 无意义差异。 |
| `.gitignore` | 忽略 OBJ、ELF、BIN、MAP、LST、Python 缓存和构建目录；为 `build.mk` 及两份必需的 Vendor `.a` 库设置跟踪例外。 |
| `AGENTS.md` | 记录 MCU、TC32 架构、SDK、工具链、构建入口、禁止修改边界和修改后的验证要求。 |
| `bms_tools/bms.py` | 统一命令入口；完成环境检查、源码发现、Make 规则生成、构建、固件后处理、尺寸/MAP、manifest、基线比较和静态分析。 |
| `bms_tools/build.mk` | 保存固定的编译、汇编和链接参数；它是薄层构建驱动，单个源文件规则由 `bms.py` 生成。 |
| `bms_tools/source_order.txt` | 版本化保存参与构建的 `.c` / `.S` 及其链接顺序；它是命令行构建的权威顺序输入。 |
| `bms_tools/static_analysis/cppcheck.cfg` | 固定 Cppcheck 分析选项；只统计 `vendor/ble_sample` 应用层问题，SDK头文件仅解析并由每次运行生成的范围清单排除诊断。 |
| `docs/no_ide_toolchain_new_new_master.md` | 记录环境、构建、产物、校验、基线和烧录流程。 |
| `docs/project_flash_map_8251_512k.md` | 记录当前 512 KB Flash、OTA、业务数据和 SDK 保留区域。 |
| `proj_lib/liblt_825x.a` | 当前 SDK 随附的 TLSR825x 预编译链接库。 |
| `proj_lib/liblt_general_stack.a` | 当前 SDK 随附的协议栈预编译链接库。 |
| `tests/test_bms_tools.py` | 验证工具 PATH 处理、CRC/Telink 尾部契约及允许暴露的命令集合。 |

Git 查看器以红色块显示 `.a`，只是因为二进制文件不能按文本展示差异，不表示文件损坏。

## 3. 新增源文件如何编译

每次调用 `build` 或 `rebuild` 时，`bms.py` 都会根据 `bms_tools/source_order.txt` 重新生成 `project/tlsr_tc32/B85/825x_ble_sample_cli/sources.mk`，因此不需要为每个源文件手写 Make 规则。命令行输出目录与 IDE 的 `project/tlsr_tc32/B85/825x_ble_sample/` 分离。顺序文件受 Git 管理，新增、删除或重命名源文件时，构建会先失败并列出差异，不会静默改变链接布局。

当前直接扫描以下目录中的 `*.c` 和 `*.S`：

```text
vendor/common
vendor/ble_sample
drivers/B85
drivers/B85/flash
drivers/B85/driver_ext
common
boot/B85
application/usbstd
application/print
application/keyboard
application/audio
application/app
```

`vendor/ble_sample/` 会递归发现子目录中的源文件；其余 SDK 目录保持与原工程一致的非递归边界。比如新增：

```text
vendor/ble_sample/my_feature/my_feature.c
vendor/ble_sample/my_feature/my_feature.h
```

第一次执行检查或构建会报告 `unlisted new source`。审核文件确实应参与固件后，执行：

```powershell
python bms_tools/bms.py sources --update
git diff -- bms_tools/source_order.txt
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 4
```

`--update` 只更新顺序清单，不写 Makefile；随后的构建自动生成逐文件规则。顺序变化必须随代码一起评审和提交。

### 3.1 与可选 IDE 输出对照

命令行构建不读取 IDE 工程文件。若本机恰好有 Telink Eclipse CDT 生成的 `project/tlsr_tc32/B85/825x_ble_sample/makefile` 和各目录 `subdir.mk`，可做只读对照：

```powershell
python bms_tools/bms.py sources --compare-ide
```

该命令一致时返回 0；不一致时打印前 20 个顺序差异且不修改文件。确实要采用 IDE 新顺序时，必须显式执行并审核：

```powershell
python bms_tools/bms.py sources --import-ide
git diff -- bms_tools/source_order.txt
```

因此可以同步，但不会“自动跟随 GUI”。这是有意的安全门禁：IDE 生成文件可能陈旧或只反映某台电脑的增量状态，不能在无人审核时改写发布固件布局。

### 3.2 当前限制

源码发现与编译规则的边界如下：

- `vendor/ble_sample/` 的 `.c` / `.S`（包括新子目录）会被发现，但必须通过 `sources --update` 明确纳入；
- 新增全新的顶层源码组时，需要修改 `bms.py` 的 `SOURCE_GROUPS`，仍不需要手写逐文件 Make 规则；
- 新增头文件搜索路径时，需要修改 `build.mk` 的 `INCLUDES`；
- 新增全局宏、链接库或特殊编译参数时，需要修改 `DEFINES`、`LIBS` 或相应 flags；
- 当前不扫描 `.cpp`，也没有为特殊源文件提供单独编译参数。

当前生成的 Make 依赖只记录“对象文件依赖对应 `.c` / `.S`”，没有生成头文件 `.d` 依赖。只修改 `.h` 后，增量 `build` 可能不会重编所有受影响的源文件，所以修改头文件后必须使用 `rebuild`。

推荐的修改后验证顺序：

```powershell
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static
```

## 4. 与 Telink IDE 构建的一致性

### 4.1 已确认一致的配置

- 编译器：`tc32-elf-gcc 4.5.1-tc32-1.3`；
- 优化：`-O2`；
- 编译选项：`-ffunction-sections -fdata-sections -Wall -fpack-struct -fshort-enums -finline-small-functions -std=gnu99 -fshort-wchar -fms-extensions`；
- 工程宏：`__PROJECT_8258_BLE_SAMPLE__=1`、`CHIP_TYPE=CHIP_TYPE_825x`；
- Startup 汇编宏：`MCU_STARTUP_8258`；
- 链接器：`tc32-elf-ld --gc-sections`；
- 链接脚本：`project/tlsr_tc32/B85/boot.link`；
- 链接库：`liblt_825x.a`、`liblt_general_stack.a`；
- 入口：`__start`；
- 最终 BIN 使用 SDK 自带 `tl_check_fw2.exe` 单次后处理。

命令行工具链连续两次完整 `rebuild` 的 raw BIN 和最终 BIN SHA-256 均完全一致，说明命令行构建自身具有确定性。

### 4.2 当前 IDE 差异的根因和修复验证

对同一份源码的当前 IDE 与旧版命令行结果逐项比较后确认：81 个 `.o` 的 SHA-256 全部相同，924 个符号的名称和尺寸也相同；唯一差别是链接命令中的对象顺序：

- IDE 在 `vendor/ble_sample` 中把 `SocEnhance.o` 放在第一项，旧脚本因 Windows 不区分大小写排序把它放在最后；
- IDE 在 `common` 中把 `div_mod.o` 放在第一项，旧脚本把所有 `.S` 放在 `.c` 后面。

用同一批 81 个对象按 IDE 顺序重新链接后，ELF 和最终 BIN 均与 IDE 字节级完全相同：

| 产物 | IDE | 按 IDE 顺序的命令行重链接 |
|---|---|---|
| ELF SHA-256 | `2A2D24B25C0A7718D11CC474C4917E4C40F7174CDB32A68FE3FA561AB2C9E4D8` | 相同 |
| BIN 大小 | 91,092 字节 | 91,092 字节 |
| BIN SHA-256 | `B324353F462C15D64DFE2B9F24F63830A4AE973D320C4CC2262A9E5808FF7627` | 相同 |

这证明本次同源码差异来自链接顺序，而不是编译器、编译参数、源码、`.a` 库或 `tl_check_fw2.exe`。现在 `source_order.txt` 固化该顺序，`build/rebuild` 在顺序文件与磁盘源码不一致时拒绝继续；manifest v3 还记录顺序文件、对象顺序、每个对象、`build.mk`、链接脚本和 Vendor 库的 SHA-256。

“字节级一致”只对参与比较的源码状态、工具链和构建输入成立。以后新增源文件会合理改变地址与 BIN；此时应审核顺序差异、保存新基线并做硬件回归，而不是要求继续匹配旧 BIN。

### 4.3 旧归档基线也已恢复字节级一致

现有旧归档 `d3pro d003 V1.0 10s7.8Ah 20260715.bin` 与命令行干净重建结果如下：

| 项目 | 旧归档 BIN | 命令行干净重建 BIN |
|---|---:|---:|
| 文件大小 | 91,076 字节 | 91,076 字节 |
| SHA-256 | `00fc84909af4495719f6ce68b047eb62eeb9c2bc7ed66c13c2dbdee0d32a3dd3` | `00fc84909af4495719f6ce68b047eb62eeb9c2bc7ed66c13c2dbdee0d32a3dd3` |
| Telink 尾部校验 | 有效 | 有效 |

修复对象顺序后，原来 16 字节的尺寸差和地址重排全部消失，`baseline` 可判定 `BASELINE MATCH`。这进一步证明旧命令行脚本的差异来自链接顺序。

准确结论是：对于该 Git 源码状态，编译器、参数、宏、启动、链接配置和对象顺序均已对齐，命令行构建稳定可重复、通过官方固件检查，并与归档 BIN 字节级一致。以后源码或顺序发生变化时需建立新的基线，不能把这次哈希当作所有未来版本的固定值。

首次将命令行固件作为发布基线前，应至少验证现有的 Startup、AFE、Cell、Temperature、Current、Protection、CHG/DSG、Communication、Flash Storage、Watchdog 和 Low Power 功能，并保存 BIN、ELF、MAP、manifest 和硬件测试记录。

另有一项继承自原工程的配置风险：工程声明目标是 TLSR8251，但启动汇编仍使用 `MCU_STARTUP_8258`，其 SRAM 结束配置与 SDK 中 TLSR8251 定义不同。工具链只报告该风险，没有擅自修改启动配置；更改前必须确认实际芯片和硬件基线。

## 5. 两份 `.a` 文件的来源

### 5.1 为什么以前 Git 中没有

原 `.gitignore` 包含全局规则：

```gitignore
*.a
```

所以两份库虽然存在于原 SDK 工作目录并一直被 Telink IDE 链接，但没有进入 Git。创建新 worktree 或在新电脑克隆仓库时，Git 不会复制这些被忽略的文件，导致命令行链接缺少构建输入。

为使构建可迁移，本分支为这两个固定路径增加 `.gitignore` 例外并纳入版本控制。

### 5.2 Git 来源链

这两个文件最早在本仓库提交 `ff94ba8e48637abe2bea09c6a322d9582dd22a20` 中进入 Git；其父提交 `7ca59c216f538a72beb8c637af534c93b4717cd1` 尚未跟踪它们。

建立 `codex-new-new-master-no-ide-toolchain` 分支时，从现有 `no-telink-ide` 分支恢复相同 Git blob，没有重编、编辑或二进制补丁操作：

| 文件 | Git blob |
|---|---|
| `liblt_825x.a` | `fa73e443e07ac17edfd3b33dde5f9250efa2ded4` |
| `liblt_general_stack.a` | `6f20a889a5dfb912ac908a1b5118dbd590512a59` |

### 5.3 大小和 SHA-256

| 文件 | 大小 | SHA-256 |
|---|---:|---|
| `liblt_825x.a` | 484,472 字节 | `3A51224D664F9D54E1746B5DC2D9B48EEC727020D8DC038EA51B8B64390AAE17` |
| `liblt_general_stack.a` | 950 字节 | `E0331419862BE31572B4DF5B3A4ED4B82E8C2526BFBA92FF7FC71D2C589C74A0` |

只读扫描本机 `D:\telink` 后，当前工程、多个工程副本以及 `D:\telink\TLSR-8258` 下的 `V3.4.2.8_Patch_0001` SDK 副本均得到完全相同的大小和 SHA-256。旧版 `V3.4.2.1` SDK 中同名库的大小和哈希不同，本分支没有使用旧版文件。

因此可以确认，本分支中的库与本机多个 `V3.4.2.8_Patch_0001` SDK 副本字节完全一致；迁移过程中没有修改或重新编译 `.a`。`manifest` 会继续记录两个文件的 SHA-256，以便发现未来的非预期变化。

如果未来确实需要升级或替换 Vendor 库，必须同时记录来源 SDK、原始文件哈希、工具链版本、基线差异和真实硬件验证结果，不能直接覆盖现有 `.a`。
