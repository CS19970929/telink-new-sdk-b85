# BMSAssistantQt 跨平台实现说明

## 1. 目标

在不动现有 Swift/macOS 专用上位机的前提下，新建一套基于 Qt 的跨平台上位机，实现：

- macOS / Windows / Linux 共用一套 UI 与协议代码
- 兼容当前 `vendor/ble_sample` 的 BLE 通讯协议
- 保留日常调试能力
- 独立提供一页纯业务的 `电池状态` 页面

Qt 版工程路径：

```text
tc_ble_single_sdk/vendor/ble_sample/BMSAssistantQt
```

## 2. 技术选型

### 2.1 采用方案

- UI：`QtWidgets`
- BLE：`QtBluetooth`
- 语言绑定：`PySide6`

### 2.2 选择原因

这次没有继续走原生多端拆分，而是直接选 Qt，原因是：

1. 一套代码即可覆盖桌面三平台。
2. `QtBluetooth` 足够承载当前的 `BLE + SPP + Modbus RTU` 需求。
3. `PySide6` 比 C++ Qt 的本机工具链依赖更轻，当前环境更容易快速落地与验证。
4. 这套上位机以调试、测试、寄存器可视化为主，QtWidgets 的开发效率和维护成本更合适。

## 3. 模块边界

### 3.1 `protocol.py`

职责：

- `Modbus RTU` 编解码
- CRC16 校验
- 原始十六进制输入解析
- `ResponseAccumulator` 分片重组
- 寄存器目录 `RegisterCatalog`

### 3.2 `ble_transport.py`

职责：

- BLE 扫描
- 建立 `QLowEnergyController`
- 发现 `Telink SPP` 服务与两个特征
- 订阅响应特征通知
- 发送请求帧
- 接收通知数据

说明：

- 当前已经移除了 `QBluetoothLocalDevice` 依赖。
- 原因是当前 macOS 运行链路中，`QBluetoothLocalDevice` 会导致进程被系统级中断。
- 这不影响扫描、连接和收发主链路，只影响蓝牙状态标签的精细化表达。

### 3.3 `app_controller.py`

职责：

- 上位机核心状态管理
- 扫描列表过滤与排序
- 顺序命令编排
- 响应超时处理
- 身份信息刷新
- 电池状态刷新
- 手动寄存器读写
- 快捷写寄存器
- 日志记录与快照组织

### 3.4 `ui/main_window.py`

职责：

- 左侧扫描连接区
- 右侧 `电池状态 / 调试工作台` 双页
- 自动刷新控制
- 亮色 UI 风格
- 寄存器块与日志可视化

## 4. 功能对齐结果

Qt 版当前已覆盖这些能力：

- 扫描 BLE 设备
- 筛选 `BT* / 180F / 1812`
- 连接 / 断开
- 发现 `6E400001` 服务
- 发送 `Modbus RTU over BLE`
- 分片响应重组
- `Echo` 测试
- 刷新设备身份
- 刷新电池状态
- 读取保护参数预览
- 读取事件日志预览
- 手动读寄存器
- 手动写寄存器
- `写 SOC -> 0x1005`
- `写 0x1103 = 0x0003`
- 原始帧发送
- 蓝牙名后缀写入
- 报文日志
- 最近寄存器块快照

## 5. 电池状态页数据来源

Qt 版与 Swift 版保持一致：

1. 读取 `0xD000 ~ 0xD03E`
2. 读取 `0xD115 ~ 0xD116`
3. 读取 `0xD120 ~ 0xD12A`

数据优先级：

- 如果检测到 `0xD120` 页带 `magic = 0x4253`，则使用实时窗口数据
- 否则退回旧寄存器兼容模式

页面当前可展示：

- `Pack Voltage`
- `Pack Current`
- `SOC`
- `Max Temp / Min Temp / MOS Temp`
- `Cell 1 ~ Cell 10`
- `Cell Max / Cell Min / Cell Delta`
- `SOH`
- `Cycle Count`
- `Capacity Now / Full / Factory`
- `SystemStatus`
- 兼容原始测量

## 6. macOS 特殊说明

### 6.1 直接 `python main.py` 的问题

在 macOS 上，BLE 扫描会触发系统隐私权限检查。  
如果进程本身不是带有蓝牙权限说明的 `.app bundle`，系统可能直接终止进程。

本次验证中已确认：

- 纯 Python 进程下，窗口可以创建
- 纯 Python 进程下，调用 BLE 扫描时会被系统级中断
- 打包为 `.app` 后可以由 `open` 正常拉起

### 6.2 当前推荐启动方式

macOS 日常使用应走：

```bash
./scripts/run-macos-app.sh
```

而不是直接：

```bash
python main.py
```

### 6.3 权限键

`package-macos.sh` 已补写：

- `NSBluetoothAlwaysUsageDescription`
- `NSBluetoothPeripheralUsageDescription`

## 7. 当前验证结论

已完成验证：

- 源码 `py_compile` 通过
- `PySide6` / `PyInstaller` 安装通过
- 主窗口可成功实例化
- `AppController` 可成功实例化
- macOS `.app` 打包通过
- `.app` 可由系统 `open` 成功拉起

当前未在本轮自动化里完成的验证：

- 已打包 `.app` 内部的实际扫描与连接回归
- BLE 链路下的真实寄存器读写回归

原因：

- 在当前自动化执行环境中，直接走系统 UI 与权限弹窗闭环不可靠
- 但技术路径已经收敛到正确形态：macOS 必须走带 `Info.plist` 的 `.app`

## 8. 后续建议

下一阶段建议优先补这几项：

1. 在 Qt 版里实机回归扫描、连接、刷新状态、写 SOC。
2. 对 `SystemStatus` 和 `Protect` 做语义化解码。
3. 加单体电压历史曲线与 CSV 导出。
4. 如果需要串口调试，再补 `UART transport`。
