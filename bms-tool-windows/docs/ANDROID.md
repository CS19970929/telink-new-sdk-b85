# Android BMS Tool

## 0.9.1 页面调整（2026-11-06）

当前页面与服务权限以[0.9.1说明](ANDROID_UI_091.md)为准：设备信息移至电池页；客户版隐藏SOH、满充和额定容量；记录页仅设备事件；升级入口移至服务设置；连接仅扫描/扫码；升级页精简且不显示项目/协议说明。协议、保护参数读写与升级结果核对保持。

客户版先在设备页扫描/扫码选择目标，再输入原服务密码进入升级。选中的模块即使停在Boot、APP probe失败，也可进入服务设置恢复升级；普通参数操作仍要求有效APP连接。外部固件分享只导入文件，不绕过服务权限。内部版无需客户密码。

## C060与通用升级（2026-11-06）

0.9.0两版已增加MAC二维码连接、C060常规参数整组读写、候选导入导出与独立固件升级页，详见 [Android C060实施记录](ANDROID_C060.md)。安卓不展示、读取或导出K/B。先读取身份再选择C060或原D008参数视图，可选协议不支持不能阻断基础通信。

设备 → 服务设置 → 固件升级支持原Telink严格BIN及C060/D015自描述YMODEM包；文件按内容校验，不按扩展名限制。后续项目遵循同一包头、能力窗口和结果回读合同即可使用，见 [通用升级合同](UNIFIED_UPGRADE.md)。YMODEM成功以公共核心确认的版本、长度和CRC为准；原Telink仍须OTA_RESULT与身份回读。客户版也可手动升级，工程工具与自动发送仍保持原版别边界。

## 定位

`BmsTool.Android` 是与 Windows 客户版、内部版和 CLI 共用 C# 协议核心的 Android BLE 客户端。Android 有独立的移动端页面和交互，不复制 Windows XAML，也不修改 Windows 两版 UI。

协议单一真源：

- `BmsTool.Core` 链接 `BmsClient`、Modbus、诊断、D008 参数、软件保护批处理、AFE 硬件保护和 Telink OTA；
- Android 只实现 `BluetoothGatt` 传输、BLE 扫描、Activity 生命周期、手机页面和文件选择；
- Windows 与 Android 的 AFE 大帧逻辑统一依赖 `IBmsMtuTransport`，MTU=23 时仍走固件定义的授权分片事务；
- 历史 Kotlin Android 客户端不作为实现或协议依据。

## 手机页面

0.6.0 使用固定底部导航“电池 / 保护 / 日志 / 设备”。业务参数、SOC 校正、诊断及原有工具收纳到“设备”的子页面，外部固件可先导入，客户版进入服务设置后打开升级页。

电池页以 SOC、充放电状态、电压、电流、剩余容量和温度为主；下方显示保护/MOS、有效单体三列网格、循环/温度及当前设备身份。客户版隐藏SOH、满充/额定容量；内部版保留这三项。无数据用“—”，断开或超过 15 秒未更新明确显示数据过期；不存在的串位仍以 61001 过滤，保留实际串号。只改变显示，不改变原始单位或电流方向。

保护页区分软件与硬件，按保护组展开编辑，使用数字键盘。设备当前值与编辑候选分开显示，修改项高亮，底部显示修改数量；未连接、未读取、无变化或输入非法时不能保存。提交前列出具体变更，沿用原整组校验、授权事务和写后读回。

记录页只显示设备事件，不提供通信日志页签、暂停、清空或复制入口。底层通信日志仍写入App原有私有文件，供故障定位，不影响设备事件读取。

系统状态栏、导航栏及键盘区域按系统 WindowInsets 避让。四个底部入口均分屏幕宽度，不使用横向滚动导航。

### 电池与设备连接

