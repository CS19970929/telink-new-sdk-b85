# Windows 上位机

本目录是 BMS 仓库中的独立 Windows 项目区，与固件源码并列维护。

- `BmsTool.Windows/`：客户版 BMS Assistant，面向客户交付。
- `BmsFactoryTest.Windows/`：内部完整测试版 BMS Assistant，基于客户版最新版功能，保留全部工程/调试/出厂测试页面，启动后不需要密码。
- `BmsTool.Cli/`：无 UI 命令行版，面向快速 OTA、脚本和 AI/Codex 实板诊断。
- `BmsTool.Core/`：跨平台 C# 核心，单一保存 Modbus、BMS 身份读取、Telink BIN 预检和 OTA 状态机。
- `BmsTool.Android/`：新的 .NET Android App，通过 `BluetoothGatt` 复用 `BmsTool.Core`，不使用或复制历史 Kotlin Android 客户端。

四个入口共用同一套底层协议源码；Windows 客户版、内部版和 CLI 仍按原发布脚本同步构建，Android 另外构建并安装 APK。客户版不包含工厂测试入口；内部完整测试版包含全部功能。涉及 Factory Session 的共享协议代码位于各项目自身源码中，并与 BMS 固件的 `docs/factory_test_protocol.md` 对照维护。

## Android 共享核心版

Android 版不是对 Windows 流程的第二份翻译。`BmsTool.Core` 编译同一份 `BmsClient.cs`、诊断读取、`ModbusRtu.cs`、`TelinkOtaProtocol.cs` 和 `Shared/TelinkOtaCore.cs`；Windows WinRT 与 Android `BluetoothGatt` 只实现各自的传输接口。OTA 的 START/DATA/END、packet CRC16、BIN 尺寸/TLNK marker/Telink CRC32 trailer 预检、`*.raw.bin` 拒绝、`OTA_RESULT` 解码和成功判定都只有一份源码。

Android 构建：

```powershell
dotnet workload install android
dotnet build .\BmsTool.Android\BmsTool.Android.csproj -c Release
adb install -r .\BmsTool.Android\bin\Release\net10.0-android\com.cs.bmstool.android-Signed.apk
```

Android 使用独立的手机信息架构和原生控件，不复用或修改 Windows XAML。主导航分为“概览 / 保护 / 参数 / 诊断 / 工具”，当前覆盖：

- `BT_` / `BT-` 扫描、明确 MAC 连接、断开和 5 秒实时刷新；
- 电压、电流、SOC/SOH、容量、温度、单体、MOS、系统状态和三级保护；
- 65 words 软件保护整组校验写入、AFE Requested/Effective 原子事务；
- D008 容量/SOC/循环/加热参数和分组恢复，SN 在客户页面只读；
- 快速/完整诊断、AI 诊断 ZIP、100 条事件、通信日志；
- 长期监控 CSV、蓝牙名、休眠、受二次确认保护的原始寄存器读写；
- Android 文件选择器、严格 Telink BIN 预检、OTA_RESULT、重连与 Serial 回读确认。

每次启动会在 Android App 专属外部 `files/` 目录生成带时间戳的完整日志，也可用 ADB 进行可重复的实板测试：

```powershell
adb shell am start -n com.cs.bmstool.android/<MainActivity> `
  --es operation ota `
  --es mac A4:C1:38:00:30:CF `
  --es firmware_inbox_name ota-20260920-153827-640D7AC7.bin `
  --es expected_serial D007-OTA-R1
```

`firmware_inbox_name` 必须是已经由 App 或 `android-ota-test.ps1` 导入私有收件箱的文件名；不要直接向 App 私有目录执行 `adb push`。`<MainActivity>` 用 `adb shell cmd package resolve-activity --brief com.cs.bmstool.android` 获取。日志位于 `/sdcard/Android/data/com.cs.bmstool.android/files/`，可直接用 `adb pull` 导出。OTA 不会把“能重连”单独当成成功；自动测试同时要求服务器 `OTA_RESULT=OTA_SUCCESS` 和升级后 Serial 回读匹配，并记录升级前后 Firmware Build ID。修改共享 OTA 代码后必须运行 `./test-ota-protocol.ps1`，并同时构建 `BmsTool.Cli`、`BmsTool.Windows`、`BmsFactoryTest.Windows`和 `BmsTool.Android`。

