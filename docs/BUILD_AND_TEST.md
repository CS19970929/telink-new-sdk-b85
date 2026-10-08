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
| 参数/存储/更新编号 | `storage_host_check.py`（四产品公共存储）、`flash_quick_check.py`、Modbus 检查 |
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

只交付 canonical `.bin`，不把 `.raw.bin` 当 OTA 镜像。归档完整 SHA、产品/profile、工具版本、参数更新编号、BIN hash、manifest、ELF/MAP/resources 与测试日志；另按 [硬件验收](HARDWARE_VALIDATION.md) 关闭实板项目。`flash-help` 只提供说明，生成镜像不等于烧录或 OTA 成功。

### 一次生成多个产品

完成对应产品签核后，下面一组命令批量生成 D008 16S LFP、D011、D013、D014 的生产镜像并完成清单校验：

```powershell
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp rebuild --jobs 4
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp manifest
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp verify
```

四产品依次构建，每个产品内最多四个 Make job；各输出目录独立。D008 16S 输出在 `firmware/production-16s-lfp/d008/`，其他三个在 `firmware/production/d011/`、`d013/`、`d014/`。如需全部六配置，再分别对 D008 `20s-nmc`、`24s-lfp` 执行同样的 `rebuild` / `manifest` / `verify`，输出在相应 profile 目录。相同产品/模式/profile 保留最近一次结果，不并发两次构建。最终 BIN 只在 checker 和 CRC 通过后发布；发布新 BIN 会移除旧 manifest，随后重新执行 `manifest`。

## 6. VS Code 一键编译、无线发送与 OTA

### 快捷键与配置选择

常规配置为 **6 种装配 × 开发/生产 2 种模式，共 12 种**。模式的优化与诊断差异见第 4 节；增量/全量编译只改变构建方式，不增加固件配置种类。

| 装配 | 串数/体系 | 开发 BIN | 生产镜像批准状态 |
|---|---|---|---|
| D008 16S LFP | 16S 磷酸铁锂 | 可选择 | 已开放镜像生成 |
| D008 20S NMC | 20S 三元锂 | 可选择 | 专项批准未开放 |
| D008 24S LFP | 24S 磷酸铁锂 | 可选择 | 已开放镜像生成 |
| D011 | 10S | 可选择 | 整体批准未开放 |
| D013 | 4S | 可选择 | 整体/硬件配置批准未开放 |
| D014 | 8S | 可选择 | 整体批准未开放 |

本表描述源码门禁状态，不代表这 12 种配置都经过本次实际构建或实板验收。日志等级、保护台架开关和其他 `EXTRA_DEFINES` 属于额外自定义配置，不计入这 12 种常规配置，菜单不修改这些宏。

| 快捷键 | 用途 | 操作 |
|---|---|---|
| `Ctrl+Shift+B` | 所有产品开发/生产配置的编译与 OTA | 单选自动编译校验、无线发送并请求 OTA；多选生成后选择一份用于 OTA |

`Ctrl+Shift+B` 统一绑定默认任务 **BMS: 选择配置编译并发送 OTA**，本机原有的直接快捷键覆盖也已同步更新；原 `Ctrl+Alt+B` 作为兼容别名保留。Windows 勾选窗口由 VS Code 启动，支持任意多个配置混选、“全部开发”“全部生产”和“清空选择”；默认只选 D008 16S production。点击“编译并 OTA”后执行流程，可勾选“清理后全量重编译”，默认使用增量编译。取消窗口不构建、不发送。

批量构建顺序执行；每项独立执行 build/rebuild、manifest、verify，并核对模式/产品/profile/路径与 BIN 大小及 SHA-256。单项失败继续其他项，末尾分别列出成功/失败项，任一失败则任务退出码非 0。旧 BIN 可能仍在磁盘，只以本次成功校验项作为有效结果。输出按 `firmware/<mode-profile>/<product>/` 分开，开发和生产不会互相覆盖。同一 checkout/模式/profile/产品仍不得与其他构建任务并发。