- 扫描并显示所有 `BT_` / `BT-` 设备的名称、MAC、RSSI；不会绑定某个固定 MAC，也不会静默选择第一台。
- 每次通过扫描或扫码明确选择设备，不读取或保存“上次设备”默认MAC，不静默选择第一台。内部自动化仍可显式传入MAC。
- 设备页仅保留蓝牙扫描、MAC二维码连接和断开；移除手动MAC/上次设备。连接后执行Modbus probe，仅点击扫码时请求相机权限。
- 两版显示电压、电流、SOC、剩余容量、循环、温度和单体；SOH、满充与额定容量仅内部版电池页显示。
- 设备 → 业务参数提供通用“设置当前 SOC”：写公共寄存器 `0x1005`，限制为 `0..100`，写入后读取实时快照核对；不依赖 D008 capability。当前 D008/D011/D013/D014 固件均实现该寄存器，旧固件不支持时必须显示通信失败，不能伪报成功。
- 显示 CHG/DSG、加热、制冷、均衡及 L1/L2/L3 保护状态。
- 电池页“设备信息”显示BluetoothName、MAC、Serial、Hardware、Software、Firmware Build ID；断开/切换时清除旧身份。

### 保护

- 软件保护读取 65 words，按 13 个保护组显示一级、二级、三级、恢复、滤波及物理单位。
- 只保存变更项；实际下发为完整 5-word 组，复用 `ProtectionBatch` 校验顺序和恢复阈值，并逐组读回。
- AFE 页面区分 Requested、Effective、ApplyState 和 Error；保存使用完整 profile 授权事务并读回。

### 设备 → 业务参数

- 先识别参数视图；C060读取公共常规字段（不含K/B），原D008视图仍按能力窗口允许写业务参数。
- 支持 D008 额定容量、循环次数、加热启用/启动温度/停止温度；通用 SOC 设置位于设备 → 业务参数，不受 D008 参数门禁限制。
- 支持软件保护、AFE 硬件保护、业务参数三个分组恢复默认。
- SN 在 Android 客户页面只读；工厂身份和电流校准仍由内部测试流程管理。

### 设备 → 高级诊断

- “设备健康检查”使用与 Windows/CLI 相同的 `BmsHealth` 模型，输出固件来源、启动/存储、采样、保护、身份和 D008 capability 的 `pass / info / warning / critical / unknown` 结论及建议。
- 快速诊断读取运行状态；完整诊断额外读取 Trace、Evidence、原始帧、事件和保护参数。
- 显示 `SnapshotConsistent` / `TraceConsistent`，可导出与 CLI 相同结构且包含 `health.json` 的 AI 诊断 ZIP。
- “SOC 自动测试”连续采样 10 次并检查范围、容量关系、Profile、ETA、Build ID 与跳变；“诊断一致性测试”连续采集 3 次完整诊断并检查 Snapshot、Trace、Errors 与 Build ID。两者复用 `BmsTestEngine`，结果自动保存到 App Documents 目录的 JSON 报告。
- 读取 100 条设备事件并解释事件类型和相邻事件间隔。
- App通信日志只写原有私有日志文件，不在记录页显示。

### 设备 → 工具

- 修改蓝牙名后缀、请求设备休眠。
- 每 5 秒长期监控并导出 CSV。
- 固件升级已迁至两版均有的“设备 → 服务设置 → 固件升级”，缓存私有副本后按内容执行YMODEM包CRC/身份校验或严格Telink预检。
- 可从微信、浏览器或文件管理器直接“打开/分享” `.bin` 到 BMS Tool；App 自动复制到私有固件收件箱并进入 OTA 页面，不要求命令行或再次选文件。
- OTA 必须明确 MAC 并二次确认。成功必须有 `OTA_RESULT=OTA_SUCCESS`；随后重连并读取 Serial，设置期望 Serial 时还必须完全匹配。仅“重新连上”不会判定成功。
- 原始寄存器读取，以及每次均需二次确认的单寄存器写入/读回。保护和 AFE 参数不能借此页面绕过专用事务。

## 一键 Android OTA 测试

App 只需安装一次。后续修改的是 BMS 固件时，不需要重新构建或发送 APK，也不需要在手机文件选择器中逐层寻找 BIN。

从指定固件工作树 clean rebuild：

```powershell
.\android-ota-test.ps1 `
  -FirmwareRoot 'D:\path\to\feature-worktree' `
  -Mac A4:C1:38:00:30:CF `
  -ExpectedSerial D007-OTA-R4
