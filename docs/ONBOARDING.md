# 新员工交接与上手

目标：能选对产品、跑通本机验证、解释采样到 MOS 请求的路径，并提交一项范围明确的修改。[根 README](../README.md) 链接全部项目文档；本文是首次阅读入口。

## 1. 确认拿到的代码

从团队仓库取得 `codex-bms-monorepo` 分支，直接在原项目目录开发；本机 monorepo worktree 已移除。其他电脑可使用自己的目录，Windows 构建工具会处理路径中的空格。

```powershell
Set-Location 'D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)' # 改为自己的项目目录
git branch --show-current
git rev-parse HEAD
git status --short
git remote -v
```

完成标准：知道分支、完整 SHA、是否有未提交修改和仓库来源。既有脏文件先识别所有者，不用 reset/clean 清掉别人的工作。功能分支从此基线创建；公共代码合回 monorepo，旧 common 分支不再双向同步。

| 目录 | 接手时应理解的内容 |
|---|---|
| `bms/products/<product>/` | 产品身份、串数/化学体系、引脚/AFE 输入、参数更新编号、源码清单 |
| `bms/app/` | 启动、调度、低功耗、heater/balance/open-wire 策略 |
| `bms/core/` | 参数、保护、SOC、存储、协议与诊断；共享不等于全部脱离 SDK |
| `bms/afe/` | DVC1124 与 SH3673510 两种硬件后端 |
| `bms/platform/telink/` | main/IRQ、BLE/UART、Flash、GPIO、栈观测 |
| `references/afe/`、`docs/afe-audit/` | 正式原厂资料、原 PDF 检索、已知错误/原文矛盾与固定提交审查；先读 [使用指南](AFE_REFERENCE_GUIDE.md) |
| `bms_tools/`、`tests/` | TC32 工具与 host 回归 |

## 2. 准备 Windows 环境

| 工具 | 用途与检查 |
|---|---|
| Git | `git --version` |
| Python 3.11+ | `python --version`；脚本主要使用标准库 |
| 官方 TC32 + GNU Make | 固件对象/ELF；默认 `C:/TelinkIoTStudio/opt/tc32/bin`，可用 `TC32_BIN` 指定；用 `bms.py env` 核对 |
| 本机 C 编译器 | host 测试；本机示例 `C:/qp/qtools/MinGW32/bin/cc.exe`，以 `CC` 指定，不用于 TC32 固件 |
| CMake + Ninja | 可选可移植核心测试，命令见构建指南 |
| Cppcheck | `static --no-report` 不依赖本机 Excel 模板 |

固件使用仓库 SDK 的启动文件、`boot.link` 和两个 Vendor `.a`。不要从其他 Telink/ARM 工程随意补库或替换 ABI。

```powershell
$env:PYTHONUTF8 = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
# 下两行是本机示例；其他电脑换成已安装编译器的位置。
$env:CC = 'C:/qp/qtools/MinGW32/bin/cc.exe'
$env:PATH = 'C:/qp/qtools/MinGW32/bin;' + $env:PATH
python bms_tools/bms.py --product d014 env
python bms_tools/bms.py --all-products sources --check
python tests/run_host_regression.py
python bms_tools/bms.py --product d014 link --jobs 4
python bms_tools/bms.py --product d014 resources
```

每条命令完成后确认 `$LASTEXITCODE` 为 0 再继续。完成标准：host 总结 `0 failed`；TC32 零 error/warning；找到外部输出中的 ELF、MAP、资源报告和 host `results.json`。单个 host 脚本默认 D014，其他产品须设置 `BMS_PRODUCT`；完整 runner 自动选择。失败按 [构建与验证](BUILD_AND_TEST.md) 定位。

## 3. 分三轮读代码

按 [代码阅读指南](CODE_READING_GUIDE.md)，每轮只追 2～5 个核心文件：

1. 产品与启动：产品头文件 → `main.c` → `app_ble.c::user_init_normal()` → `app.c::app_init()`。指出实际后端、启动输出禁止位置和 200 ms 采样入口。
2. 参数与保护：`bms_parameters.c`、Config、软件保护、AFE guard。区分编译默认、Flash 值、requested/effective、软件命令与物理 MOS 状态。
3. 一项业务：SOC 或均衡 → 协议/诊断 → host 测试。解释输入失效、保存失败和重启时如何处理。

用 `bms.code-workspace` 打开阅读视图；SDK 被隐藏但仍参与编译。需要核对 IRQ、寄存器、Flash、BLE 时再打开实际编译的 SDK 文件。

## 4. 第一项修改

从 [配置指南](CONFIGURATION_AND_BUILD_GUIDE.md) 的开发日志练习开始；不改硬件参数即可练习选择产品、外部输出和验证。正式容量或保护配置只按批准需求修改。

中文提交说明包括：产品、原因/单位、涉及文件、是否改变参数更新编号、测试结论、尚需的实板验证。只暂存本次文件。镜像生成、OTA 和烧录是明确交付步骤，不属于阅读或 `link` 的隐含操作。

## 5. 交接验收

| 接收人要能完成 | 可检查的结果 |
|---|---|
| 选对产品 | 说明产品/profile、串数、AFE、Rsense、通信和能力限制 |
| 找到真实源码 | 展示 `sources.txt`，指出公共修改影响哪些产品 |
| 跑通环境 | 保存 SHA、工具版本、host 结果和目标 ELF 资源报告 |
| 做简单修改 | 小 diff、默认值与更新编号说明、相关检查通过 |
| 定位“参数没变” | 核对 Build ID、设备持久值、更新编号和启动结果 |
| 定位“MOS 没开” | 分别查 requested、guard、软件故障、AFE lockout；命令不是 Gate 反馈 |
| 知道剩余工作 | 列出 [硬件验收](HARDWARE_VALIDATION.md) 中本产品未关闭项 |

交接人另需提供：产品需求与参数签核人、原理图/BOM 的版本和受控位置、板号/电池配置、工具链来源、最近可重现的发布提交/镜像、上位机版本、实板记录和未关闭问题。当前仓库已包含六份 AFE 原手册与 D008 24S 原理图；其他产品原图/BOM、实装与签核资料仍需交接，文档整理不能代替这些资料。D013 的继承 IO、D014 的开发容量仍不是已签核事实。
