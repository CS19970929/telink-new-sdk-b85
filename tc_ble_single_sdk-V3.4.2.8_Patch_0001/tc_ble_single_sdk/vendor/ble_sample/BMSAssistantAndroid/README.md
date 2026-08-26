# BMSAssistantAndroid

## 定位

`BMSAssistantAndroid` 是配套 `vendor/ble_sample` BMS MCU 工程的 Android BLE 工程版。普通 BMS 通信直接复用仓库的 `Modbus RTU over BLE`、`docs/register_catalog.json` 与 `docs/protocol_test_vectors.json`；OTA 按 `docs/BMS_OTA_通信规范_V1.0.md` 与 `docs/ota_test_vectors.json` 实现。

当前包含：

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
- Telink Legacy OTA 工程入口

不包含云同步、账号体系；当前 OTA 仍定位为工程联调能力。

## 技术基线

- Android Gradle Plugin：`9.2.0`
- Gradle Wrapper：`9.4.1`
- JDK：`17`
- Kotlin：`2.3.21`
- Compose BOM：`2026.05.00`
- AndroidX Activity Compose：`1.13.0`
- `compileSdk / targetSdk`：`36`
- `minSdk`：`26`

## 工程结构

```text
BMSAssistantAndroid/
├── app/src/main/kotlin/bms/
│   ├── MainActivity.kt
│   ├── ble/
│   ├── data/
│   ├── domain/
│   ├── protocol/
│   ├── ui/
│   └── ota/
│       ├── OtaActivity.kt
│       ├── TelinkOtaBleClient.kt
│       ├── TelinkOtaCodec.kt
│       └── TelinkOtaSession.kt
├── app/src/test/kotlin/bms/
│   ├── BmsProtocolTest.kt
│   └── TelinkOtaCodecTest.kt
├── gradle/wrapper/
├── build.gradle.kts
├── settings.gradle.kts
├── gradle.properties
├── gradlew
└── gradlew.bat
```

`kotlin/bms` 用于规避 Windows 深层工程路径的 `MAX_PATH` 风险，Kotlin package 仍保持 `com.telink.bmsassistant.*`。

## 构建

```bat
cd /d vendor\ble_sample\BMSAssistantAndroid
gradlew.bat :app:testDebugUnitTest
gradlew.bat :app:assembleDebug
```

APK：

```text
app/build/outputs/apk/debug/app-debug.apk
```

安装：

```bat
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

仓库同时通过 `.github/workflows/bms-client-ci.yml` 自动执行 Android unit test。

## App 入口

当前工程版本在同一个 APK 中暴露两个 Launcher Activity：

- `BMS Assistant Android`：普通 BMS 状态/工程调试
- `BMS OTA`：独立 Telink OTA 工具

这样 OTA 实机验证不会干扰普通 BMS 通信。OTA 稳定后可再把它收进主 App 的设置/升级页并移除第二个 Launcher。

## OTA V1

OTA Service：

```text
00010203-0405-0607-0809-0A0B0C0D1912
```

OTA Characteristic：

```text
00010203-0405-0607-0809-0A0B0C0D2B12
```

实现：

- `TelinkOtaCodec`：bin 校验、START/DATA/END/RESULT、CRC16
- `TelinkOtaBleClient`：扫描、连接、OTA GATT discovery、Notify、Write With Response
- `TelinkOtaSession`：严格串行 OTA 状态机
- `OtaActivity`：选 bin、进度、确认、结果显示

固件选择后必须通过：

- offset `0x08` = `KNLT`
- offset `0x18` firmware size 合法

OTA 成功判据必须是收到：

```text
06 FF 00
```

而不是仅以所有 GATT 写操作完成判定。

## OTA 真机步骤

1. 安装 debug APK。
2. 启动 `BMS OTA`。
3. 授予 BLE 权限。
4. 扫描并选择目标 BMS。
5. 点击“连接 OTA”，等待 `OTA characteristic ready`。
6. 通过 Android 系统文件选择器选择 `firmware.bin`。
7. 确认页面显示 firmware size / packet count。
8. 开始 OTA，禁止中途断电。
9. 等待 `OTA_SUCCESS`。
10. BMS 重启后回主 App，重新连接并读取软件版本确认升级结果。

## 普通 BLE 实机步骤

1. 手机开启蓝牙并授予 App 权限。
2. 点击 `扫描`，优先查找 `BT*` 或广播服务含 `180F / 1812` 的设备。
3. 选择目标设备并连接，等待状态进入 `可收发`。
4. 在 `电池状态` 页确认自动刷新可读取 `0xD000 / 0xD115 / 0xD120` 数据。
5. 在 `调试工作台` 执行 Echo、手动读 `0xD120`、写 SOC、写 `0x1103`。
6. 导出电池快照 JSON 与报文日志 CSV。

## 安全边界

当前固件 `BLE_APP_SECURITY_ENABLE=0`。写寄存器和 OTA 当前均为工程能力；正式客户版本必须增加权限/鉴权与固件可信校验后再开放。
