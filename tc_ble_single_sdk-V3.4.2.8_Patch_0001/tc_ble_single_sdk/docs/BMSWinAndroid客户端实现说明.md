# BMS Windows 上位机与 Android BLE App 实现说明

## 1. 目标与范围

本次实现面向 `tc_ble_single_sdk/vendor/ble_sample` BMS MCU 工程，交付两类配套客户端：

- Windows 桌面蓝牙上位机：沿用并补强 `vendor/ble_sample/BMSAssistantQt`
- Android 原生 BLE App：新增 `vendor/ble_sample/BMSAssistantAndroid`

两端均不改动固件协议，统一使用：

- `docs/register_catalog.json`
- `docs/protocol_test_vectors.json`
- `docs/generated/BmsGeneratedRegisterCatalog.kt`
- `Modbus RTU over BLE`

首版以工程调试为主，不包含 OTA、云同步、账号体系或固件协议改造。

## 2. 固件 BLE 协议约束

当前固件仍按 Telink SPP 风格提供 BLE 字节通道：

- Service UUID：`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- Request Characteristic：`6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- Response Characteristic：`6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- 请求：单次 GATT Write，当前安全上限 `20 byte`
- 响应：通过 notify 按 `20 byte` 分片返回
- 业务帧：完整 `Modbus RTU`
- 常用功能码：`0x03`、`0x06`、`0x10`、`0x7F Echo`

客户端必须先完成服务发现和 notify 订阅，再允许发送业务命令。响应必须按完整 Modbus RTU 帧做长度推断、分片重组和 CRC16 校验。

## 3. Windows 上位机

路径：

```text
vendor/ble_sample/BMSAssistantQt
```

技术栈：

- `PySide6`
- `QtBluetooth`
- `QtWidgets`
- `PyInstaller`

本次补强点：

- `scripts/package-windows.bat` 在打包前自动执行 `python main.py --smoke-test`
- Windows 打包加入 `--collect-all PySide6`，减少 Qt 插件漏收集风险
- 交付包复制 `README.md`、`WINDOWS-DELIVERY.md` 和本跨平台说明文档
- `WINDOWS-DELIVERY.md` 补充 Windows 运行、打包和实机验收闭环

建议开发运行：

```bat
cd /d vendor\ble_sample\BMSAssistantQt
scripts\run.bat
```

建议交付打包：

```bat
cd /d vendor\ble_sample\BMSAssistantQt
scripts\package-windows.bat
```

有 BMS 板时，至少验证扫描 `BT*`、连接 READY、自动刷新、Echo、读 `0xD120`、写 SOC、写 `0x1103`、导出 CSV/JSON。

## 4. Android App

路径：

```text
vendor/ble_sample/BMSAssistantAndroid
```

技术栈：

- Kotlin `2.3.21`
- Android Gradle Plugin `9.2.0`
- Gradle Wrapper `9.4.1`
- Jetpack Compose，BOM `2026.05.00`
- `activity-compose 1.13.0`
- `compileSdk / targetSdk 36`
- `minSdk 26`

Android 工程按四层拆分：

- `ble`：扫描、连接、GATT 服务发现、notify 订阅、请求写入
- `protocol`：寄存器常量、CRC、Modbus 编解码、分片重组、20 byte 请求上限
- `domain`：设备身份、电池快照、寄存器块、报文日志和导出模型
- `data/ui`：命令编排、状态管理、设备列表、电池状态页、调试工作台

高层动作名称与共享资产保持一致：

- `refreshIdentity`
- `refreshBatteryStatus`
- `readProtectPreview`
- `readEventLogPreview`
- `writeSOC`
- `writeRegister1103`
- `writeBluetoothNameSuffix`

构建命令：

```bat
cd /d vendor\ble_sample\BMSAssistantAndroid
gradlew.bat :app:testDebugUnitTest
gradlew.bat :app:assembleDebug
```

当前仓库路径较深，Android 源码放在 `src/main/kotlin/bms` 下以降低 Windows `MAX_PATH` 风险；Kotlin package 仍为 `com.telink.bmsassistant.*`。

## 5. 验证

共享资产回归：

```bat
cd /d tc_ble_single_sdk
python script\bms_client_asset_tool.py verify
```

Android 单元测试覆盖：

- 请求帧向量
- 响应解析
- CRC 错误
- notify 分片重组
- 实时窗口优先于旧寄存器快照解码

本机当前限制：

- 当前机器没有可用 `gradle` 命令
- 当前 Java 为 1.8，Android 工程需要 JDK17
- 当前缺少 Android SDK / ADB，真实 APK 构建、安装与手机联调需补齐环境后执行

## 6. 参考

- Android Gradle Plugin 9.2.0：<https://developer.android.com/build/releases/agp-9-2-0-release-notes?refresh=1>
- AGP 内置 Kotlin：<https://developer.android.com/build/migrate-to-built-in-kotlin>
- Compose BOM：<https://developer.android.com/develop/ui/compose/bom>
- Kotlin Release：<https://kotlinlang.org/docs/releases.html>
- AndroidX Activity：<https://developer.android.com/jetpack/androidx/releases/activity>
