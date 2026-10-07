> 历史记录：仅代表本文标注的日期和固定提交。当前实现见 [文档导航](../README.md)，当前验证以本轮报告为准。

# Monorepo 初次迁移验证记录

本页保留 `fd50730` 迁移时的历史记录；2026-10-05 后续修复与实测以 [整改验证](BMS_MONOREPO_REMEDIATION.md) 为准。

验证日期：2026-10-01；实现分支：`codex-bms-monorepo`；工作树：`D:\telink\bms-monorepo`。
仓库已有名为 `codex` 的分支，Git 无法同时创建 `codex/…`，因此使用上述独立分支名。
本次完成四产品源码合并、公共参数格式、构建选择、host 回归和 TC32 ELF 链接。
未生成 BIN，未烧录，也没有完成实板或 OTA 验收。

## 源码来源与责任

四个导入 commit 固定在 `bms/products/baselines.json`。公共模块和产品差异见 `BMS_MONOREPO.md`。
原工作树的 `d014-485-test`、未提交 `app.c` / `conf.h` 和 `references/` 没有被修改。
该测试分支晚于选定 D014 基线的实验变更没有隐式混入公共实现。

`monorepo_source_check.py` 验证 17 个公共 C 文件被四个产品清单直接引用，
产品目录没有业务 C 文件，SDK 示例目录没有另一份公共实现。
D008 实际构建 98 个对象，其余三个产品各 97 个对象；包含相同 SDK 的 61 个 C 文件和 2 个汇编文件。
D008 选择 DVC 后端，其他产品选择同一 SH 后端。

## 单一真源的实测

对唯一文件 `bms/core/bms_crc.c` 做两次临时实验，之后恢复原始字节：

1. 插入 `#error BMS_SINGLE_SOURCE_PROOF`，四个产品对象编译均以退出码 2 失败，均出现同一错误标记。
2. 只将 CRC16 初值 `0xFFFF` 改成 `0xFFFE`，四个产品编译均通过；
   比较全部对象 SHA-256，四个产品都只有 `bms/core/bms_crc.o` 字节改变。
3. 恢复源文件后，四个产品再次链接通过，全部对象 SHA-256 与实验前一致。

源文件原始 SHA-256 为 `299a3d3e47ba06e23a94f0dccbe35fe6f24512ccf5941720a95222637943ea91`。
四个产品 CRC 对象均由
`c81dff3280a92dbf95d5e5ef31c5b391624987e09544752e2d4b9dd433e6f083` 变为
`4ae79108ab5f0ad380f67872f79b5b57b64391e8a91e2e01df1815b4d46bab2a`，随后恢复。
实验没有使用复制、同步或替换产品私有 common 目录。
构建器采用保守的全输入指纹，公共头文件、工具、编译宏和 Git 身份变化会重新编译；
“只有一个对象字节改变”不表示仅执行了一条编译命令。

## Host 与工具验证

| 验证 | 实际结果 |
|---|---|
| `python tests/run_host_regression.py` | 98 组，0 失败；按公共 / SH / 产品专属范围运行 |
| 构建工具 unittest | 每产品 26 项，通过；包含清单、路径、收据、过期输入和资源检查 |
| CMake `bms_core` | 5 个生产模块独立于 Telink SDK 编译，`-Wall -Wextra -Werror` 通过 |
| CTest | `storage_journal`、`portable_core` 两项通过，断言有效 |
| 四产品源码清单 | 全部通过；重复、漏列、失效路径和错误后端均有检查 |
| 四产品 TC32 链接 | 全部通过，编译器诊断均为 0 error / 0 warning |
| 四产品 ELF/MAP 资源检查 | 全部通过；检查 TLSR8251 启动、SRAM 和镜像槽边界 |

Host 测试使用实际生产 C 文件或选定产品的实际预处理实现。
覆盖软件保护、SOC 输入与回放、校准、SH 电流方向、AFE 故障恢复、sleep/wake 返回值、
bus silence、三次有效样本资格、heater/balance 能力、温度无效与 MOS 独立保护、
Modbus 地址及事务边界、参数范围、Flash 写入失败和日志 checkpoint。
存储测试对 Config / State / Event 的逐字节写入中断执行掉电模拟，
验证未提交记录不取代旧记录、失败保存不发布新 RAM 值、旧格式及其他产品记录被拒绝。
整数校准另外用 10,000 组数据与独立高精度计算结果比较。
这些是 host 故障注入证据，不是物理 Flash 掉电或实际 MOS 动作证据。

## 资源预算

以下大小来自最终代码的 ELF/MAP，预计镜像含 16-byte 对齐和 4-byte 固件 CRC；没有执行 BIN 转换。
镜像槽上限 126,976 bytes，SRAM 32,768 bytes，主栈预留 3,072 bytes。

