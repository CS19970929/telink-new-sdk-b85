# new-new-master 无 Telink IDE 工具链

新增文件作用、源码自动发现限制、与旧 IDE 的一致性边界以及 Vendor `.a` 来源，详见 `toolchain_files_sources_and_vendor_libraries.md`。

## 目标与范围

本工具链从本机 `new-new-master` 提交 `b9d4d0790cd7f163867872bf6ce7980a71dfee76` 建立，目标是让工程在不打开 Telink IDE 的情况下完成环境检查、编译、清理重编、固件生成、官方固件检查、尺寸/MAP 分析、完整性记录、基线比较和基础静态分析。

迁移没有修改 `vendor/ble_sample` 产品代码、SDK 驱动、启动汇编或链接脚本。底层构建仍使用工程原有、已验证的 TC32 GCC 和官方 B85 库；Python 只是统一入口，GNU Make 只是薄驱动。

## 本机依赖

| 工具 | 固定位置或发现方式 |
|---|---|
| Python | 当前 `python`，建议 3.11 以上 |
| GNU Make | PATH 中的 `make`，否则 `C:\qp\qtools\bin\make.exe` |
| TC32 GCC | `C:\TelinkIoTStudio\opt\tc32\bin` |
| Telink BDT | `C:\TelinkIoTStudio\tools\libusbBDT\bin` |
| Cppcheck | `C:\Program Files\cppcheck\cppcheck.exe` |
| 固件检查 | SDK 自带 `script\tl_check_fw\tl_check_fw2.exe` |

先运行：

```powershell
python bms_tools/bms.py env
```

环境检查会验证链接脚本、官方库、工具版本和 Flash 布局，并报告 TLSR8251 声明与 `MCU_STARTUP_8258` 启动配置之间仍需硬件确认的风险。

## 构建

```powershell
# 增量构建
python bms_tools/bms.py build --jobs 4

# Clean + 完整重建
python bms_tools/bms.py rebuild --jobs 4
```

产物：

| 文件 | 用途 |
|---|---|
| `build/bms/825x_ble_sample.elf` | ELF 和符号分析 |
| `build/bms/gen/825x_ble_sample.map` | 链接 MAP |
| `build/bms/gen/825x_ble_sample.lst` | 反汇编和源代码混合列表 |
| `build/bms/825x_ble_sample.raw.bin` | objcopy 中间产物，不烧录 |
| `build/bms/825x_ble_sample.bin` | 经 `tl_check_fw2.exe` 单次处理的标准烧录文件 |
| `build/bms/fw_manifest.json` | 固件、工具和官方库哈希记录 |

每次 `build` / `rebuild` 都从 ELF 重新生成 raw BIN，再复制并执行一次官方后处理，避免对已带尾部信息的 BIN 重复处理。

## 分析与校验

```powershell
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static
```

`manifest` 记录最终 BIN 的大小、SHA-256、Telink 尾部 CRC/残值、Git 状态、目标配置和两份官方 `.a` 库的 SHA-256。`verify` 必须在烧录前通过。

Cppcheck 分为 `bms_app`、`vendor_common`、`drivers_b85`、`common` 四个 scope，输出到 `build/static/`。默认报告真实发现但不因历史 SDK 告警停止；需要门禁时使用 `static --strict`。

一键主机流水线：

```powershell
python bms_tools/bms.py ci --jobs 4
```

报告写入 `build/ci/ci_<UTC>.json` 和 `build/ci/latest.json`。该流水线不替代烧录、板级冒烟测试或硬件读回。

## 基线比较

保留已验证旧固件后执行：

```powershell
python bms_tools/bms.py baseline D:\path\reference.bin --report-only
```

若内容不一致，必须结合 MAP、section 尺寸、编译参数和源码差异解释；没有明确原因时不得直接接受。

本次迁移的主机验证结果：

- 连续两次 `rebuild` 的 raw BIN 和官方后处理 BIN SHA-256 均完全一致。
- 标准 BIN：91,060 字节，SHA-256 `ee9f3a9a91b1c326553c607e05324472ad43ffcf2b12eb9ef81f2cb06d6dccd9`。
- `text=86,364`、`data=4,504`、`bss=5,256`；Flash 统计 90,868 字节，RAM 统计 9,760 字节。
- `tl_check_fw2.exe`、manifest 和 verify 全部通过。
- 找到同名旧归档 `d3pro d003 V1.0 10s7.8Ah 20260715.bin`：91,076 字节，SHA-256 `00fc84909af4495719f6ce68b047eb62eeb9c2bc7ed66c13c2dbdee0d32a3dd3`。它同样具有有效 Telink 尾部，但比干净重建大 16 字节，并有链接地址重排；现有归档没有与该 BIN 同时保存的 ELF/MAP/完整构建日志，因此不能证明两者来自完全相同的对象输入。

上述差异不能直接标记为基线通过。当前命令行构建已经证明可复现，但在把新 BIN 用作发布固件前，仍需用真实 TLSR8251 BMS 执行现有功能冒烟测试，并由项目负责人确认该干净重建作为新的可追踪基线。

当前编译器有 85 条历史警告、0 错误。Cppcheck 初始盘点为：项目代码 417 项、`vendor/common` 261 项、B85 驱动 322 项、`common` 265 项；这些是待分类的真实初始发现，不代表构建失败，也不应使用大范围 suppression 隐藏。

## 烧录

```powershell
python bms_tools/bms.py flash-help
```

使用 Telink BDT 将 `build/bms/825x_ble_sample.bin` 写到 `0x00000`。烧录前运行 `verify`；不得烧录 `.raw.bin`，不得全片擦除，必须保护 `0x74000..0x7FFFF` 的 SDK 配对、MAC 和校准区域。烧录后仍需在真实板上执行产品现有功能冒烟测试。