```

脚本依次执行：

1. 核对固件工作树并记录 `HEAD`；
2. 执行 `python bms_tools/bms.py rebuild` 和 `check-fw`；
3. 拒绝 `*.raw.bin`，计算正式 BIN 的 size 和 SHA-256；
4. 自动选择唯一的 ADB 手机，或用 `-DeviceId` 明确指定；
5. 通过仅 ADB/system shell 可调用的导入入口，将 BIN 分片写入 App 私有 `FirmwareInbox`，并由 App 校验完整文件的 SHA-256；
6. 启动 App，填入 MAC、固件路径和期望 Serial；
7. 用户在手机核对目标、供电和文件后作最终确认；
8. 等待 `OTA_SUCCESS`、升级后重连和身份回读；
9. 将 `logcat`、App 日志和 `summary.json` 拉回 `%LOCALAPPDATA%\CodexTemp\bms-android-ota\`。

已经有可烧录 BIN：

```powershell
.\android-ota-test.ps1 `
  -Bin .\project\tlsr_tc32\B85\825x_ble_sample_cli\825x_ble_sample.bin `
  -Mac A4:C1:38:00:30:CF `
  -ExpectedSerial D007-OTA-R4
```

首次安装或 App 已更新时增加 `-InstallApp`；平常固件测试不要加。只导入固件、打开确认页但不在脚本中等待结果时可使用 `-NoWait`。多台手机连接时必须指定 `-DeviceId`。

App 的“设备 → 服务设置 → 固件升级与其他工具 → OTA 升级 → 固件收件箱”会列出最近 20 个 BIN，并逐个执行与正式 OTA 相同的严格 Telink 预检。通过系统文件选择器导入的 BIN 也会复制到该收件箱，之后可直接复用。

PC 已编译好固件后，研发人员可直接运行独立的 `BmsTool.Android.Deployer.exe`，选择/拖入正式 BIN、选择已连接手机并点击发送。它在后台使用 ADB receiver 导入，不显示命令行、不要求填写 BMS MAC、不自动开始 OTA；App 打开固件页后，仍由用户从 `BT_` / `BT-` 列表明确选择目标设备。

VS Code 已提供四个仓库任务：按 `Ctrl+Shift+B` 会执行默认的 `BMS: 编译并发送固件到 Android`，先运行 `bms.py rebuild/check-fw`，再预检并发送标准输出 BIN。仅这个默认任务携带 `--auto-ota`：App 必须已经连接明确的 BMS，且连接对象和 MAC 在 OTA 开始前仍一致，才会跳过确认框直接使用现有共享 Telink OTA 流程；否则只导入固件并记录 `AUTO_OTA_SKIPPED`，不会自动扫描、自动连接或选择第一台设备。自动请求还必须匹配受 `android.permission.DUMP` 保护的导入 Receiver 生成的五分钟一次性 `upload_id + SHA-256` 授权，授权在检查时立即消费。`BMS: 发送现有固件到 Android`、`BMS: 选择 BIN 并发送到 Android` 和 Windows 图形发送器继续要求手机端人工确认。`BMS: 连接 Android 无线调试` 只恢复无线 ADB 连接。Sender 在没有活动设备时会重试 mDNS，自动连接唯一的官方 `_adb-tls-connect` 设备；同一手机切换无线调试后短暂残留的历史端点会逐个验证，不会被误判为多台手机。检测到不同手机时仍拒绝自动选择。

ADB 导入 receiver 要求 `android.permission.DUMP`，普通第三方 App 不能调用；分片总大小限制为 2 MiB，任一分片、offset 或最终 SHA-256 不匹配都会删除临时文件。普通发送入口导入成功后仍要求 App 内确认；只有研发默认构建任务的 `--auto-ota` 会在 App 已保持目标 BMS 连接时授权自动写 Flash，严格 BIN 预检、`OTA_SUCCESS` 和升级后身份回读要求不变。

## 无线 ADB 日常使用

首次启用时可用 USB 完成 Android 开发者选项、无线调试配对和 APK 安装。配对成功且 PC/手机位于同一可信局域网后，日常编译、固件发送、日志导出和 OTA 不需要 USB 线。

日常检查：

```powershell
adb devices -l
adb mdns services
```