| 产品 | 预计镜像 bytes | Flash 余量 bytes | RAM 地址跨度 bytes | 扣除主栈预留后的余量 bytes |
|---|---:|---:|---:|---:|
| D008 | 121,780 | 5,196 | 25,024 | 4,672 |
| D011 | 116,676 | 10,300 | 24,196 | 5,500 |
| D013 | 116,020 | 10,956 | 24,180 | 5,516 |
| D014 | 116,676 | 10,300 | 24,196 | 5,500 |

D008 低于工具的 8 KiB Flash 余量提示线，后续新增功能必须重新检查预算。
RAM 地址跨度包含 RAM code/cache/IRQ stack 等占用；主栈预留是静态预算，不能替代实板 stack high-water 测量。

## 静态分析与证据边界

Cppcheck 使用各产品真实 TC32 编译宏、include 路径和依赖捕获结果。
公共及所选 AFE / 平台 / 产品头文件进入应用检查范围；SDK 依赖只用于解析，SDK 内诊断单独排除。
D008 检查 35 个应用 C 编译单元，其余产品各 34 个；不是把其他产品未编译代码当成当前目标。
本次没有 error / warning / portability 诊断；仍有 style 诊断，未宣称静态分析零问题。
D008 为 114 条 style，三个 SH 目标各 109 条，主要是声明/命名和现有代码风格提示。
SH 目标还有两个不被实际编译单元引用的头文件覆盖缺口；MISRA addon 没有安装，本次未运行 MISRA。

GitHub Actions 已加入四产品 host 回归和 Windows TC32 ELF / 资源检查，
但本次仅验证本机流程，没有把未运行的远程 CI 写成已通过。
CMake skill 的包装脚本因本机缺少 `tool_config` 无法启动，已直接调用本机 CMake / Ninja 完成验证。

`bms_core` 可移植子集已经脱离 SDK；SOC、参数、语义存储和 Modbus 虽然已为四产品共用，
仍有现有 `conf.h` / 时钟依赖。未来 STM32 接入还需实现具体平台和 AFE，不能视为本次已经完成 STM32 移植。

## 证据位置与复验

构建输出在源码树外：
`%LOCALAPPDATA%\CodexTemp\bms-monorepo-build\fcdfbbf61247\<product>\`。
各目标保存对象、ELF、MAP、LST、输入及链接收据、`gen/resources.json` 和 `static/latest.json`；
收据将当前输入与产物 hash 绑定，旧产物不能冒充当前结果。
提交完成后再次对干净 HEAD 链接和检查资源，产物内 Build ID 及收据应对应实现提交。

本次详细 host 日志与单一真源实验记录在：
`%LOCALAPPDATA%\CodexTemp\telink-bms-monorepo-20261001\complete-regression\results.json`、
同目录的各检查日志及 `single-source-proof/proof.json`。
这些目录是可清理的本机验证输出，不是编译所需的源码依赖。

正常复验入口：

```powershell
python bms_tools/bms.py --all-products sources --check
python tests/run_host_regression.py
python bms_tools/bms.py --all-products link --jobs 4
python bms_tools/bms.py --all-products resources
python bms_tools/bms.py --all-products static --no-report
```

后续实板验收分别覆盖四个硬件：上电默认参数、AFE 初始化失败、保护及物理恢复窗口、
MOS / heater / balance、电流校准及 SOC、通信负载、Flash 掉电、低功耗唤醒、watchdog 和 OTA。
开发板旧参数不迁移，第一次使用新格式会拒绝旧记录并按产品默认值初始化；默认值仍须按实际电池和板卡签核。


## 2026-10-05：精简与 OTA 参数控制

实现提交 `a700bcf`；本机验证提交 `b575189` 与远端 Git tree 完全相同。
四产品开发配置链接零错误、零警告；四产品生产配置及 D008 三种 profile 共六套 ELF/资源门禁通过。
104 组主机回归通过，包含 OTA 六类 Config 的 64 种选择组合，以及 Config/State/Event 逐字节写中断和重复启动测试。
CMake Release 的 storage_journal、portable_core、soc_eta 共 3 项测试通过。
只生成 ELF/MAP/LST；没有生成 BIN、烧录或进行实板 OTA。

生产最小 Flash 余量 8940 bytes，最小 RAM 余量（扣除 3072-byte 主栈预算）4692 bytes。
详见 [四产品配置审计](FOUR_PRODUCT_CONFIGURATION_AUDIT.md) 和 [OTA 参数控制](../OTA_PARAMETERS.md)。
实板仍需检查旧 schema 首次初始化、按类更新/保留、实际擦写掉电、OTA 切换及 MOS 授权时序。

远端 [CI 37291934835](https://github.com/CS19970929/telink-new-sdk-b85/actions/runs/37291934835) 的 8 个任务全部通过，
包含 Windows 四产品实际编译配置驱动的静态检查。
OTA 存储回归还使用一组 101～109 的替代更新编号重编译相同生产代码，确保以后调整发布编号不会破坏测试，
保留/更新的判定不依赖当前默认编号恰好为 1。
