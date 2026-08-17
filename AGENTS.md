# AGENTS.md — TLSR8251 BMS 命令行开发规则

本分支基于本机 `new-new-master` 的提交 `b9d4d0790cd7f163867872bf6ce7980a71dfee76`，只增加可复现的命令行开发工具链，不修改产品固件源码或运行行为。

## 平台

- MCU：Telink TLSR8251 / TLSR825x B85 系列。
- CPU：TC32，不是 ARM Cortex-M；禁止使用 ARM GCC、普通 GCC 或 ARM 链接脚本。
- SDK：`tc_ble_single_sdk V3.4.2.8_Patch_0001`，禁止自动升级。
- 应用：`tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/`。
- 启动：`boot/B85/cstartup_825x.S`，入口 `__start`。
- 链接：`project/tlsr_tc32/B85/boot.link`。

当前工程声明目标为 TLSR8251，但沿用 SDK 的 `MCU_STARTUP_8258` 启动配置；脚本会明确显示该目标配置风险。未完成真实硬件和内存边界复核前，不得静默修改启动宏或链接脚本。

## 固定工具链

- `C:\TelinkIoTStudio\opt\tc32\bin\tc32-elf-gcc.exe`
- 编译器版本：GCC `4.5.1-tc32-1.3`
- 官方预编译库：`proj_lib/liblt_825x.a`、`proj_lib/liblt_general_stack.a`
- 官方固件后处理：`script/tl_check_fw/tl_check_fw2.exe`
- 烧录：Telink BDT；默认路径 `C:\TelinkIoTStudio\tools\libusbBDT\bin`

禁止擅自升级编译器、SDK、ABI、启动代码、链接脚本或替换预编译库。两份 `.a` 文件属于构建输入，必须保留在 Git 中并由 manifest 记录 SHA-256。

## 唯一命令入口

在仓库根目录执行：

```powershell
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python bms_tools/bms.py build --jobs 4
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static
python bms_tools/bms.py ci --jobs 4
```

`build` 和 `rebuild` 会自动完成 ELF、MAP、LST、raw BIN、官方后处理 BIN 的生成。命令行产物统一放在 `project/tlsr_tc32/B85/825x_ble_sample_cli/`，与 IDE 的 `project/tlsr_tc32/B85/825x_ble_sample/` 完全分离。可烧录文件只有 `825x_ble_sample_cli/825x_ble_sample.bin`；`825x_ble_sample.raw.bin` 是中间产物，禁止烧录。

仓库路径含空格时，脚本会建立 `C:\opencode\bms_repo` junction 供 GNU Make 使用，不需要手工修改路径或系统 PATH。

## 临时文件边界

- 禁止在仓库内创建或使用 `.codex_tmp/` 存放 Codex/ChatGPT 的日志、截图、文档渲染、表格审计、解压文件或其他中间产物。
- 此类临时文件统一放到 Windows 用户临时目录，例如 `$env:LOCALAPPDATA\CodexTemp\tlsr8251-bms\<task-id>`；任务结束后可按需清理。
- 工程命令自身的可复现构建和静态分析证据仍按既定规则写入 `project/tlsr_tc32/B85/825x_ble_sample_cli/`，不得改为不可追溯的临时输出。
- 需要交付给用户的正式文件应写入用户明确指定的位置；未指定时先说明目标位置，不得为了中间处理在仓库根目录新增临时目录。

## 固定编译与链接参数

```text
tc32-elf-gcc -ffunction-sections -fdata-sections -Wall -O2
  -fpack-struct -fshort-enums -finline-small-functions
  -std=gnu99 -fshort-wchar -fms-extensions
  -D__PROJECT_8258_BLE_SAMPLE__=1 -DCHIP_TYPE=CHIP_TYPE_825x

startup assembly: -DMCU_STARTUP_8258
link: tc32-elf-ld --gc-sections -L proj_lib -T boot.link
      ... -llt_825x -llt_general_stack
```

不要仅为了“现代化”改用 CMake；`build.mk` 是保持旧固件编译语义的薄驱动，源文件规则由 Python 自动生成。

## 源码与链接顺序

`bms_tools/source_order.txt` 是参与构建的 `.c` / `.S` 和对象链接顺序的权威输入。不得依赖文件系统枚举顺序，也不得让 `build/rebuild` 静默改写它。

- 新增、删除或重命名源文件后，先执行 `python bms_tools/bms.py sources --update`，审核并提交 `source_order.txt` 的 Git diff，然后执行 `sources --check` 和 `rebuild`；不需要手写逐文件 Make 规则。
- `vendor/ble_sample/` 递归发现项目源文件；新增其他顶层源码组时需审核并修改 `SOURCE_GROUPS`。
- 若保留 IDE 生成文件，可用 `sources --compare-ide` 只读对照；只有明确决定采用 IDE 顺序时才能执行 `sources --import-ide`。IDE 文件不是构建依赖。
- 修改头文件后必须 `rebuild`；当前构建不依赖自动生成的头文件 `.d` 文件。
- 顺序变化会改变链接地址和 BIN。重大变化必须比较 ELF/MAP/BIN 并做真实硬件回归。

## 修改边界和验证

- `vendor/ble_sample/` 是项目代码；修改后必须执行 `rebuild`、`check-fw`、`size`、`manifest`、`verify`、`static`。
- `vendor/common/`、`drivers/B85/`、`common/` 是官方 SDK 边界，原则上不修改。
- `boot/B85/`、`boot.link`、`proj_lib/*.a` 禁止随意修改。
- 烧录前先执行 `verify`，禁止全片擦除或擦除 `0x74000..0x7FFFF`。
- 重大修改后用 `baseline <reference.bin>` 比较尺寸、SHA-256 和首末差异位置，并在真实 TLSR8251 板上完成现有功能冒烟测试。

静态分析由 cppcheck 执行，只检查 `vendor/ble_sample` 应用层。官方 SDK 头文件仅为保持真实类型、宏和条件编译而解析，其诊断按可追溯范围清单排除，不纳入问题统计。结果输出到 `project/tlsr_tc32/B85/825x_ble_sample_cli/static/`。Cppcheck 结果不能描述为完整 MISRA-C 合规结论。

详细迁移说明见 `docs/no_ide_toolchain_new_new_master.md`；新增文件、源码自动发现、IDE 一致性边界和 Vendor `.a` 来源见 `docs/toolchain_files_sources_and_vendor_libraries.md`；顺序管理和发布门禁见 `docs/source_link_order_management.md`。