Android GATT 在 CCCD 成功后固定等待 300 ms 再发送首帧，并对连接阶段的瞬态 `status=22`、timeout 或 I/O 失败最多重试 3 次；数据阶段仍由共享 `BmsClient` 的有界 probe/reconnect 逻辑处理。

完整页面、共享源码、安全边界、构建和真机测试结果见 [Android BMS Tool](docs/ANDROID.md)。Android 当前只提供 BLE；Windows 的 COM 直连和 STM32 Serial IAP 不伪装为 Android 功能。

日常固件 OTA 不需要重新构建或重新安装 App。App 安装一次后，可用一条命令完成固件构建、`check-fw`、ADB 安全导入、打开 OTA 确认页、等待结果和拉取证据：

```powershell
.\android-ota-test.ps1 `
  -FirmwareRoot 'D:\path\to\firmware-worktree' `
  -Mac A4:C1:38:00:30:CF `
  -ExpectedSerial D007-OTA-R4
```

已有 BIN 时改用 `-Bin .\825x_ble_sample.bin`。只有首次安装或 App 本身更新时才加 `-InstallApp`。脚本不会绕过手机端最终确认，也不会接受 `raw.bin`。

不希望使用命令行时，运行独立的 `BmsTool.Android.Deployer.exe`：选择或拖入 PC 已编译的正式 BIN，选择已授权的 Android 手机，点击“发送到手机并打开 BMS Tool”。发送器在后台调用 ADB，不弹出命令窗口，不要求填写 BMS MAC，也不会自动开始 OTA；手机端继续只显示 `BT_` / `BT-`，由用户明确选择目标设备并最终确认。发布命令：

```powershell
.\publish-android-deployer.ps1
```

使用 VS Code 时按 `Ctrl+Shift+B` 即可执行默认任务 `BMS: 编译并发送固件到 Android`。它直接完成固件 `rebuild/check-fw`、严格 Telink BIN 预检、ADB 分片发送和手机端 SHA-256 导入确认；若 App 在任务开始前已经连接明确的 BMS，导入后会跳过手机确认框并直接沿用该连接的 MAC 执行 OTA。未连接、连接已变化或 MAC 不一致时只导入并打开 OTA 页面，绝不自动扫描或选择设备。`Tasks: Run Task` 中的 `BMS: 发送现有固件到 Android`、`BMS: 选择 BIN 并发送到 Android` 以及 Windows 图形发送器仍保持人工确认模式。`BMS: 连接 Android 无线调试` 会在没有活动设备时通过 mDNS 自动发现并连接唯一的官方 `_adb-tls-connect` 设备。默认任务使用 `bms.py` 的标准输出 `tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\project\tlsr_tc32\B85\825x_ble_sample_cli\825x_ble_sample.bin`；检测到多台手机或多个无线服务时会拒绝自动选择。

## 三入口发布约定

统一使用 `build-release.ps1` 发布 Windows x64、自包含、单文件 EXE：

```text
BmsTool.Windows\publish\customer-win-x64-<时间戳>\BmsTool.Windows.exe
BmsFactoryTest.Windows\publish\internal-full-win-x64-<时间戳>\BmsFactoryTest.Windows.exe
BmsTool.Cli\publish\cli-win-x64-<时间戳>\bms-cli.exe
```

脚本会为每次发布创建新的时间戳目录，避免覆盖正在运行的旧 EXE，并输出三套 EXE 的 SHA-256、目标框架和当前 Git commit。客户版的高级页面仍由 `hs456` 控制；内部完整测试版不设置密码门槛，仅供研发、调试和出厂测试使用，不应作为客户交付包。

详细的仓库边界、构建入口和协作规则见 `../docs/bms_windows_joint_maintenance.md`。

## 命令行版 / AI 接口

`BmsTool.Cli` 提供 `scan / info / ota / diag`。它直接复用 WPF 上位机的 BLE、串口、BmsClient、Telink OTA、STM32 IAP 和 D008 Diagnostics 源码，不维护第二套协议。

快速 OTA：

```powershell
bms-cli ota .\firmware.bin --auto --yes
```

AI/Codex：

```powershell
bms-cli scan --json
bms-cli info --auto --json
bms-cli ota .\firmware.bin --mac A4:C1:38:12:34:56 --yes --json
bms-cli diag --mac A4:C1:38:12:34:56 --output .\D008_diag.zip --json
```

`--json` 的 stdout 是稳定 JSON 契约，通信细节只在 `--verbose` 时写到 stderr；自动 OTA 使用 `--yes`，不会弹 UI 或等待图形交互。完整说明见 [BMS CLI：快速 OTA 与 AI 实板诊断](docs/CLI.md)。

## 客户版功能边界

客户版默认只提供日常使用所需的实时监控、设备信息/蓝牙名、事件日志和长期监控功能：

- 不编译 AFE 硬件保护参数读写代码；
- 不提供专业调试页和原始寄存器读写入口；
- 不包含出厂测试页；出厂测试仍使用独立的 `BmsFactoryTest.Windows` 项目；
- 软件保护/BMS 参数页和 OTA 页默认隐藏。

实时监控页右侧的“高级功能”按钮输入密码 `hs456` 后，才会显示软件保护/BMS 参数页和 OTA 页；再次点击可锁定。该密码是客户版 UI 访问控制，不替代固件侧权限控制。

BLE 搜索兼容现有两种 BMS 广播名前缀：`BT_` 和 `BT-`。对于 STM32 IAP 可能出现的空广播名，上位机会先确认设备确实提供 Nordic UART service，再加入列表，避免把周围其他无名称 BLE 外设显示为 BMS。搜索列表同时显示设备 MAC，连接时仍会执行 Modbus 探测确认设备类型。

单体电压按公共协议固定读取 32 个槽位；固件传输值 `61001` 表示该串不存在。客户版和工厂版均按该哨兵值过滤有效串，不把不存在的槽位当作 61.001 V，也不压缩原始串号。

连接探测兼容已部署的串口蓝牙模块：优先检查 `0xD120` 实时窗口 magic；如果该可选窗口未启用，则继续校验稳定的 `0xD000` Legacy 数据窗口或 `0xC002` 生产信息窗口。部分外置蓝牙模块不实现 BMS 的 `0x0000` MAC、`0x0100` 蓝牙名称或空的硬件/软件版本寄存器，此时设备信息页分别使用 BLE 连接地址、BLE 广播名称和“未知”，不影响实时数据和保护数据读取。

## 直连串口

通信设置左侧可切换 `BLE` / `串口`。选择串口后可在波特率框选择或输入波特率，再点击“刷新”枚举 Windows 当前可用的 COM 端口；默认参数来自 BMS/IAP 的 SCI 初始化：`19200 baud、8 data bits、No parity、1 stop bit、No hardware flow control（19200 8N1）`。波特率必须与 BMS/UART 模块两端一致。串口接收按任意长度分片交给同一套 Modbus RTU 组帧器，不依赖一次接收完整响应，因此普通监控、身份读取、BMS 软件参数和内部 Factory Session 测试均复用 BLE 相同的上层逻辑。

串口连接会记录 `OPEN/WRITE/RX_FRAGMENT/READ_FAIL` 日志，并在轮询连续失败后自动重连。串口没有 BLE 地址，设备身份读取仍以 BMS 寄存器为准；如果 MAC 或蓝牙名称寄存器不存在，界面使用 `串口 COMx` 作为连接端点回退信息。AFE 硬件保护参数写入只允许 BLE 连接，防止把 BLE 专用硬件参数通道误用到直连串口。

## OTA 双架构自动升级

Telink OTA 已按仓库内 `tc_ble_single_sdk V3.4.2.8` 的官方 Master/Server 协议重新核对，并加入协议单元测试与 CI 门禁。详细来源、协议字段、D008 Server 状态、Windows 审计结论、修复项和实板验收清单见 [Telink OTA Master、D008 OTA Server 与 Windows OTA](docs/TELINK_OTA_MASTER_AND_WINDOWS_OTA.md)。修改 OTA 代码后必须运行 `./test-ota-protocol.ps1`。

OTA 页默认使用 `Auto（自动识别）`：

- 发现 Telink 专用 OTA service `00010203-0405-0607-0809-0a0b0c0d1912` 时，使用现有 Telink OTA START/DATA/END 流程；
- 发现 BMS Nordic UART service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` 时，使用 STM32 串口 IAP 流程：`0xFFFD` 进入 IAP、`0xFFFE` 每页写 1024 字节、`0xFFFF` 完成。由于现有 IAP 的实际解析约定，页帧使用 `0x10` 的扩展兼容格式：`byte count=0`、`quantity=1024`，不能使用标准 `byte count=0x400`（单字节无法表达）；
- 直连 `COM` 时自动选择 STM32 Serial IAP，使用同一组 `0xFFFD/0xFFFE/0xFFFF` 命令；进入 IAP 和每一页发送后都等待并校验 Modbus ACK。BLE 链路按 20 字节分片，直连串口按 IAP 缓冲区能力直接发送约 1033 字节页帧，不把 BLE 分片规则误套到 COM；ACK 超时仍立即停止，禁止对无页序号的例程盲目重传；
- STM32 APP BIN 必须能通过向量表校验（初始栈指针在 SRAM、复位向量在 `0x08001C00..0x0800F800`），按 `0x08001C00` APP 区使用，最大 55 KB，最后一页用 `0xFF` 补齐；已识别为 Telink 格式的文件会被拒绝；
- STM32 在 ENTER 应答后等待 BMS 复位并重新建立 GATT 连接；如果设备已经处于 IAP 且普通 Modbus 探测无响应，上位机会保留 GATT 连接并允许直接进入 OTA；之后每个升级帧按串口兼容分片发送，并对每个分片使用带响应写入，按 19200 波特率和安全余量动态排空，不重复增加固定等待，必须收到完整 Modbus ACK 才发送下一页；ACK 超时会停止，不会对没有序号的旧 IAP 例程盲目重传；
- 升级后统一重连并读取软件版本、实时数据验证。

