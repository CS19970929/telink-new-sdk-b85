# BMS 上位机与 App 统一开发指南

## Windows 的 C073 与 Comm Tool

客户版和内部版共用四种连接方式。蓝牙和直连串口沿用既有流程。「一线通」连接 Comm Tool 后监控 A1.3.3 或锂神广播。「CAN」使用 Comm Tool 的 115200 8N1 串口，连接 C073 的 250 kbit/s CAN 总线，支持实时监控、参数读写、飞道广播和 CAN 升级。

C073 以 D170 升级身份和分区识别。C073002C 表示加固后的 44 KiB 布局，C073000D 继续识别历史 44 KiB 与 46 KiB。2C00 单片快照提供实测通道与有效位。「参数设置 → C073 基本参数」读取和保存容量、循环次数、SOC 电压端点和串数。整组写经 0x16 原子提交，操作后读回。SOC 满、空电压继续使用旧 CAN 上位机的 0x231A / 0x231B（mV）。Windows 的「SH367309 硬件保护 / BMS 参数」页读写单片 32 字档案（0x2B10），并逐项查看芯片回读。Android 保护页提供同一组 Flash 和芯片实际值的只读视图。旧 0x2100 窗口保留给工厂工具。电流显示为 2:6 分流器的名义安培数。保存后核对 MCU Flash 并确认 309 的 25 个配置字节。直连串口、BLE 和 Comm Tool CAN 共用该操作，BLE 的 32 字硬件档案在 MTU 23 下按 20 字节分片透传，C073 收齐并校验整帧才提交。分片间须小于固件的 100 ms 字节超时，整帧须在 500 ms 内接收。SOC 四字组可在 MTU 23 下单帧发送，一线通用于广播监控。SN 使用生产身份接口。客户版沿用对应操作的解锁要求。

升级使用 C073 格式 2 包或带身份的页对齐 APP。44 KiB 对应 APP 版本族 `0x0002xxxx`，旧 46 KiB 对应 `0x0001xxxx`。先核对 D170 分区，两个布局之间的首次切换须使用配套 IAP、APP 和工厂初始化。加固 44 KiB 使用 Comm Tool 0.2.11 或更新版的原生流程，历史 44 KiB 仍支持 0.2.10。识别到旧 Comm Tool 0.2.3、0.2.4、0.2.6、0.2.7 或 0.2.8 时，新版上位机可直接使用原格式 2 包，通过旧缓存分类 0x08004800 传输。C073 Boot 仍写入 0x08003000。兼容流程要求 CAN 直连单块 C073，缓存前和启动前都核对运行 APP 身份，升级后核对运行镜像。转接器中的 C073 缓存只用于 C073 单板，请勿用于其他板型的离线升级。0.2.9 和 0.2.10 使用原生流程，不满足镜像要求时可用直连串口或 BLE。同容量的两种 44 KiB 布局也须核对升级身份，切换时使用全量工厂初始化。44 KiB 设备已在 Boot 时，请用能读取 D170 的直连串口或 BLE 恢复。46 KiB 保留原 CAN Boot 恢复路径。升级后核对板号、分区、运行模式、版本、长度和 CRC32。先查询 Comm Tool 的 Flash 缓存。APP 地址、长度、CRC16、CRC32 和有效标志全部匹配时复用缓存。同一工位每次连接一块 C073，连续升级同一镜像只需首次写入缓存。缓存不匹配时从第一块开始按 48 字节写入，分块应答异常时核对缓存身份后最多重建一次，提交应答丢失时读取缓存的校验结果。每块板都先读 APP 身份来唤醒并核对板型。46 KiB 无 APP 应答时保留 CAN Boot 板号检查。发送 CAN 启动命令后查询传输状态，用户取消才发送中止命令。0x23 表示转接器在进入 Boot 前未能确认 C073 身份。


更新于 2026-11-18。下文保留客户端首次整合基线 `0a9eb6bcc152349ef3b80c9a70655a1922b07a08` 的来源和验证记录。

### 旧项目 CAN 升级兼容和安全边界

