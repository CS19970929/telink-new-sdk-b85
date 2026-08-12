# Telink BMS Android / iOS 手机 App

## 功能范围

`src/TelinkOta.Mobile` 是真正运行在 Android 手机和 iPhone 上的 .NET MAUI App，两端使用同一套 UI 与业务代码：

- 主动扫描附近 BLE 设备；不强制按名称过滤，因为 `BT_cs-0604` 实测广播可能没有 LocalName；
- 连接 Telink SPP 服务并轮询 Modbus RTU；
- 显示总压、电流、SOC、温度、单体电压、容量、循环、状态/故障、SN、HW、SW、MAC 与蓝牙名；
- 链路断开或连续读失败后自动重连；
- 通过 `0x10 @ 0x0100` 修改蓝牙名，随后 `0x03` 读回完全一致才报告成功；
- 从手机文件选择器读取 BIN，复用 `TelinkOta.Core` 的 Mark、Size、CRC32、分包、CRC16、Legacy/Extend、Result、重启重连及版本复核逻辑；
- OTA 时自动停止电池监控，保持屏幕常亮，结束后恢复监控；支持取消与完整日志。

## 代码结构

```text
src/TelinkOta.Mobile/
├─ Ble/
│  ├─ MobileBleManager.cs       # Android/iOS 扫描和设备缓存
│  ├─ MobileBleTransport.cs     # Plugin.BLE -> IBleTransport
│  └─ MobileBleDevice.cs
├─ Services/
│  └─ MobileBatteryMonitor.cs   # Modbus 轮询、重连、改名
├─ Platforms/Android/           # Android 权限/入口
├─ Platforms/iOS/               # iOS Bluetooth Usage Description/入口
├─ MainPage.xaml                # 共用手机 UI
└─ MainPage.xaml.cs             # 监控与 OTA 编排
```

协议核心仍只有一份：`src/TelinkOta.Core`。移动端 BLE 使用 `Plugin.BLE 3.1.0`，其 Android 后端调用 BluetoothGatt，iOS 后端调用 CoreBluetooth。

## Android 构建与安装

要求：.NET 7.0.400、`maui-mobile` 工作负载、Android SDK 33、JDK 11。项目同时声明 Android/iOS 目标，因此 Windows 开发机也安装完整手机工作负载，避免还原多目标项目时缺包。

```powershell
dotnet workload install maui-mobile
dotnet restore ota-app\src\TelinkOta.Mobile\TelinkOta.Mobile.csproj
dotnet build ota-app\src\TelinkOta.Mobile\TelinkOta.Mobile.csproj `
  -f net7.0-android -c Debug `
  -p:AndroidSdkDirectory=<Android-SDK> `
  -p:JavaSdkDirectory=<JDK-11>
```

本机已生成可直接侧载的调试签名 APK：

```text
src/TelinkOta.Mobile/bin/Debug/net7.0-android/com.telink.bms.mobile-Signed.apk
```

连接已打开 USB 调试的手机后：

```powershell
adb install -r src\TelinkOta.Mobile\bin\Debug\net7.0-android\com.telink.bms.mobile-Signed.apk
```

Android 12 以上首次启动会请求“附近设备”扫描与连接权限；Android 11 及以下会请求定位权限，这是旧 Android BLE 扫描 API 的系统要求。App 不使用扫描结果推断位置。

正式发布必须使用项目自己的 Android keystore 签名，不能把调试签名 APK 作为量产发布包。

## iOS 构建与安装

Windows 可以恢复依赖并验证 C#/XAML 的 `net7.0-ios` 编译，但不能完成 Apple 签名或产生可安装到 iPhone 的 IPA。最终步骤必须在装有 Xcode 的 Mac 上执行，且需要 Apple Developer Team、Bundle ID `com.telink.bms.mobile` 对应的证书与 Provisioning Profile。

```bash
dotnet workload install maui-ios
dotnet restore ota-app/src/TelinkOta.Mobile/TelinkOta.Mobile.csproj
dotnet publish ota-app/src/TelinkOta.Mobile/TelinkOta.Mobile.csproj \
  -f net7.0-ios -c Release \
  -p:RuntimeIdentifier=ios-arm64 \
  -p:ArchiveOnBuild=true \
  -p:CodesignKey="Apple Development: ..." \
  -p:CodesignProvision="..."
```

也可在 Visual Studio/Xcode 对应环境中选择真实 iPhone 构建。`Info.plist` 已包含蓝牙用途说明和 `bluetooth-central` 后台模式；首次扫描由 iOS 弹出系统蓝牙授权。

## 手机操作流程

1. 打开蓝牙，让电池处于可广播状态。
2. 打开 App，允许系统蓝牙权限；等待 20 秒扫描或手动点击“扫描”。
3. 设备名称为空时，依据设备 UUID/RSSI 选择；连接后通过页面中的 MAC 与蓝牙名复核设备身份。
4. 点击“连接”进入电池监控。实时窗口每秒读取，完整单体窗口约每两秒读取。
5. 改名时输入 `BT_` 完整名或后缀；App 会二次确认并读回验证。当前设备 MTU=23 时，单次 Modbus 写帧限制使后缀最多 10 个字符。
6. OTA 页选择 BIN；预检通过后确认稳定供电再开始。升级中不要切断电池电源、关闭蓝牙、强杀 App 或让手机远离设备。

## 验证状态

- `net7.0-android` Debug APK：本机完整构建通过，0 warning / 0 error；APK Manifest 已核对包名、SDK 级别与蓝牙权限。
- `net7.0-ios` iOS Simulator RID：本机托管代码和 XAML 编译通过，0 warning / 0 error。
- `TelinkOta.Core.Tests`：94/94 通过。
- Windows 只读实机基线：`BT_cs-0604 / A4C13816025A` 的 D120、SN、HW、SW、蓝牙名均已验证。
- 当前没有通过 ADB 连接的 Android 手机，也没有 Mac/iPhone 签名环境，因此 Android/iPhone 的真实 BLE、改名持久化和 OTA 仍标记为“需手机实机验证”。在完成该验证前不得把手机端描述为量产验收通过。

## 发布前必须完成

1. Android 与 iPhone 各至少一台真机完成扫描、连接、30 分钟轮询、断连重连和前后台切换。
2. 用非生产测试名称完成改名、断电重启、重新扫描验证。
3. 用隔离样机完成 OTA 成功、取消、远离断连、低电量提示及设备重启重连矩阵。
4. 配置 Android release keystore、Apple Team/Profile、隐私说明、版本号与应用图标。
5. 由于当前仓库使用 .NET 7，提交应用商店前需结合当时商店/Xcode要求升级到仍受支持的 .NET MAUI 版本，并重新执行全部回归。
