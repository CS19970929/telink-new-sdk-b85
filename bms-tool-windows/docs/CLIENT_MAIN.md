# BMS 上位机与 App 统一开发指南

更新：2026-11-17。客户端源码整合基线：`0a9eb6bcc152349ef3b80c9a70655a1922b07a08`；后续文档/测试入口提交在同一分支持续追加。

## 唯一日常开发入口

| 项目 | 固定入口 |
|---|---|
| 仓库 | `CS19970929/telink-new-sdk-b85` |
| 长期客户端分支 | `client/main` |
| 本机工作目录 | 本仓库的 D 盘主目录，见下方命令；不使用额外客户端 worktree |
| 客户端源码 | 主目录中的 `bms-tool-windows/` |
| Windows 客户版 / 内部版 | `BmsTool.Windows/` / `BmsFactoryTest.Windows/` |
| Android Customer / Internal | 同一个 `BmsTool.Android/`，由 `BmsAppEdition` 选择 |
| CLI / 公共基础 | `BmsTool.Cli/` / `BmsTool.Core/`、`Shared/` |

今后适配新的 MCU、AFE、BMS 项目、串口或蓝牙时，继续在这条客户端主线或用户明确指定的已有功能分支增加适配。当前任务分支为 `feature/ui-unified-monitor-lishen-20261116`，尚未合回 `client/main`。同一仓库的固件与客户端可在主目录依次切换分支；其他独立项目仍保留各自目录。选择目标固件的现有协议/能力完成客户端适配，不为每块板复制客户端或创建永久分支。仓库名称包含 Telink 不限制客户端复用范围。

2026-11-17 按用户选择将上位机迁回主目录，并移除 `C:\Users\Administrator\source\bms-client` linked worktree。原分支、用户保存的 Keil 改动和本地文件均保留；旧 D 盘未跟踪上位机副本已单独归档。本轮仅移除这个客户端工作树，其他工作树未清理。下方 2026-11-14 的来源和验证记录仍是当时的历史证据。

开工执行：

```powershell
Set-Location 'D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)'
# 日常 client/main 使用默认参数；当前已获授权的功能分支使用：
pwsh -NoProfile -File bms-tool-windows/check-client-worktree.ps1 `
    -ExpectedBranch feature/ui-unified-monitor-lishen-20261116
