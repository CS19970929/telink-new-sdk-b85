# Windows 主窗口交互约束

## BLE 设备选择

- 扫描器可能持续收到同一 MAC 的重复广播，并更新名称或 RSSI。
- 更新列表项时必须按 MAC 保留 `DeviceList` 的已选设备，不能因对象替换把选择重置为空。
- 连接仍以用户明确选择的设备为准；不自动选择第一台设备。

## 实时监控页

- 主窗口最小可用区域为 `900 x 600`，实时监控页在内容不足以完整显示时提供水平和垂直滚动。
- 实时监控内容宽度跟随滚动区域的可视宽度；`1180 x 760` 及更宽窗口不得因电芯列表产生水平溢出，只有低于 `1020` 的内容区域才允许水平滚动。
- 一级告警、二级告警和三级保护各自允许垂直滚动，长状态文字不能被固定行高裁剪。
- “系统状态”只显示充电 MOS、放电 MOS 的实际状态，以及两个对应的控制按钮。加热、制冷、均衡和 MOS 许可说明不在此区域重复显示。
- MOS 按钮仍修改固件定义的用户许可，实际输出继续受保护、采样资格和 AFE 门禁控制；确认对话框和日志保留这些安全信息。

## 业务参数页

- 页面保留字段、当前设备值、必要状态和操作按钮，不显示成段的常驻说明文字。
- 安全约束不得删除：输入校验、能力判断、写入确认、设备读回和失败门禁继续由原逻辑执行。

## 回归验证

```powershell
./test-main-window-experience.ps1 -CustomerAssembly <customer-dll> -InternalAssembly <internal-dll>
./test-business-parameters-ui.ps1 -CustomerAssembly <customer-dll> -InternalAssembly <internal-dll>
./test-communication-layout.ps1 -CustomerAssembly <customer-dll> -InternalAssembly <internal-dll>
./test-ble-connection.ps1 -TargetFramework net8.0
./test-ble-connection.ps1 -TargetFramework net48
./test-mos-control.ps1
```

这些检查是 Windows Host/离屏 WPF 证据，不代表真实电脑 DPI 组合、蓝牙适配器或 BMS 实板验收。