只选一个配置且构建成功时，自动调用 `--send-only`：重新验证当前镜像后发送到安卓并请求 OTA，不重复编译。选择多个配置时，生成完毕后弹出“选择本次用于 OTA 的固件”，仅列本次成功项；用户选择与当前连接 BMS 相符的一份并点击“发送并 OTA”。即使批量中只有一项成功也要求明确选择，不将不同型号/模式的 BIN 连续刷到同一设备；取消后保留生成的 BIN。全部失败不发送。脚本 `-BuildOnly` 可用于仅构建验证，`-PlanOnly` 仅输出计划，均不连接手机。

VS Code 的自定义快捷键必须放在用户级配置，本仓库提供 [快捷键示例](../.vscode/keybindings.example.json)；换电脑时可将其条目加入 `Preferences: Open Keyboard Shortcuts (JSON)`，或通过 `Tasks: Run Task` 直接选择上述任务，不必写构建命令。其他电脑需要 Windows PowerShell 5.1/WinForms 和原 TC32/Python 环境。生产批准、干净提交和资源门禁继续适用，菜单不会自动提交或批准。

### 单个型号一键 OTA

打开仓库根目录或 `bms.code-workspace`，从 `Tasks: Run Task` 选择 **BMS: 编译并发送固件到 Android**。该任务作为单独入口保留，弹出型号/装配选择框，包含 D008 的三个 profile 及 D011/D013/D014，默认 D008 16S LFP。

选定后，`bms_tools/android_ota.py` 显式指定产品、profile 和 `--production`，依次执行增量 `build --jobs 4`、`manifest`、`verify`。所有步骤成功且发送前产品/profile/路径、大小与 SHA-256 匹配，才将当前 `firmware/<mode-profile>/<product>/825x_ble_sample.bin` 交给既有 Android Sender 的 `--firmware ... --auto-ota`。任何构建、批准或校验失败都停止，不发送旧 BIN；取消选择不会启动任务。

本机 Sender 默认位置为 `%USERPROFILE%/Documents/CodexOutputs/telink-new-sdk-b85/android-direct-sender-v3/BmsTool.Android.Sender.exe`，其他电脑用环境变量 `BMS_ANDROID_SENDER` 指向安装好的同一 Sender。Sender 源码仍在 Windows 上位机维护分支；本流程不复制上位机源码，不再依赖旧 `send-from-vscode.ps1` 中的 SDK 输出路径。本机用户级旧编译发送任务已取消默认标记，避免两个默认任务使快捷键先弹出任务选择；手动选择 BIN 和连接无线调试任务保留。默认配置选择入口已包含自动发送和 OTA；原 production 单型号任务也保留。

手机须与电脑具备原无线调试条件并已授权；原 Sender 负责重连及传输。安卓 App 已连接明确 BMS 时自动 OTA，否则由 App 选择/连接设备后升级。所选型号须与实际连接的板卡一致，当前流程核对的是构建镜像身份，没有新增设备型号读回检查。Sender 成功退出代表发送及 OTA 请求完成，最终升级结果以 App 为准。

生产批准门继续适用：当前 D008 16S/24S 可生成镜像，D008 20S NMC 与其他产品未批准时停止。本任务不会自动改批准值、提交脏源码、绕过资源/保护门禁或降为开发模式。production 要求先提交代码；镜像及 manifest 不提交 Git。

若 VS Code 尚在使用旧任务，执行 `Developer: Reload Window`，再用 `Tasks: Configure Default Build Task` 选择 **BMS: 选择配置编译并发送 OTA**。也可直接运行 OTA 任务的脚本：

```powershell
python bms_tools/android_ota.py --target d008-16s-lfp
# 只验证构建和镜像，不连接手机：
python bms_tools/android_ota.py --target d008-16s-lfp --build-only
```

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
