# BMSAssistantQt

## 项目说明

`BMSAssistantQt` 是基于 `PySide6 + QtBluetooth + QtWidgets` 的跨平台 BLE 上位机，实现目标是把当前 `BMSAssistant` 的核心功能完整迁移到 Qt：

- BLE 扫描、连接、断开
- `Telink SPP` 服务发现与通知订阅
- `Modbus RTU over BLE` 收发、CRC 校验、响应分片重组
- 独立的 `电池状态` 页面
- 保留完整能力的 `调试工作台`
- 手动读写寄存器
- `写 SOC -> 0x1005`
- `写 0x1103 = 0x0003`
- 原始帧发送
- 蓝牙名后缀写入
- 响应预览、寄存器块快照、报文日志
- `QSettings` 配置持久化
- `CSV` 报文日志导出
- `JSON` 电池快照导出

## 工程结构

```text
BMSAssistantQt/
├── main.py
├── requirements.txt
├── README.md
├── scripts/
│   ├── run.sh
│   ├── run.bat
│   ├── run-macos-app.sh
│   ├── package-macos.sh
│   ├── package-linux.sh
│   └── package-windows.bat
└── bmsassistantqt/
    ├── app_controller.py
    ├── ble_transport.py
    ├── models.py
    ├── protocol.py
    └── ui/
        └── main_window.py
```

## 功能对齐说明

### 1. 左侧扫描与连接

- `扫描模式`
  - `全部设备`：默认模式，先确保不漏设备
  - `当前固件`：再按 `BT* / 180F / 1812` 过滤显示
- 支持设备名搜索
- 支持只显示疑似 BMS 设备
- 支持连接所选设备与主动断开
- 已接 `deviceDiscovered + deviceUpdated`，用于接收 `scan response` 里的名称和补充字段
- 扫描改为 `BLE-only` 连续窗口，避免 `4s` 短扫描漏掉 `800ms` 广播设备
- 列表会额外显示设备 `ID`，即使没拿到 `BT_DEFAULT` 名字也能定位匿名设备
- 扫描条件会持久化到本地，下次启动自动恢复

### 2. 电池状态页

该页只放业务数据显示，不放调试控件。

读取顺序与 Swift 版保持一致：

1. `0xD000 ~ 0xD03E`
2. `0xD115 ~ 0xD116`
3. `0xD120 ~ 0xD12A`

页面包含：

- `Pack Voltage`
- `Pack Current`
- `SOC`
- `Max Temp / Min Temp / MOS Temp`
- `Cell Max / Cell Min / Cell Delta`
- `SOH / Cycle Count / Capacity`
- `Cell 1 ~ Cell 10`
- `SystemStatus`
- 兼容原始测量
- 连接与版本信息
- 寄存器快照
- `自动刷新` 默认开启，且只在 `电池状态` 页工作
- 支持导出当前 `JSON` 电池快照

### 3. 调试工作台

保留日常调试能力：

- 刷新设备身份
- 读取系统状态
- 读取保护参数预览
- 读取事件日志预览
- 手动读寄存器
- 手动写寄存器
- `Echo` 链路测试
- 原始帧发送
- 蓝牙名后缀写入
- 最近响应
- 最近寄存器块
- 报文日志
- 支持导出 `CSV` 报文日志

## 运行方式

### macOS

macOS 上推荐直接跑 `.app`，因为 BLE 扫描需要进程本身带有蓝牙权限描述。

```bash
cd "/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/BMSAssistantQt"
./scripts/run-macos-app.sh
```

### Linux

```bash
cd "/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/BMSAssistantQt"
./scripts/run.sh
```

### 开发直跑

`run.sh` 适合 Linux 和 Windows，或 macOS 上只做 UI/非扫描自检。

```bash
cd "/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/BMSAssistantQt"
./scripts/run.sh
```

### Windows

```bat
cd /d "...\tc_ble_single_sdk\vendor\ble_sample\BMSAssistantQt"
scripts\run.bat
```

脚本会自动：

- 创建 `.venv`
- 安装 `requirements.txt`
- 启动 Qt 上位机

macOS 例外：

- `run-macos-app.sh` 会优先打开带 `Info.plist` 的 `.app`
- 如果 `.app` 不存在，会先自动执行一次 `package-macos.sh`
- 如果界面提示 `error.PoweredOffError`，不要先把它理解成“系统蓝牙真的关闭”。
  在 macOS + QtBluetooth 下，这通常也可能表示当前 App 还没有蓝牙权限。
  先到“系统设置 -> 隐私与安全性 -> 蓝牙”里允许 `BMSAssistantQt`，然后彻底退出 App 再重开。

## 打包方式

### macOS

```bash
./scripts/package-macos.sh
```

输出目录：

```text
.dist/BMSAssistantQt.app
```

脚本会补写蓝牙权限说明：

- `NSBluetoothAlwaysUsageDescription`
- `NSBluetoothPeripheralUsageDescription`

### Linux

```bash
./scripts/package-linux.sh
```

输出目录：

```text
.dist/BMSAssistantQt
```

### Windows

```bat
scripts\package-windows.bat
```

输出目录：

```text
.dist\BMSAssistantQt
```

同时会额外生成：

```text
.dist\Launch-BMSAssistantQt.bat
.dist\docs\README.md
.dist\docs\WINDOWS-DELIVERY.md
```

## 协议边界

### BLE

- Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- Request Characteristic: `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- Response Characteristic: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

### 当前默认单包约束

当前仍按固件默认 `MTU=23` 的安全路径处理，单包请求长度上限为 `20 byte`。

因此：

- `0x10` 写多寄存器建议不超过 `5 words`
- 蓝牙名写入建议不超过 `10 个 ASCII byte`

## 跨平台说明

这套实现选的是 Qt 官方技术栈：

- UI: `QtWidgets`
- BLE: `QtBluetooth`
- 语言绑定: `PySide6`

这意味着一套代码可以覆盖：

- macOS
- Windows
- Linux

如果后续你要继续往下走，可以直接在这套工程上加：

- 单体电压历史曲线
- 保护状态语义化解码
- UART transport
- OTA 页面
- CSV 导出与抓包归档
