# BMSAssistant

## 目标

`BMSAssistant` 是面向当前 `vendor/ble_sample` 固件的 macOS BLE 调试上位机，定位是你日常开发、联调、测试时的桌面工具。

当前实现重点：

- BLE 扫描、连接、断开
- Telink SPP 通道发现与通知订阅
- `Modbus RTU over BLE` 请求/响应收发
- 独立的“电池状态”页面
- 设备身份刷新
- 系统状态、保护参数、事件日志预览
- 手动读写寄存器
- 原始帧发送
- 原始报文日志

## 打开方式

推荐直接打开以下包根目录：

- `vendor/ble_sample/BMSAssistant/Package.swift`

如果你更习惯从 `vendor/ble_sample` 根目录打开，也可以使用：

- `vendor/ble_sample/Package.swift`

## 构建方式

```bash
cd vendor/ble_sample/BMSAssistant
swift build
```

## 运行方式

```bash
cd vendor/ble_sample/BMSAssistant
./scripts/run-macos-app.sh
```

脚本会先执行 `swift build`，再自动生成标准 macOS `.app`：

- 输出目录：`vendor/ble_sample/BMSAssistant/.dist/BMSAssistant.app`
- 启动方式：自动 `open` 该 `.app`

首次启动 macOS 会弹出蓝牙权限框，请允许访问。这里不能直接使用 `swift run BMSAssistant` 作为日常运行方式，因为 SwiftPM 默认启动的是裸可执行文件，当前会被 macOS TCC 以蓝牙隐私描述校验拒绝；包装成 `.app` 后可以正常运行。

## 当前协议约束

当前工具按现有固件事实实现，关键约束如下：

- 请求写入特征：`6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- 响应通知特征：`6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- 当前固件默认 MTU 为 `23`
- 因此 BLE 单包安全请求长度按 `20 byte` 控制
- `0x10` 写多寄存器建议不超过 `5 words`
- 蓝牙名 suffix 通过 BLE 写入时，当前建议不超过 `10 ASCII byte`

## 电池状态页

电池状态页与调试工作台已经分离：

- `电池状态`：用于日常查看关键状态量
- `调试工作台`：保留原始调试能力，不放到状态页里

当前电池状态页读取固件新增的实时寄存器窗口：

- 地址范围：`0xD120` ~ `0xD12A`
- 内容包括：
  - `Pack Voltage`
  - `Pack Current`
  - `SOC`
  - `Max Temp`
  - `Min Temp`
  - `MOS Temp`
  - `Cell Max`
  - `Cell Min`
  - `Cell Delta`

如果板子还是旧固件，上位机会提示“当前固件未提供电池状态窗口，请重新刷写最新固件”。

为了兼容未刷入新固件的板子，电池状态页也会同步读取旧寄存器：

- `0xD000` ~ `0xD03E`
- `0xD115` ~ `0xD116`

因此旧板子现在也能直接看到：

- `Cell 1 ~ Cell 10` 单串电压
- `Pack Voltage`
- `Pack Current`
- `SOC`
- `Max/Min/MOS Temp`
- `SystemStatus` 状态字
- 电池温度 / MOS 温度 ADC 原始值

`0xD120~0xD12A` 实时窗口仍然有价值，因为它把这些业务量做成了稳定协议页，不再依赖当前工程 `stCell_Info` 的内存平铺布局。

## 后续建议

下一阶段建议继续补：

1. 会话录包导出
2. 常用寄存器模板与分组显示
3. 事件日志语义化解码
4. UART transport
5. OTA 页面