当前 STM32 例程兼容模式能提供文件边界、Modbus CRC16、页 ACK 和升级后通信验证，但例程 IAP 本身没有镜像签名和完整镜像校验，不能单独作为量产级防篡改安全保证。正式量产前必须启用安全 IAP 协议。

## 客户版发布

发布产物应放在本仓库项目目录内，例如：

```text
BmsTool.Windows\publish\customer-win-x64\BmsTool.Windows.exe
```

使用 `dotnet publish` 生成 Windows x64、自包含、单文件 EXE。发布时应同时记录 EXE 的 SHA-256、源码 commit 和实际目标框架。


## SH3673520 保护参数扩展（2026-09-07）

客户版和完整版新增“3520 保护参数”页，共用 `Shared` 中的界面和物理单位编解码。客户版随高级功能解锁可见；此前“客户版不包含 AFE 硬件参数”的限制仅针对旧 SH367309 页面。
连接后先读取，分别编辑和保存 MCU 软件保护、AFE 硬件保护。当前保护模式、同/分口、WDT和SPI类型只读显示；未启用的兼容软件字段只读。
新固件通过0x2500型号/版本识别，旧0x2400寄存器含义不变。写入范围/步进/滞回由两端校验；硬件ACK还必须结合回读应用状态。
详细参数、协议及当前固件Flash容量阻碍见 [3520 参数说明](docs/3520_parameters.md)。尚未完成实板联调，不能给未升级的板子写新参数。
BLE整组写要求MTU≥60及桥接模块支持57字节整帧，否则使用直连串口。旧BLE、Factory Session保留；已识别3520不能使用起址不同的旧STM32 OTA。
运行 `./test-3520-parameters.ps1` 检查共享编解码；运行 `./build-release.ps1` 同时生成两版EXE。构建中间文件转到 `%LOCALAPPDATA%/CodexTemp/bms-tool-windows`，正式EXE仍在各项目publish目录。


