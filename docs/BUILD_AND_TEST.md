# 构建、测试与交付

本页命令在仓库根目录 PowerShell 执行。产品配置见 [配置指南](CONFIGURATION_AND_BUILD_GUIDE.md)，工具准备见 [上手指南](ONBOARDING.md)。每条命令结束检查 `$LASTEXITCODE`，非 0 时先解决失败。

本机验证的执行条件和范围以[根协作规则](../AGENTS.md#本机验证约定)为准：默认源码审查和差异检查，用户明确要求测试、编译或运行验证后才运行相应命令。本页命令用于按需执行；完整回归须有明确的完整范围要求。交付时注明实际执行项及未执行项。本约定不修改现有远端 CI 流程。

## 1. 产品与输出

`--product d008/d011/d013/d014` 优先于环境 `BMS_PRODUCT`，两者都未指定时为 D014。`--all-products` 依次执行四产品；`--jobs 4` 是单产品 Make 并行度。D008 profile 使用 `--d008-profile 16s-lfp/20s-nmc/24s-lfp`，开发默认 16S LFP，生产必须显式选择。

源码/链接顺序真源为 `bms/products/<product>/sources.txt`。新增/删除 `.c` 时更新相关清单并审查顺序，可用 `sources --update` 生成候选，再检查 diff 和四产品 `sources --check`。不要直接调用 Make、旧 IDE 自动扫描或历史分支 source_order。

```powershell
$env:PYTHONUTF8 = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
python bms_tools/bms.py --product d014 env
python bms_tools/bms.py --all-products sources --check
```

对象、ELF/MAP、raw BIN 和日志的默认根为 `%LOCALAPPDATA%/CodexTemp/bms-monorepo-build`，可用 `BMS_BUILD_ROOT` 指向其他源码树外目录。实际路径为 `<根>/<checkout-hash>/<mode-profile>/<product>/`，看 `env` 的 `build dir`。最终校验通过的 BIN 和 manifest 固定放在当前项目 `firmware/<mode-profile>/<product>/`，看 `final firmware dir`；D008 的 profile 始终附加到 mode。Windows 无空格 junction 由工具在用户临时区维护。

| 文件 | 含义 |
|---|---|
| `obj/` | 目标对象文件 |
| `825x_ble_sample.elf` | 链接 ELF |
| `gen/825x_ble_sample.map`、`.lst` | 链接分布/反汇编 |
| `gen/build.log` | 编译与 warning 门禁日志 |
| `gen/compile-inputs.json`、`gen/link-completed.json` | 输入指纹和链接完成收据 |
| `gen/resources.json` | 资源报告，必须识别是 ELF 投影还是 BIN 检查 |
| 树外 `825x_ble_sample.raw.bin` 及 checker 处理中间镜像 | 仅明确执行镜像流程才产生；不是交付入口 |
| 项目内 `firmware/<mode-profile>/<product>/825x_ble_sample.bin`、`fw_manifest.json` | 校验通过的最终镜像及显式生成的完整性清单 |

同一 checkout/模式/profile/产品不要并发两个构建。若要强制全编译又不生成 BIN，改用新的外部 `BMS_BUILD_ROOT` 后执行 `link`，不使用 `rebuild`。

## 2. 按需验证：不生成 BIN

```powershell
python bms_tools/bms.py --product d014 compile --jobs 4
python bms_tools/bms.py --product d014 link --jobs 4
python bms_tools/bms.py --product d014 resources
```

`link` 会编译所需对象，获准编译/链接时通常直接执行即可；单独 `compile` 用于只检查编译。获准测试公共 core/app/平台或构建设置，且用户未限定更小范围时，使用四产品入口：

```powershell
python bms_tools/bms.py --all-products sources --check
python bms_tools/bms.py --all-products link --jobs 4
python bms_tools/bms.py --all-products resources
python bms_tools/bms.py --all-products test
```

host 需要本机 C 编译器；Windows 设置示例：

```powershell
$env:CC = 'C:/qp/qtools/MinGW32/bin/cc.exe'
$env:PATH = 'C:/qp/qtools/MinGW32/bin;' + $env:PATH
# 可选，保持在源码树外；每次验证用独立目录留证。
$env:BMS_TEST_OUTPUT = "$env:LOCALAPPDATA/CodexTemp/bms-onboarding/host"
python tests/run_host_regression.py
```

runner 的产品分配以 `tests/validation_catalog.py` 为准；新增 `*_check.py` 漏登记即失败。默认报告保存到用户 Documents/CodexOutputs，显式输出目录必须在源码树外且为空。`report.md`、`report.json`、`junit.xml`、`results.json` 和逐项日志共同留证；不要固定宣称永远是某个测试组数。使用 `--baseline <旧 report.json>` 比较配置/行为，`--with-targets` 合并开发 ELF/resource 验证；完整范围和边界见 [多产品自动化验证](AUTOMATED_VALIDATION.md)。单个脚本用环境选择产品，`bms.py --product` 不会传递给后续独立 Python 进程：

```powershell
$savedProduct = $env:BMS_PRODUCT
try {
    $env:BMS_PRODUCT = 'd014'
    python tests/d014_configuration_contract_check.py
    if ($LASTEXITCODE -ne 0) { throw 'D014 contract 失败' }
} finally { $env:BMS_PRODUCT = $savedProduct }
```

| 修改模块 | 获准测试时的优先检查（明确要求完整范围时使用完整 runner） |
|---|---|
| 产品/板级 | `tooling_contract_check.py`、对应 integration/profile/board checks |
| 软件保护 | `core_contract_check.py`、`sw_temperature_groups_host_check.py` |
| AFE 参数/恢复 | `afe_hw_*`、所选后端 recovery/sleep tests |
| 参数/存储/OTA 分组更新 | `storage_host_check.py`（四产品公共存储）、`flash_quick_check.py`、Modbus 检查 |
| SOC | `core_contract_check.py`、`soc_scenarios_host_check.py`、对应方向/open-wire tests |
| 日志/协议 | `diagnostics_host_check.py`、`modbus_host_check.py` |

## 3. 可移植核心与静态分析

```powershell
$coreBuild = "$env:LOCALAPPDATA/CodexTemp/bms-onboarding/core"
cmake -S . -B $coreBuild -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build $coreBuild
ctest --test-dir $coreBuild --output-on-failure
python bms_tools/bms.py --product d014 static --no-report
```

CMake 使用本机 C 编译器，target 范围以根 `CMakeLists.txt` 为准，不生成 Telink 固件，也不证明 SOC/协议已完全脱离 SDK。静态分析要看 severity、coverage gap、实际产品宏和未执行项，不能只看退出码。Windows `env` 自检仍带 Windows 工具路径要求；Linux 以 CI 中官方容器和 `TC32_BIN` 配置为准。

## 4. 生产配置的 ELF 验证

从已提交的干净工作区执行，先移除实验 `EXTRA_DEFINES`；生产模式在公共 core 使用 `-Os`，SDK/AFE/平台保持 `-O2`：

```powershell
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp link --jobs 4
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp resources
```

示例只验证 D008 16S。涉及全部 D008 profile 时，再分别对 `20s-nmc`、`24s-lfp` 执行 `--product d008 --production --d008-profile ... link` 和 `resources`。生产 trace/raw 诊断关闭，相关能力位和窗口按 [日志说明](RUNTIME_DEBUG_LOG.md) 处理。生产要求至少 8 KiB Flash 余量，并拒绝 dirty/空 Build ID、测试开关或保护关闭。不能用 `EXTRA_DEFINES` 伪造生产/Build ID/profile。

优化由 `bms_tools/build.mk` 的 `CFLAGS_BASE` / `CORE_OPT_FLAGS` 和 `bms.py` 生成的逐源规则确定：

| 模式 | `bms/core/*.c` | app / AFE / 平台 / SDK 源码 |
|---|---|---|
| 开发（未传 `--production`） | `-O2` | `-O2` |
| 生产（`--production`） | `-Os` | `-O2` |

生产 core 命令先包含基础 `-O2`，后追加 `-Os`，以最后一项为准。其余参数保持 SDK 示例的 `-ffunction-sections -fdata-sections -Wall -fpack-struct -fshort-enums -finline-small-functions -std=gnu99 -fshort-wchar -fms-extensions`，链接使用 `--gc-sections` 及原 Vendor 库。相比本机 B85 SDK 示例的全 `-O2`，生产 core 的 `-Os` 是本仓库明确差异；启动也按实际 TLSR8251 使用 `MCU_STARTUP_8251` / 32 KiB SRAM，不能照搬示例 8258 配置。这些源码选项不会重新优化预编译 Vendor `.a` 的内部代码。本轮简化没有更改这些设置。

## 5. 仅明确需要镜像时

`release` / `build` / `rebuild` / `objcopy` / **`check-fw`** 会生成或重新生成 BIN；`ci` 串联镜像流水线，也不是无镜像测试入口。`map`/`manifest`/`verify` 属于已有 BIN 的验收链，日常 ELF 使用 `resources`。

生产镜像先要求各产品 `BMS_PRODUCT_RELEASE_APPROVED=1`，再检查专项签核：D008 全部 profile 要求 `BMS_D008_SCD_POLICY_APPROVED=1`；20S NMC 还要求 `BMS_D008_20S_NMC_PROTECTION_APPROVED=1`；D013 要求 `BMS_D013_HW_CONFIG_APPROVED=1`。2026-10-08 用户明确要求修改门禁并编译 D008 16S BIN，已将 D008 整体批准和现有 SCD 策略批准置 1，用于授权镜像生成；没有据此补写参数或实板验收通过结论，仍按 [硬件验收](HARDWARE_VALIDATION.md) 独立记录。D008 20S NMC 专项及 D011/D013/D014 批准保持 0。批准值须经受控提交，不能由 `EXTRA_DEFINES` 覆盖。`link/resources` 允许检查未签核工程配置，不构成发布签核。

本次只生成 D008 16S LFP，使用单产品命令；`--all-products` 仍会被其他产品的批准门拦截：

```powershell
python bms_tools/bms.py --product d008 --production --d008-profile 16s-lfp release --jobs 4
```

获准验证时按范围选择 `link`（开发验证）、`test`（host）、`static`（静态分析），明确需要镜像并完成签核后用 `release` 串联 clean/build/check、manifest、verify。`--product d014 test` 选择 D014，`--all-products test` 只执行一次统一四产品报告。

以已经确认的 D014 生产交付为例，保持每条命令产品/模式/profile 一致：

```powershell
python bms_tools/bms.py --product d014 --production release --jobs 4
```

只交付 canonical `.bin`，不把 `.raw.bin` 当 OTA 镜像。归档完整 SHA、产品/profile、工具版本、更新组及执行标识、BIN hash、manifest、ELF/MAP/resources 与测试日志；另按 [硬件验收](HARDWARE_VALIDATION.md) 关闭实板项目。`flash-help` 只提供说明，生成镜像不等于烧录或 OTA 成功。

### 一次生成多个产品

完成对应产品签核后，下面一组命令批量生成 D008 16S LFP、D011、D013、D014 的生产镜像并完成清单校验：

```powershell
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp rebuild --jobs 4
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp manifest
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp verify
```

四产品依次构建，每个产品内最多四个 Make job；各输出目录独立。D008 16S 输出在 `firmware/production-16s-lfp/d008/`，其他三个在 `firmware/production/d011/`、`d013/`、`d014/`。如需全部六配置，再分别对 D008 `20s-nmc`、`24s-lfp` 执行同样的 `rebuild` / `manifest` / `verify`，输出在相应 profile 目录。相同产品/模式/profile 保留最近一次结果，不并发两次构建。最终 BIN 只在 checker 和 CRC 通过后发布；发布新 BIN 会移除旧 manifest，随后重新执行 `manifest`。

## 6. Ctrl+Shift+B 单项目配置与编译

范围依据：DVC1124-2 DS V1.1 原 PDF 第 1 页支持 4～24 串且电池包不超过 100V，故 4.20V NMC 上限为 23 串；SH36735XX CV1.0A 原 PDF 第 8 页规定 SH3673510 为 4～10 串、SH3673520 为 4～20 串，连续使用 VC1..VCN，未使用输入不得悬空。入口见 [AFE 资料指南](AFE_REFERENCE_GUIDE.md)。

打开仓库根目录或 `bms.code-workspace`，按 **Ctrl+Shift+B**，进入默认任务 **BMS: 选择配置编译并发送 OTA**。常用配置全部在一个窗口内完成，不需要输入命令；已移除批量勾选和重复的单型号 OTA 入口。

| 窗口项目 | 使用方式 |
|---|---|
| 板型 | D008、D011、D013、D014，单次选择一个；D008 固定 DVC1124-2；SH 板型可选择 3510 或 3520 |
| AFE 型号 | SH 板型独立选择 SH3673510 / SH3673520，复用同一套寄存器驱动；必须与实装芯片相符 |
| 构建模式 | 开发 / 生产；生产仍须满足干净提交、产品批准和资源门禁 |
| 串数 | 可编辑：D008 LFP 为 4～24 串，NMC 为 4～23 串；SH3673510 为 4～10 串、SH3673520 为 4～20 串 |
| 电池类型 | 四产品均可选磷酸铁锂或 4.20 V 三元锂；D008 切到 NMC 时串数上限自动降至 23 |
| 默认容量 | Ah，步进 0.1 Ah；未改时读取产品头文件，后续记住本机选择 |
| OTA 后更新所选组 | 软件保护、AFE 硬件保护、容量/加热/均衡、SOC 配置四个勾选；全不选保留，勾选组恢复本次固件默认 |
| 更换电池：重置 SOC / 循环 | 单独选择 SOC_STATE；恢复源码初始 SOC、放电累计和循环，不代表真实电量已校准 |
| 更新标识 | 工具自动生成并编入 BIN；界面无需编号，不读取客户旧编号 |
| 清理后全量重编译 | 默认不勾选，使用增量编译 |
| 生成后发送安卓并请求 OTA | 取消勾选时只生成并校验 BIN，无需手机；勾选时编译成功后发送当前镜像并请求 OTA |

修改 AFE、串数或电池类型时自动选择四组 Config；修改容量时只选择业务组，可手动调整。全不选时健康同拓扑设备保留参数，不依据旧编号覆盖。均衡、单体/总压和 SOC 曲线自动关联类型，总压按有效串数计算。SH 均衡入口使用 32 位掩码，支持第 11～20 串；D013 均衡能力仍关闭。详见 [配置指南](CONFIGURATION_AND_BUILD_GUIDE.md) 和 [OTA 参数策略](OTA_PARAMETERS.md)。

**更换串数或持久电池类型时必须选齐四组 Config，否则固件保持启动输出禁止；校准、SN 和事件保留。** 可选范围以实装 AFE 型号为准，3510 不因共享驱动而支持 20 串。串数须匹配接线，未使用采样输入按手册处理；D014 原 8S 板改为 9～20S 的接线未确认。菜单不修改生产批准值。

本机设置写在 `%LOCALAPPDATA%/BmsBuildMenu/<repoHash>.json`，按板型只记住 AFE、串数、类型和容量。每次打开更新勾选默认关闭，旧设置中的编号不再使用；换电脑不影响更新判断。每次明确选择更新并编译产生新随机标识，重复交付同一个 BIN 保持原标识。当前每域记录最后一次执行标识，回刷其他更新包可能重新恢复该包选中的参数；详见 OTA 参数策略。

点击“开始编译”依次执行 build/rebuild、manifest、verify。AFE、串数、类型、容量、更新范围和一次性标识写入输入收据与 manifest；发送前核对配置、路径、BIN 大小及 SHA-256。失败停止，不发送旧 BIN；发送阶段复用同一标识，仅重新验证。输出为 `firmware/<mode>-<afe型号>-<串数>s-<lfp或nmc>/<product>/`，旧命令未覆盖串数时保留原目录；同一目录不并发构建。

Sender 默认位置为 `%USERPROFILE%/Documents/CodexOutputs/telink-new-sdk-b85/android-direct-sender-v3/BmsTool.Android.Sender.exe`，其他电脑可通过环境变量 `BMS_ANDROID_SENDER` 指向原 Sender。手机沿用原无线调试授权及连接条件；安卓 App 需连接明确的目标 BMS。流程核对镜像身份，没有新增设备型号读回检查；Sender 成功代表发送及 OTA 请求完成，最终升级结果以 App 为准。

D008 窗口使用独立的 `custom` 编译 profile，旧三个 profile 保留兼容。现有 D008 16/24S LFP 生产签核范围保持；20S NMC 仍需要原专项批准，其余新组合仅开放开发构建，生产需另行源码签核。SH 产品仍受原生产批准门限制；菜单不自动提交、不批准、不降级为开发模式。已有源码脏修改也会阻止生产构建。镜像及 manifest 不提交 Git。

快捷键目标名称保持不变，仓库提供 [快捷键示例](../.vscode/keybindings.example.json)。若界面仍是旧版，关闭旧窗口并重新按快捷键，必要时执行 `Developer: Reload Window`。此入口需要 Windows PowerShell 5.1 / WinForms 和原 TC32 / Python 环境。2026-10-10 本次参数组窗口未运行；此前旧窗口启动/控件读取不作为本版本验证。本次未执行 host、固件编译、镜像生成或 OTA。

## 7. 常见失败

| 现象 | 排查顺序 |
|---|---|
| TC32/Make 找不到 | `env` → `TC32_BIN`/PATH → 官方工具与 Vendor 库；不要改用 host GCC |
| host 找不到 `cc` 或运行 DLL | `CC`、编译器 bin 的 PATH；查看对应测试日志 |
| source manifest 失败 | 本产品 `sources.txt`、重复/遗漏/错误后端；变更集合后才更新清单 |
| link 看似成功但命令失败 | `gen/build.log` 的 warning 门禁或生产资源门禁；不要压制警告绕过 |
| resources 报收据陈旧 | 同产品/模式/profile 重新 link；源码、宏、Git 身份变化都可能使输入过期 |
| production 拒绝 | `git status --short`、有效 Build ID、D008 显式 profile、实验宏、8 KiB 余量 |
| 找不到 BIN | `link` 本就不生成；确认任务明确要求镜像后才使用第 5 节 |
| 静态分析要本机模板 | 使用 `static --no-report`，保留机器可读结果 |

文档修改默认审查链接、源码引用和 Git 差异；文档 contract 等检查脚本也只在用户要求测试时执行。本机结果不等于远端 CI，host/ELF 结果不等于实板 MOS、Flash 掉电或低功耗验证。
