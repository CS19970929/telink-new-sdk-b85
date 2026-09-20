# Android BMS Tool

## 定位

`BmsTool.Android` 是与 Windows 客户版、内部版和 CLI 共用 C# 协议核心的 Android BLE 客户端。Android 有独立的移动端页面和交互，不复制 Windows XAML，也不修改 Windows 两版 UI。

协议单一真源：

- `BmsTool.Core` 链接 `BmsClient`、Modbus、诊断、D008 参数、软件保护批处理、AFE 硬件保护和 Telink OTA；
- Android 只实现 `BluetoothGatt` 传输、BLE 扫描、Activity 生命周期、手机页面和文件选择；
- Windows 与 Android 的 AFE 大帧逻辑统一依赖 `IBmsMtuTransport`，MTU=23 时仍走固件定义的授权分片事务；
- 历史 Kotlin Android 客户端不作为实现或协议依据。

## 手机页面

### 概览

- 扫描并显示所有 `BT_` / `BT-` 设备的名称、MAC、RSSI；不会绑定某个固定 MAC，也不会静默选择第一台。
- 成功完成 BMS 协议 probe 后才记住该 MAC，下次仅作为默认值；随时可以扫描并选择其他设备。
- 明确 MAC 连接和断开，连接后执行 Modbus probe。
- 显示 Pack Voltage、Current、SOC、SOH、容量、循环、温度、单体 min/max/delta、有效单体列表。
- 显示 CHG/DSG、加热、制冷、均衡及 L1/L2/L3 保护状态。
- 显示 BluetoothName、MAC、Serial、Hardware、Software、Firmware Build ID。

### 保护

- 软件保护读取 65 words，按 13 个保护组显示一级、二级、三级、恢复、滤波及物理单位。
- 只保存变更项；实际下发为完整 5-word 组，复用 `ProtectionBatch` 校验顺序和恢复阈值，并逐组读回。
- AFE 页面区分 Requested、Effective、ApplyState 和 Error；保存使用完整 profile 授权事务并读回。

### 参数

- 先读取 D008 能力窗口，再允许写业务参数。
- 支持额定容量、SOC、循环次数、加热启用/启动温度/停止温度。
- 支持软件保护、AFE 硬件保护、业务参数三个分组恢复默认。
- SN 在 Android 客户页面只读；工厂身份和电流校准仍由内部测试流程管理。

### 诊断

- 快速诊断读取运行状态；完整诊断额外读取 Trace、Evidence、原始帧、事件和保护参数。
- 显示 `SnapshotConsistent` / `TraceConsistent`，可导出与 CLI 相同结构的 AI 诊断 ZIP。
- 读取 100 条设备事件并解释事件类型和相邻事件间隔。
- App 通信日志同时显示在页面并写入 App 专属外部目录。

### 工具

- 修改蓝牙名后缀、请求设备休眠。
- 每 5 秒长期监控并导出 CSV。
- Android 文件选择器选择 BIN；缓存私有副本后执行严格 Telink marker、大小和 CRC trailer 预检。
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

App 的“工具 → OTA 升级 → 固件收件箱”会列出最近 20 个 BIN，并逐个执行与正式 OTA 相同的严格 Telink 预检。通过系统文件选择器导入的 BIN 也会复制到该收件箱，之后可直接复用。

PC 已编译好固件后，研发人员可直接运行独立的 `BmsTool.Android.Deployer.exe`，选择/拖入正式 BIN、选择已连接手机并点击发送。它在后台使用 ADB receiver 导入，不显示命令行、不要求填写 BMS MAC、不自动开始 OTA；App 打开固件页后，仍由用户从 `BT_` / `BT-` 列表明确选择目标设备。

VS Code 已提供四个仓库任务：按 `Ctrl+Shift+B` 会执行默认的 `BMS: 编译并发送固件到 Android`，先运行 `bms.py rebuild/check-fw`，再预检并发送标准输出 BIN。仅这个默认任务携带 `--auto-ota`：App 必须已经连接明确的 BMS，且连接对象和 MAC 在 OTA 开始前仍一致，才会跳过确认框直接使用现有共享 Telink OTA 流程；否则只导入固件并记录 `AUTO_OTA_SKIPPED`，不会自动扫描、自动连接或选择第一台设备。自动请求还必须匹配受 `android.permission.DUMP` 保护的导入 Receiver 生成的五分钟一次性 `upload_id + SHA-256` 授权，授权在检查时立即消费。`BMS: 发送现有固件到 Android`、`BMS: 选择 BIN 并发送到 Android` 和 Windows 图形发送器继续要求手机端人工确认。`BMS: 连接 Android 无线调试` 只恢复无线 ADB 连接。Sender 在没有活动设备时会重试 mDNS，自动连接唯一的官方 `_adb-tls-connect` 服务，因此手机重启或重新进入可信局域网后无需记忆动态端口。检测到多台手机或多个无线服务时拒绝自动选择。

ADB 导入 receiver 要求 `android.permission.DUMP`，普通第三方 App 不能调用；分片总大小限制为 2 MiB，任一分片、offset 或最终 SHA-256 不匹配都会删除临时文件。普通发送入口导入成功后仍要求 App 内确认；只有研发默认构建任务的 `--auto-ota` 会在 App 已保持目标 BMS 连接时授权自动写 Flash，严格 BIN 预检、`OTA_SUCCESS` 和升级后身份回读要求不变。

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
