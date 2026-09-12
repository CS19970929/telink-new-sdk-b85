# 构建、测试与发布门禁

## 1. 固定环境

正式固件使用项目锁定的 `tc32-elf-gcc 4.5.1-tc32-1.3`、Telink 官方 B85 库、现有 `boot.link` 和 `tl_check_fw2.exe`。不得因主机方便替换编译器、ABI、链接脚本或 Vendor `.a`。

Windows 常见依赖位置：

| 工具 | 发现方式 |
|---|---|
| Python | 当前 `python`，建议 3.11+ |
| GNU Make | `PATH` 或 `C:\qp\qtools\bin\make.exe` |
| TC32 | `C:\TelinkIoTStudio\opt\tc32\bin` |
| BDT | `C:\TelinkIoTStudio\tools\libusbBDT\bin` |
| Cppcheck | `PATH` 或 `C:\Program Files\cppcheck\cppcheck.exe` |

先运行 `python bms_tools/bms.py env`，不要在缺失工具时把 host clang 结果描述为 TC32 构建成功。

## 2. 源码与链接顺序

`bms_tools/source_order.txt` 是版本化的唯一源码/对象顺序清单。它保持与旧 Telink IDE 已验证链接顺序一致，也避免不同主机的文件排序改变固件。

```bash
python3 bms_tools/bms.py sources --check
python3 bms_tools/bms.py sources --update   # 仅在源码集合确实改变时
git diff -- bms_tools/source_order.txt
```

更新后必须人工确认只出现预期的新增、删除或替换，不能把排序漂移混入功能提交。

## 3. 构建和产物

```powershell
python bms_tools/bms.py build --jobs 4
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
```

命令行产物位于 `project/tlsr_tc32/B85/825x_ble_sample_cli/`，不会覆盖 IDE 的 `825x_ble_sample/`。烧录文件是 `825x_ble_sample.bin`；`.raw.bin` 是中间产物，不得直接烧录。

## 4. 主机契约测试

```bash
python3 tests/dvc1124_config_quick_check.py
python3 tests/flash_quick_check.py
python3 -m unittest tests/test_bms_tools.py
```

这些测试覆盖寄存器真值、特殊访问策略、配置事务、协议共用入口、Flash 区域互斥、SOC/Runtime 源码契约和构建工具。它们不执行目标指令，也不替代时序与实板验证。

可选宿主机 clang 只用于发现声明、类型和语法问题。Telink SDK 针对 32 位内存映射寄存器，会在普通 64 位 clang 下产生大量非目标平台 warning；审查时要区分 SDK 噪声和应用错误。

## 5. 静态分析与 CI

GitHub Actions 的完整安装、使用、Windows 服务维护、Artifact 下载、安全下线和
故障排查见 `docs/GITHUB_ACTIONS_RUNBOOK.md`；本节只定义构建与发布门禁。

```powershell
python bms_tools/bms.py static
python bms_tools/bms.py static --strict
python bms_tools/bms.py ci --jobs 4
```

不要用全局 suppression 隐藏应用层问题。Vendor SDK 历史告警应按 scope 单独记录，项目新增告警必须解释或修复。

GitHub Actions 的 `TC32 production build` 运行在 repository-level Windows
self-hosted runner 上，runner 必须带有 `telink-tc32` label。仓库是 public，
该 job 同时受 `TELINK_TC32_CI_ENABLED=1` 和事件来源门禁约束：push、手工触发
及同仓库 PR 可以进入 runner，外部 fork PR 只能执行 GitHub-hosted 的
`Host contract checks`，不得在本机执行代码。

当前锁定的 Windows TC32 安装不提供目标端 `libgcc.a`，因此目标代码不能依赖
`__muldi3`、`__divdi3` 等 64-bit runtime helper。高频测量换算应在给出范围证明
后使用等价的 32-bit 定点表达式；不能混入 host、ARM 或其他 TC32 版本的
`libgcc`。每次 clean rebuild 都以最终链接结果验证该约束。

## 6. 发布门禁

每个候选 commit 至少需要：

1. `sources --check` 与全部 host tests 通过。
2. 固定 TC32 clean rebuild 通过。
3. MAP/BIN 不超过 124 KB 默认 OTA 上限，且布局检查通过。
4. `check-fw`、manifest 和 `verify` 通过。
5. 保护、MOS、通信、Flash、OTA 或低功耗改动完成对应实板矩阵。
6. 测试记录包含板号、AFE 型号/修订、固件 commit、参数、仪器和结果。

烧录地址为 `0x00000`。禁止全片擦除，必须保护 `0x74000..0x7FFFF` 的配对、MAC 和校准区域。