| 上位机 | Comm Tool 0.2.3 | Comm Tool 0.2.11 |
| --- | --- | --- |
| 原经典 CAN 上位机，旧项目 APP BIN | 原缓存和 CAN 命令 | 保留原缓存和 CAN 命令 |
| BMS Assistant，旧项目 APP BIN | 读回运行 APP 后使用原 CAN 命令 | 读回运行 APP 后使用原 CAN 命令 |
| BMS Assistant，当前 C073 `.c073` | 单板兼容流程 | C073 原生流程 |

BMS Assistant 先选「CAN」再选原项目 APP BIN。本次已核对 `103 + 309` 旧项目的 250 kbit/s、APP 地址 0、Boot 节点 1，APP 从 `0x08004800` 起。上位机按旧 Boot 的栈和复位向量规则检查文件，并以 `0x0801C000` 作为镜像结束上限，给该项目的参数、SOC 和日志留出空间。保留旧 CAN 的分块和 Flash 缓存协议。其他 CAN 地址、速率或 Flash 布局需按实际项目确认。

旧板升级前检查 `D170` 是否为独立升级协议，并读回 `C002` 的 SN、硬件和软件版本及 `D034` 运行状态。界面显示所选 BIN 与读到的设备身份，由操作者确认项目匹配和单板连接。缓存准备完毕后再次读身份，变化时终止。已进入 Boot 且无法确认 APP 身份时走项目原工装恢复。新上位机传输完成后核对原设备的 SN 和硬件版本，证据为 APP 恢复响应和读回软件版本。若填了预期软件版本，必须读回匹配。旧流程没有运行镜像 CRC32 窗口。

旧 `.bin` 和旧 Boot 缺少可强制对照的板号。已核对的旧 Boot 在 `START` 先擦除 APP 首页，之后才校验收到的镜像和向量。经典 CAN 上位机在读不到重启 APP 时会留下日志提示，同时显示传输完成。两种 Comm Tool 均会保留 Flash 缓存。切换板型后应按所选镜像核对缓存，旧转接器的离线按键流程缺少运行 APP 身份检查。旧项目 APP 还可能按构建的 `UpgradeParamPolicy` 对配置进行已声明的迁移，验收应包含参数读回。

当前 C073 在擦除前检查内嵌板号、地址、版本和向量，写完核对 Flash CRC32 后提交有效启动记录。BMS Assistant 重启后继续核对 `D170/D17C`。旧转接器路径依赖上位机前置核验与单板连接。两种布局的单 APP 烧写中断后可能需要恢复升级。旧项目组合按已知协议做主机回归，实际出货板、线束和掉电恢复须逐项目实测。


### C073 内部调试窗口

内部测试版首页「调试工具」打开非模态窗口，通过主窗已连接的 CAN、直连串口或蓝牙按需读取 C073 Debug 的只读 RTC 窗口。测试时可暂停 BMS 自动轮询，同时取消在途自动请求并暂停后台诊断、参数探测和自动日志读取。读取 RTC 记录属于手动请求，可能唤醒设备。关闭窗口恢复该窗口发起的暂停。新版 Debug 每次退出 RTC 只记一条，最近八条保存在 BMS RAM，复位清空。每条显示从 RUN 进入 RTC 到退出 RTC 的总时长，包含中间的定时巡检。旧 Debug 仍可读原因，时长显示为「—」。正常定时巡检继续睡眠不写入；因充放电、保护或故障退出时显示具体原因。内部版移除了「趋势与记录」「C11 软件验证」「故障注入」和「3520 参数」的页面入口。

## 唯一日常开发入口

| 项目 | 固定入口 |
|---|---|
| 仓库 | `CS19970929/telink-new-sdk-b85` |
| 长期客户端分支 | `feature/windows-classic-ui-20261118` |
| 本机工作目录 | 本仓库的 D 盘主目录，见下方命令；不使用额外客户端 worktree |
| 客户端源码 | 主目录中的 `bms-tool-windows/` |
| Windows 客户版 / 内部版 | `BmsTool.Windows/` / `BmsFactoryTest.Windows/` |
| Android Customer / Internal | 同一个 `BmsTool.Android/`，由 `BmsAppEdition` 选择 |
| CLI / 公共基础 | `BmsTool.Cli/` / `BmsTool.Core/`、`Shared/` |

