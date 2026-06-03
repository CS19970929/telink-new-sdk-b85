# BMSAssistantAndroid

## 定位

`BMSAssistantAndroid` 是配套 `vendor/ble_sample` BMS MCU 工程的 Android BLE 工程版首版。它不修改固件协议，直接复用仓库已有的 `Modbus RTU over BLE`、`docs/register_catalog.json` 与 `docs/protocol_test_vectors.json`。

首版面向工程调试，包含：

- BLE 扫描、连接、断开
- `6E400001` 服务发现
- `6E400002` 请求写入
- `6E400003` notify 响应接收
- 20 byte 单请求上限检查
- notify 分片重组与 CRC 校验
- 电池状态页自动刷新
- 手动读写寄存器
- Echo 测试
- 原始 Modbus RTU 帧发送
- 写 `SOC -> 0x1005`
- 写 `0x1103 = 0x0003`
- 蓝牙名后缀写入
- 报文日志与快照导出

不包含 OTA、云同步、账号体系或固件协议改造。

## 技术基线

- Android Gradle Plugin：`9.2.0`
- Gradle Wrapper：`9.4.1`
- JDK：`17`
- Kotlin：`2.3.21`
- Compose BOM：`2026.05.00`
- AndroidX Activity Compose：`1.13.0`
- `compileSdk / targetSdk`：`36`
- `minSdk`：`26`

AGP 9 使用内置 Kotlin 支持，工程未启用 `org.jetbrains.kotlin.android` 插件。

## 工程结构

```text
BMSAssistantAndroid/
├── app/
│   ├── build.gradle.kts
│   └── src/
│       ├── main/
│       │   ├── AndroidManifest.xml
│       │   ├── kotlin/bms/
│       │   │   ├── MainActivity.kt
│       │   │   ├── ble/
│       │   │   ├── data/
│       │   │   ├── domain/
│       │   │   ├── protocol/
│       │   │   └── ui/
│       │   └── res/values/styles.xml
│       └── test/kotlin/bms/BmsProtocolTest.kt
├── gradle/wrapper/
├── build.gradle.kts
├── settings.gradle.kts
├── gradle.properties
├── gradlew
└── gradlew.bat
```

`kotlin/bms` 是为了规避当前 Windows 深层工程路径的 `MAX_PATH` 风险，Kotlin package 仍保持 `com.telink.bmsassistant.*`。

## 运行与构建

在 Android 工程目录执行：

```bat
cd /d vendor\ble_sample\BMSAssistantAndroid
gradlew.bat :app:testDebugUnitTest
gradlew.bat :app:assembleDebug
```

调试包路径：

```text
app/build/outputs/apk/debug/app-debug.apk
```

安装到手机：

```bat
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

当前机器如果只有 Java 1.8，或未安装 Android SDK Platform 36，上述 Gradle 命令会失败。需要先安装 JDK17、Android SDK、Build Tools，并配置 `JAVA_HOME` 与 Android SDK 路径。

## 权限

Manifest 已声明：

- Android 12+：`BLUETOOTH_SCAN`、`BLUETOOTH_CONNECT`
- Android 11 及以下：`ACCESS_FINE_LOCATION`
- BLE 硬件特性：`android.hardware.bluetooth_le`

App 启动后会主动请求运行时权限。若扫描不到设备，优先确认系统蓝牙已开启、权限已授予，并且同一块 BMS 板未被 Windows 上位机或其他手机长时间占用。

## 协议动作

高层动作名称与共享 JSON 保持一致：

- `refreshIdentity`
- `refreshBatteryStatus`
- `readProtectPreview`
- `readEventLogPreview`
- `writeSOC`
- `writeRegister1103`
- `writeBluetoothNameSuffix`

关键协议约束：

- 请求帧必须完整落在单次 GATT Write 内
- 当前安全上限为 `20 byte`
- 响应通过 `6E400003` notify 按 `20 byte` 分片返回
- 上层按完整 `Modbus RTU` 帧做 CRC 校验与解析

## 实机联调步骤

1. 手机开启蓝牙并授予 App 权限。
2. 点击 `扫描`，优先查找 `BT*` 或广播服务含 `180F / 1812` 的设备。
3. 选择目标设备并连接，等待状态进入 `可收发`。
4. 在 `电池状态` 页确认自动刷新可读取 `0xD000 / 0xD115 / 0xD120` 数据。
5. 在 `调试工作台` 执行 Echo、手动读 `0xD120`、写 SOC、写 `0x1103`。
6. 导出电池快照 JSON 与报文日志 CSV。

写寄存器能力不会在工程版隐藏，但 UI 会在执行前弹出确认。