## 事件日志100/500条（2026-09-07）

客户版与完整版的事件日志页自动识别100或500条容量，无需用户选择，每次点击都重新识别。先读前100条，再读第二页；仅第二页返回本固件的地址非法异常（功能0x03、错误码0x01）时识别为100条，其余成功读取5页后识别为500条。超时、CRC错误（本协议0x02）、短响应、其他异常以及第三页及以后失败均报错，不降级，保留此前显示数据。不能用有效日志数量判断设备容量。

继续使用`0xC008`起始窗口，每条占一个寄存器，高字节为事件、低字节为间隔。每次读100字；500条依次读取`C008/C06C/C0D0/C134/C198`，共5帧，BLE和串口共用，未改变Modbus帧格式或OTA/Factory Session逻辑。读取期间停轮询并防止重复点击，全部成功后更新列表。

`test-event-log.ps1`对两版页面一致性、真实分页读取代码的100/500条顺序、旧100条边界、短响应和中途失败进行测试，生成文件仅位于用户临时区。固件对应`Flash.h`的`FLASH_STORAGE_LOG_RECORD_COUNT`宏（100U或500U）。

最新双版EXE通过`build-release.ps1 -ReleaseTag log-commands-v2-20260907`生成：

- 客户版：`BmsTool.Windows/publish/customer-win-x64-log-commands-v2-20260907/BmsTool.Windows.exe`
- 完整版：`BmsFactoryTest.Windows/publish/internal-full-win-x64-log-commands-v2-20260907/BmsFactoryTest.Windows.exe`