今后适配新的 MCU、AFE、BMS 项目、串口或蓝牙时，继续在这条客户端主线或用户明确指定的已有功能分支增加适配。2026-11-18 用户将包含经典界面及此前适配的 `feature/windows-classic-ui-20261118` 定为长期主线。原 `client/main` 保留为历史分支。同一仓库的固件与客户端可在主目录依次切换分支；其他独立项目仍保留各自目录。选择目标固件的现有协议/能力完成客户端适配，不为每块板复制客户端或创建永久分支。仓库名称包含 Telink 不限制客户端复用范围。

2026-11-17 按用户选择将上位机迁回主目录，并移除 `C:\Users\Administrator\source\bms-client` linked worktree。原分支、用户保存的 Keil 改动和本地文件均保留；旧 D 盘未跟踪上位机副本已单独归档。本轮仅移除这个客户端工作树，其他工作树未清理。下方 2026-11-14 的来源和验证记录仍是当时的历史证据。

开工执行：

```powershell
Set-Location 'D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)'
pwsh -NoProfile -File bms-tool-windows/check-client-worktree.ps1
```

此入口只检查主工作目录、预期分支、HEAD 内的客户端工程与工作区，不创建/切换分支。默认要求 `feature/windows-classic-ui-20261118`。只有用户明确指定其他已有分支时，才传入 `-ExpectedBranch <分支名>`。根 `AGENTS.md` 记录本次迁移后的入口。其他电脑使用各自仓库主目录即可，本机路径不是源码硬编码。仓库远端默认分支不代表客户端主线，远端状态按需现场核实。

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

- Windows 两版共用实时数据、串口/BLE、参数和升级。客户版在首页启停长期记录并导出 CSV 或 Excel，设备事件日志按序号和事件名称显示和导出。趋势、历史蓝牙设备和 3520 维护页进入内部版查看。
- C060/C077 基本参数面向客户显示均衡与容量/SOC，C060 另显示低压休眠。均衡关闭压差对应 2302，和启动电压、启动压差整组保存。系统和电流/SOC 表参数由内部版维护。
- 高级功能的“硬件短路保护”按设备身份分流。C060 使用 2308 整组和 BQ76930 档位，RSNS 跨量程也会改变硬件 OCD 阈值。C077 按 AFE 分别保存 2B10/2B30 档案并经 2A7F 下发所选片，确认实际 EEPROM。实板配置与 MOS 波形需单独验收。
- C077 实时监控按设备身份探测只读快照 `2C00 / 103`。Windows、Android 与 CLI 共用解析及逐项状态，温度故障时继续显示有效电压和电流。旧固件沿用原有实时窗口。
- 设备以 2AF9.bit5 声明 RTC／低功耗诊断时，实时监控读取 FC03 D04F 高字节，非零显示 RTC／低功耗故障及 MOS 开启被禁止。低字节仍是单体压差诊断。未声明的旧设备沿用原轮询。
- 逐串均衡通过板型无关的能力位 2AF9.bit4 识别，支持时读取 2D00 的 5 个字。状态确认后按逻辑第 1～32 串显示，停止、读取失败或当前单体无效时隐藏图标。旧固件使用 D03D/D03E，已识别的 C060 分别按 10 串解码。
- Android 沿用当前 0.9.1 客户/内部变体，包含已有 C060 适配、扫码、服务权限和升级分流，引用同一 Core。C077 专用双 AFE 维护页、新事件历史等桌面专用页面不能视为已全部移植到手机。
- CLI 复用公共合同，保留能力查询、参数、诊断、升级和 C077 维护等入口；JSON schema 与 exit code 兼容要求继续有效。
- Core 当前链接部分 `BmsTool.Windows/` 下的协议源码。不要为了目录外观另抄实现，后续按实际需要小步整理即可。
- 新 BMS 优先按能力扩展。只有设备实际实现了兼容合同才复用对应事务；客户端统一不授权修改固件协议、存储或 Boot/App 布局。