# 切回 client/main 后使用：
# pwsh -NoProfile -File bms-tool-windows/check-client-worktree.ps1
```

此入口只检查主工作目录、预期分支、HEAD 内的客户端工程与工作区，不创建/切换分支。默认要求 `client/main`；只有用户明确指定功能分支时传入对应名称，不能用当前分支名自动填充来绕过核对。根 `AGENTS.md` 记录本次迁移后的入口。其他电脑使用各自仓库主目录即可，本机路径不是源码硬编码。仓库远端默认分支不代表客户端主线，远端状态按需现场核实。

切换分支前先保存并关闭当前工程，提交或妥善备份本分支未提交修改，再执行 `git switch <已有分支>`。切换影响整个仓库的受跟踪文件，不只是上位机目录；不要让 Keil 或编辑器将旧分支内存内容写回新分支。当前已迁移的 Keil 工程位于 `firmware/comm_tool_f103ret6/keil/COMM_TOOL_F103RET6.uvprojx`。

## 本轮整合来源

在原通用客户端分支 `feature/windows-afe-hw-protection-editor-v2@31838f47` 上依次收回进度，最后将该分支重命名为 `client/main`。本地分支总数保持 86，没有新增长期客户端线。

| 已有来源 | 收回内容 | 本轮提交 |
|---|---|---|
| 通用分支 `31838f47` | Windows BLE 搜索、历史缓存区分、连接串行化及取消修复 | 原基线，保留 |
| `codex-d015-ymodem-host@13682956` | D015/C060 客户端、统一升级包与 YMODEM、Android 0.9.1 | `2b20243f` |
| `codex-c077-host-v3@f0df5af7` | C077 双 AFE 参数、诊断和内部维护入口 | `0cd90114` |
| `codex-dev-event-log@e5d0b716` | 新事件历史协议、解析、Windows 界面与导出 | `0fd9d328` |
| `codex-soc-host-config-20261020@b2ca53c4` | SOC/AFE 参数能力、Telink 串口升级 | `433e7d43` |
| `codex-f030-3520-host-compat@2d7ae385` | 旧 HS-D015 / 3520 参数和严格识别的旧升级兼容 | `5af0b255` |
| `codex-c11-fault-injection-20261027@1e90d970` | 仅移植客户端故障注入和 C11 内部验证，不合入其固件 | `5eb9fba8` |
| `audit/client-reliability-20261001@cdd1797b` | 连接取消、参数快照归属、旧异步结果失效 | `891559d3` |

`codex-d015-host-compat-20261028` 已被 D015 YMODEM 线包含。旧 `codex-c077-host@4e859c48` 的只读 commissioning 检查以 v3 现有 `afe309 check` / schema-2 诊断为准，不将旧合同覆盖 v3。旧独立 Android/OTA 应用不再作为基础。

合并冲突按实际协议与现有行为处理：保留新 BLE 发现修复、参数整组事务、3520 原语义和双 AFE 维护；客户/内部版共用升级分流；通用设备不因 C11 内部验证功能自动发送专用故障注入探测。旧 HS-D015 的 23K 布局与后续标准升级合同分开识别，不凭设备名称猜布局。

最终兼容性回归还定位并修复了 C11 导入时将变长响应规则误用于 0x42 AFE 寄存器的问题，修复提交为 `0a9eb6bc`。计数长度规则只作用于 0x41；既有 AFE 固定帧和 SOC HIL 合同保持原样。诊断/AFE 单元入口改为编译生产 Core 的完整源清单，避免漏掉新部分实现。对应 AFE 分片、寄存器 CAS/丢 ACK、SOC HIL 与 C11 回归通过。

2026-11-14 整合时保留了旧功能分支、临时工作树与历史发布包供追溯；旧临时通用工作树停留在 `31838f47` 的历史快照。后续日常入口以本文顶部的主目录和分支规则为准，不回到旧目录继续开发。

## 功能与平台边界

统一的是源码、进度和后续开发入口，不能据此宣称每个平台的专用界面已经相同。

- Windows 两版共用实时数据、串口/BLE、参数和升级等公共能力；内部维修、C11 故障注入/验证、工厂配置与客户服务权限仍保持各自边界。
- Android 沿用当前 0.9.1 客户/内部变体，包含已有 C060 适配、扫码、服务权限和升级分流，引用同一 Core。C077 专用双 AFE 维护页、新事件历史等桌面专用页面不能视为已全部移植到手机。
- CLI 复用公共合同，保留能力查询、参数、诊断、升级和 C077 维护等入口；JSON schema 与 exit code 兼容要求继续有效。
- Core 当前链接部分 `BmsTool.Windows/` 下的协议源码。不要为了目录外观另抄实现，后续按实际需要小步整理即可。
- 新 BMS 优先按能力扩展。只有设备实际实现了兼容合同才复用对应事务；客户端统一不授权修改固件协议、存储或 Boot/App 布局。

## 构建与验证

所有中间文件放到源码树外。以下仅构建客户端，不构建固件：

最终安装包按用户2026-11-17要求直接交付到项目根 `releases/`：APK 位于 `releases/bms-tool-android/<日期-功能-提交>/<customer|internal>/`，EXE 位于 `releases/bms-tool-windows/<发布标识>/<customer|internal|cli>/`。TEMP 仅用于缓存、日志和构建中间文件，不作为最终安装包入口。发布时保留版本、源码提交及 SHA256；手机安装使用同一发布目录中的 APK 并核对手机实际安装包，历史包不覆盖。

```powershell
$out = Join-Path $env:LOCALAPPDATA 'CodexTemp/bms-client/current'
foreach ($project in @('BmsTool.Windows','BmsFactoryTest.Windows','BmsTool.Cli')) {
    dotnet build "bms-tool-windows/$project/$project.csproj" -c Release `
        --artifacts-path (Join-Path $out 'desktop') -p:NuGetAudit=false
    if ($LASTEXITCODE -ne 0) { throw "$project build failed" }
}
# 同一源码分别选择客户/内部 Android 版；本机 SDK/JDK 路径按实际环境配置。
foreach ($edition in @('Customer','Internal')) {
    dotnet build bms-tool-windows/BmsTool.Android/BmsTool.Android.csproj -c Release `
        --artifacts-path (Join-Path $out "android-$edition") -p:BmsAppEdition=$edition `
        -p:AndroidSdkDirectory=D:\telink\android_env\android-sdk `
        -p:JavaSdkDirectory=D:\telink\android_env\jdk\jdk-17.0.19+10 -p:NuGetAudit=false
    if ($LASTEXITCODE -ne 0) { throw "Android $edition build failed" }
}
```

2026-11-14 的本轮结果：

| 验证层 | 结果 |
|---|---|
| Windows 客户/内部、CLI Release | 全部通过，零警告零错误 |
| Android Customer / Internal Release | 全部通过，零警告零错误 |
| 实际 WPF 生命周期 | 每版 15 项，进行中/排队取消、写入串行化、快照归属与迟到结果 |
| C060 / C077 / 保护整组 | 150 字段合同、65 字保护分组、C077 58 项及跨项目检查通过 |
| 两版 AFE 与业务参数 UI | 开关、范围、隐藏值保留、保存资格、设备页切换通过 |
| BLE、事件历史、SOC/AFE、旧 HS-D015 | 各对应 host 回归通过，旧 HS 发送器 50 项与参数编码 17 项 |
| 升级与内部验证 | Telink OTA/串口升级、41 项升级包/能力检查、C11 9 项通过 |
| Android 与 CLI 离线 | 升级端点 24 项、服务权限 17 项、扫码解析 52 项及 CLI JSON/比较报告通过 |

客户端 CI 已配置为监听 `client/main`，执行 Windows/CLI 编译与 host/UI 回归，构建/发布中间文件位于 runner 临时区；Android 两版在本轮本地验证。因尚未推送，本轮没有远端 CI 运行结果。

完整日志、构建产物与截图在本机 `$env:LOCALAPPDATA\CodexTemp\bms-client-unification-20261114`；正式摘要与输入程序集指纹见 [本轮证据](CLIENT_MAIN_EVIDENCE_20261114.json)。编译产物不提交为源码。Android 版本字段仍为 0.9.1 / 16，本轮没有重新发布、安装或交付同版本客户包。

未连接手机或 BMS，未验证实际串口/BLE 升级、OS 切换时序、持久化与硬件动作；这些需使用对应项目的设备单独验收。未改固件，未执行固件构建或烧录。原审计证据和 README 各日期的现场记录仅证明当时版本，不是此次设备验证。

## 后续日常规则

1. 在仓库主目录核对 `client/main` 或用户明确指定的已有功能分支、HEAD 与 dirty，再开发；不自动创建客户端 worktree。
2. 新项目作为现有客户端适配增量，客户/内部和 Windows/Android/CLI 按能力共用公共实现。
3. 跑相关 host/UI 检查与平台编译，在同一分支分阶段提交，记录源码和发布物版本。
4. 未获明确授权不创建分支、不整体合固件分支、不删除历史、不强推。正式发布才按现有流程产出安装包并记录源码 SHA。

本轮完成的是本地客户端主线；尚未推送远端，也未修改 GitHub 默认分支。后续同步使用这条 `client/main` 的首次正常推送建立同名跟踪，不能再把增量推回旧 feature/产品分支。远端保护规则尚未配置，开工检查与 AGENTS 是本地约束，不能冒充服务器强制门禁。

## 2026-11-17 C077新项目适配

本轮继续`feature/ui-unified-monitor-lishen-20261116`，复用主目录，不新增分支/worktree。C077新固件提供独立mA/ppm电流校准、30秒端口租约和新鲜转换tick；Windows/Android共用两点采集、仅校零点、保存后公式验证与本次连接恢复。C060/D008/D015已有合同继续分别检测。C077通用基本参数14项，删除所有旧K/B，软件版本只读；参数包schema2拒绝旧格式包。固件新存储7/540由C077项目定义，不迁移旧配置。

C077原始BIN经过固件产品/地址/容量/向量/长度检查后使用现有YMODEM；已有Telink升级分流不变。Android固件页新增可选“LK8353模块·19200波特率优化”：有响应≤120字节写入，按UART排空节流；默认标准20字节。该选项不改模块/Boot的波特率，也不用于Telink无线OTA。不用无响应突发或240字节。客户版保留服务授权，内部版仍保留额外诊断。

正式交付Windows两版/CLI和Android两版放项目`releases/`，缓存/测试证据放TEMP；手机安装以发布目录中同一APK为准。Android正式版本提升0.9.2/17。校准与OTA均不会自动编程309 EEPROM；实际传输和精度验收分别记录。
