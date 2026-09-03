# Windows 上位机

本目录是 BMS 仓库中的独立 Windows 项目区，与固件源码并列维护。

- `BmsTool.Windows/`：客户版 BMS Assistant。
- `BmsFactoryTest.Windows/`：工厂版 BMS Assistant，基于客户版最新版功能，额外提供“出厂测试”页和 Factory Session 测试流程。

两个项目分别构建、分别发布。客户版不包含工厂测试入口；涉及 Factory Session 的共享协议代码位于各项目自身源码中，并与 BMS 固件的 `docs/factory_test_protocol.md` 对照维护。

详细的仓库边界、构建入口和协作规则见 `../docs/bms_windows_joint_maintenance.md`。

## 客户版功能边界

客户版默认只提供日常使用所需的实时监控、设备信息/蓝牙名、事件日志和长期监控功能：

- 不编译 AFE 硬件保护参数读写代码；
- 不提供专业调试页和原始寄存器读写入口；
- 不包含出厂测试页；出厂测试仍使用独立的 `BmsFactoryTest.Windows` 项目；
- 软件保护/BMS 参数页和 OTA 页默认隐藏。

实时监控页右侧的“高级功能”按钮输入密码 `hs456` 后，才会显示软件保护/BMS 参数页和 OTA 页；再次点击可锁定。该密码是客户版 UI 访问控制，不替代固件侧权限控制。

BLE 搜索兼容现有两种 BMS 广播名前缀：`BT_` 和 `BT-`。搜索列表同时显示设备 MAC，连接时仍会执行 Modbus 探测确认设备类型。

连接探测兼容已部署的串口蓝牙模块：优先检查 `0xD120` 实时窗口 magic；如果该可选窗口未启用，则继续校验稳定的 `0xD000` Legacy 数据窗口或 `0xC002` 生产信息窗口。部分外置蓝牙模块不实现 BMS 的 `0x0000` MAC、`0x0100` 蓝牙名称或空的硬件/软件版本寄存器，此时设备信息页分别使用 BLE 连接地址、BLE 广播名称和“未知”，不影响实时数据和保护数据读取。

## 客户版发布

发布产物应放在本仓库项目目录内，例如：

```text
BmsTool.Windows\publish\customer-win-x64\BmsTool.Windows.exe
```

使用 `dotnet publish` 生成 Windows x64、自包含、单文件 EXE。发布时应同时记录 EXE 的 SHA-256、源码 commit 和实际目标框架。