`adb devices -l` 中出现 `<phone-ip>:<dynamic-port> device` 即可断开 USB。IP 和 ADB TLS 端口都是动态状态，禁止写入 VS Code Task、脚本或 `AGENTS.md`。`BMS: 连接 Android 无线调试` 与 Sender 使用官方 `_adb-tls-connect` mDNS 服务恢复连接；同一手机切换无线调试后可能短暂残留旧端口，Sender 会按同一 mDNS instance 逐个验证，但检测到不同手机时仍会拒绝自动选择。

已配对手机的恢复顺序：

1. 确认 PC 和手机位于同一局域网，路由器未开启 AP/client isolation；
2. 手机解锁后点击系统快捷设置中的“无线调试”磁贴；
3. 运行 VS Code 任务 `BMS: 连接 Android 无线调试`，或直接再次执行默认发送任务；
4. 仅在配对记录被删除、开发者选项被重置、系统更新破坏授权或 mDNS 始终无法发现时，再使用 USB 恢复配对。

部分 Android ROM（已验证的 HyperOS 机型即如此）会在手机重启后关闭无线调试开关，但保留快捷设置磁贴和已授权 PC。因此重启后需要手工点击一次磁贴，这是系统安全行为，不应由 App 或脚本绕过。

2026-09-20 实机验证：无线调试磁贴完整“关闭 -> 开启 -> mDNS 新端口 -> TLS 重连 -> 读取手机 serial”循环 `4/4 PASS`；手机重启后磁贴保留，点击一次后无需重新输入配对码即恢复无线 ADB。同日已通过明确无线 device id 完成一轮 `rebuild/check-fw -> 分片发送 -> 自动 OTA -> OTA_SUCCESS -> 重连/身份回读`。

## 客户固件交付方向

固件收件箱解决开发、售后和离线升级。正式在线客户升级不应让客户寻找裸 BIN，后续按独立阶段增加：

- HTTPS 固件清单，按产品、Hardware、Software、Build ID 和发布渠道匹配；
- App 私有缓存、断点/失败清理、SHA-256 和签名清单验证；
- 更新说明、版本回退策略、分批发布和明确的用户确认；
- `OTA_SUCCESS + Build ID/版本变化` 作为客户成功证据，修改 Serial 只用于研发测试。

当前 Telink marker、CRC trailer 和 SHA-256 都不是发布者身份认证。面向客户发布前至少需要签名清单；如需抵抗本地恶意镜像，最终还应由 Bootloader 验证固件签名。

## Android 平台边界

- 当前只支持 Android BLE，不提供 Windows COM 直连。
- 当前 OTA 只开放共享 Telink OTA；Windows 的 STM32 Serial IAP 没有冒充为 Android 功能。
- 导出文件位于 `/sdcard/Android/data/com.cs.bmstool.android/files/Documents/`；运行日志位于同一 App 外部目录的 `files/` 根目录。
- 固件收件箱位于 App 私有内部存储，文件管理器和其他 App 不能直接访问；请通过 App 的“导入 BIN”或 `android-ota-test.ps1` 导入。
- Android 12 及以上请求 `BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT`；旧系统扫描请求定位权限。

## 构建与安装

```powershell
dotnet build .\BmsTool.Android\BmsTool.Android.csproj -c Release
adb install -r .\BmsTool.Android\bin\Release\net10.0-android\com.cs.bmstool.android-Signed.apk
```

自动读取身份：

```powershell
adb shell am start -n com.cs.bmstool.android/<MainActivity> `
  --es operation info `
  --es mac A4:C1:38:00:30:CF
```

自动 OTA 仍支持 `operation=ota`、`mac`、`firmware` 和 `expected_serial` extras。Activity 实际名称通过以下命令取得：

```powershell
adb shell cmd package resolve-activity --brief com.cs.bmstool.android
```

## 2026-09-20 真机基线

测试手机：Xiaomi `23127PN0CC`，Android BLE，目标 `A4:C1:38:00:30:CF`，协商 MTU=23。