两版发布和分页测试通过；尚未进行实板串口/BLE的500条读取验证。跨帧期间BMS仍可能产生新日志，当前协议不提供冻结快照。

## 设备日志删除与休眠（2026-09-07）

- 完整版“事件日志 → 删除设备日志”：确认后发送 Modbus `0x06`，寄存器 `0x1007`，值 `0x0001`。客户版不包含删除入口或删除处理函数。删除与读取互斥，暂停轮询，收到通过CRC、地址及值校验的成功应答才清空界面；失败保留原列表，提示重新读取核实，不自动重发。
- 两版“实时监控 → 连接区域 → 休眠”：确认后发送 `0x06`，寄存器 `0x1102`，值 `0x000A`。复用串口/BLE通信。固件先应答，等待通信及存储空闲后处理深度休眠；上位机收到ACK只表示请求被接受，随后停止轮询、取消自动重连并主动断开。按板上唤醒按键后手动连接。超时不当作成功，也不自动重发命令。
- 协议依据当前 BMS `Sci_Upper.h`、`Sci_WrReg_0x06_BMS_FunctionON`、`LogRecord.c/Sci_WrReg_0x06_Reset_EventRecord` 和 `rtc_sleep.c/lp_process_command_sleep`。删除持久化逻辑重置标记，旧页随后复用，并非立即物理擦除全部旧数据；100/500条共用同一指令，新事件仍会正常记录。
- 本次不修改BMS固件。通过协议组帧/ACK异常测试、100/500条分页回归及双版发布构建；尚未进行实板删除和休眠验证。最新发布标签 `log-commands-v2-20260907`。

## SOC加速擦写测试（完整版）

完整版新增“Flash寿命测试”页，支持SOC循环变化、重复值、小幅往返和强制保存，实时查看进度、物理页擦除及失败计数，自动保存备份、流程日志、CSV和结果JSON。需要 `FLASH_ENDURANCE_TEST_ENABLE=1` 专用BMS固件；测试真实消耗SOC页寿命，正常版固件默认不开放。客户版不接入。

详见 [SOC加速擦写使用、协议及恢复说明](docs/flash_endurance.md)。测试记录在用户文档 `BmsFlashTests`。运行 `test-flash-endurance.ps1` 验证协议，`test-event-log.ps1` 回归日志。

最新双版：`build-release.ps1 -ReleaseTag flash-endurance-v3-20260907`；完整版目录 `BmsFactoryTest.Windows/publish/internal-full-win-x64-flash-endurance-v3-20260907/`，客户版目录 `BmsTool.Windows/publish/customer-win-x64-flash-endurance-v3-20260907/`。测试和构建通过，未执行实板耐久测试。
