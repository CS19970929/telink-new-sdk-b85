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

- 扫描 `BT_` / `BT-` 广播，显示全部发现设备的名称、MAC、RSSI；不会静默选择第一台。
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
- OTA 必须明确 MAC 并二次确认。成功必须有 `OTA_RESULT=OTA_SUCCESS`；随后重连并读取 Serial，设置期望 Serial 时还必须完全匹配。仅“重新连上”不会判定成功。
- 原始寄存器读取，以及每次均需二次确认的单寄存器写入/读回。保护和 AFE 参数不能借此页面绕过专用事务。

## Android 平台边界

- 当前只支持 Android BLE，不提供 Windows COM 直连。
- 当前 OTA 只开放共享 Telink OTA；Windows 的 STM32 Serial IAP 没有冒充为 Android 功能。
- 导出文件位于 `/sdcard/Android/data/com.cs.bmstool.android/files/Documents/`；运行日志位于同一 App 外部目录的 `files/` 根目录。
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