- Release 构建：0 warning / 0 error。
- 安装和启动：通过，无 `AndroidRuntime` 崩溃。
- GATT/CCCD/Modbus probe：通过。
- 实时数据和身份读取：通过；实际回读 Serial `D007-OTA-R3-6BDD625`。
- 软件保护 65 words：读取通过，Android 正确生成 13 组编辑 UI。
- 扫描：通过；本轮断开后发现另一台 `BT_CS0822 / A4:C1:38:19:4D:BE`，未自动选择。
- D008 参数与 Diagnostics：当前连接固件未声明对应 magic，App 正确显示“不支持”，没有错误写入或崩溃；支持路径仍需 D008 固件实板复测。
- AFE 参数写入、参数写入、休眠和原始写：本轮为避免改变现场设备，未执行写操作。
- Telink OTA：共享状态机此前已在同一手机和目标设备完成三轮 OTA_SUCCESS + 不同 Serial 回读；本次完整 UI 基线不重复刷写。

修改共享协议、保护、AFE 或 OTA 后，至少执行：

```powershell
.\test-ota-protocol.ps1
dotnet build .\BmsTool.Cli\BmsTool.Cli.csproj -c Release
dotnet build .\BmsTool.Windows\BmsTool.Windows.csproj -c Release
dotnet build .\BmsFactoryTest.Windows\BmsFactoryTest.Windows.csproj -c Release
dotnet build .\BmsTool.Android\BmsTool.Android.csproj -c Release
```

主机与真机读取通过不能代替 D008 参数写入、AFE 有效值、保护触发/恢复、休眠唤醒和 OTA 断电恢复验证。

## 2026-10-13 手机 UI 验证边界

- UI 发布版本 0.6.0 / versionCode 11，沿用原包名与签名，覆盖安装保留 App 数据。
- 公共核心固定于 c20b8ddc062299ab1ea2dc4ccdd53acd00b9b406；构建快照叠加本次 Android UI 与开始工作前已有的 Android 未提交修改。未纳入并行进行中的 Windows/共享核心改动。
- Android Release 构建和真机页面检查；手机 Xiaomi 23127PN0CC / Android 16，1200×2670。验证四入口、无连接门禁、扫描结果、用户选定设备的实时数据、软件保护 65 项、DVC1124 硬件参数读取与折叠，以及事件/通信日志显示。
- 未执行保护/参数保存、原始寄存器写入、休眠或 OTA。本次真机读取与 UI 检查不能替代参数写入、物理保护和其它手机/字体尺寸验收。
- 构建、源快照和截图在用户临时区；最终 APK 与发布清单在当前固件项目的 releases/bms-tool-android/ 下，不提交生成产物。

## 2026-10-13 两版 UI 与独立 MOS 控制源码

当前源码版本为 `0.8.0` / versionCode `14`。默认 `BmsAppEdition=Internal` 保持原 `com.cs.bmstool.android` 测试包；`BmsAppEdition=Customer` 生成可并存的 `com.cs.bmsbattery` 客户包。设备页显示构建源码 commit，脏工作树追加 `-dirty`。完整取舍见 [两版 App 与 MOS 控制](APP_EDITIONS_AND_MOS_CONTROL.md)。

客户版四个主入口为“电池 / 单体 / 记录 / 设备”，日常首页突出 SOC、总压、电流、容量、温度、充放电 MOS 和告警。软件/硬件保护、SOC 与业务参数通过本次连接有效的服务设置进入；测试版保留保护、日志、诊断、OTA、长期监控和原始寄存器等工程功能。

两版 App 与 Windows 上位机共用 `Shared/BmsClient.MosControl.cs`。按钮只改变固件 RAM 用户许可，不复用休眠，不绕过保护；写后读取 `0x2F10..0x2F17` 核对。旧固件不支持窗口时按钮禁用，实时数据继续工作。

2026-10-13 后续获得用户明确授权后，两版 `0.7.0` Release APK 均完成构建，结果均为 0 warning / 0 error；Xiaomi 23127PN0CC / Android 16 上完成两版安装、启动和四个主入口截图检查。测试版使用正式包名覆盖安装；客户版因 HyperOS 首次安装新包名要求小米账号密码，真机检查临时复用了手机上同一开发签名的 `com.telink.bms.mobile` 包位，正式客户包名仍是 `com.cs.bmsbattery`。

本次未连接 BMS，未读取或写入保护参数，未切换 MOS，也未执行休眠、烧录或 OTA。上述结果仅覆盖构建、安装和静态页面交互；上方 `0.6.0` 真机读数仍是历史基线，不能当作 `0.7.0` 协议或实板验收。