### C060 / C077 硬件保护显示

设备在 `2AF9.bit6` 声明硬件保护详情。主页面三级保护使用 `2D10` 的软件状态和 `2D11` 的 AFE 保护原因。短路显示为「放电短路」，过压、欠压、放电过流及 C077 充电过流显示相应硬件字样。旧固件或详情读取失败时，继续使用已有的三级保护字。

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

当时的客户端 CI 监听 `client/main`。此次主线调整同步更新了本地工作流的分支过滤。工作流执行 Windows/CLI 编译与 host/UI 回归，产物在 runner 临时区。上表的 Android 验证为本地结果。

完整日志、构建产物与截图在本机 `$env:LOCALAPPDATA\CodexTemp\bms-client-unification-20261114`；正式摘要与输入程序集指纹见 [本轮证据](CLIENT_MAIN_EVIDENCE_20261114.json)。编译产物不提交为源码。Android 版本字段仍为 0.9.1 / 16，本轮没有重新发布、安装或交付同版本客户包。

未连接手机或 BMS，未验证实际串口/BLE 升级、OS 切换时序、持久化与硬件动作；这些需使用对应项目的设备单独验收。未改固件，未执行固件构建或烧录。原审计证据和 README 各日期的现场记录仅证明当时版本，不是此次设备验证。

## 后续日常规则

1. 在仓库主目录核对 `feature/windows-classic-ui-20261118` 或用户明确指定的其他已有分支、HEAD 与 dirty，再开发；不自动创建客户端 worktree。
2. 新项目作为现有客户端适配增量，客户/内部和 Windows/Android/CLI 按能力共用公共实现。
3. 跑相关 host/UI 检查与平台编译，在同一分支分阶段提交，记录源码和发布物版本。
4. 未获明确授权不创建分支、不整体合固件分支、不删除历史、不强推。正式发布才按现有流程产出安装包并记录源码 SHA。

此处的主线是客户端日常开发入口。涉及远端同步、默认分支或分支保护时，先核对对应仓库的实际状态。

## 2026-11-17 C077新项目适配

本轮继续`feature/ui-unified-monitor-lishen-20261116`，复用主目录，不新增分支/worktree。C077新固件提供独立mA/ppm电流校准、30秒端口租约和新鲜转换tick；Windows/Android共用两点采集、仅校零点、保存后公式验证与本次连接恢复。C060/D008/D015已有合同分别检测。C077通用基本参数14项，删除全部旧K/B，软件版本只读；参数包schema2拒绝旧格式。固件新存储7/540不迁移旧配置。

C077原始BIN经产品/地址/容量/向量/长度检查后走现有YMODEM；Telink升级独立分流。Windows的LK8353有响应120字节节流已通过完整实板升级，含复连和CRC共74.882秒。Android首轮120字节优化首块连续重试，不能把电脑验证结果套用手机；当前0.9.5/20移除该优化入口，恢复原有20字节/15ms标准发送，不修改Windows和Telink。Android提速与完整升级验收仍未完成，不按编译成功交付为已验证提速功能。

Android已实机完成C077连接、通用参数读取、新鲜样本采集及取消。按设备能力隐藏C060/C077不支持的D008电池类型/算法与旧业务控件；客户版保留服务授权。正式Windows两版/CLI及Android两版放`releases/`，缓存/原始测试证据放TEMP，手机安装核验发布APK同一哈希。校准与OTA不自动编程309 EEPROM；外接电流表的带载精度验收独立进行。

当前实板交接：用户已暂停硬件测试，COM7/BT-BMS-56EA仍在原Boot mode1；SWD只读核对APP41984字节/CRC B22402D0与交付BIN完全一致、Boot未改、最新镜像记录有效。ST-Link重开失败，未实际执行软复位。等用户复位后先读D170 mode0、Build21266f9c及校准系数；不再自动刷板。当前手机安装0.9.3/18，0.9.5标准版打包后不在用户离开时自动安装。Android完整OTA/提速和外部电流表精度仍待验收。
