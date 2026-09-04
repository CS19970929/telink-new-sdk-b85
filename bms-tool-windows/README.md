# Windows 上位机

本目录是 BMS 仓库中的独立 Windows 项目区，与固件源码并列维护。

- `BmsTool.Windows/`：客户版 BMS Assistant，面向客户交付。
- `BmsFactoryTest.Windows/`：内部完整测试版 BMS Assistant，基于客户版最新版功能，保留全部工程/调试/出厂测试页面，启动后不需要密码。

两个项目分别构建、分别发布；以后每次生成必须同时生成两个版本。客户版不包含工厂测试入口；内部完整测试版包含全部功能。涉及 Factory Session 的共享协议代码位于各项目自身源码中，并与 BMS 固件的 `docs/factory_test_protocol.md` 对照维护。

## 双版本发布约定

统一使用 `build-release.ps1` 发布两套 Windows x64、自包含、单文件 EXE：

```text
BmsTool.Windows\publish\customer-win-x64-<时间戳>\BmsTool.Windows.exe
BmsFactoryTest.Windows\publish\internal-full-win-x64-<时间戳>\BmsFactoryTest.Windows.exe
```

脚本会为每次发布创建新的时间戳目录，避免覆盖正在运行的旧 EXE，并输出两套 EXE 的 SHA-256、目标框架和当前 Git commit。客户版的高级页面仍由 `hs456` 控制；内部完整测试版不设置密码门槛，仅供研发、调试和出厂测试使用，不应作为客户交付包。

详细的仓库边界、构建入口和协作规则见 `../docs/bms_windows_joint_maintenance.md`。

## 客户版功能边界

客户版默认只提供日常使用所需的实时监控、设备信息/蓝牙名、事件日志和长期监控功能：

- 不编译 AFE 硬件保护参数读写代码；
- 不提供专业调试页和原始寄存器读写入口；
- 不包含出厂测试页；出厂测试仍使用独立的 `BmsFactoryTest.Windows` 项目；
- 软件保护/BMS 参数页和 OTA 页默认隐藏。

实时监控页右侧的“高级功能”按钮输入密码 `hs456` 后，才会显示软件保护/BMS 参数页和 OTA 页；再次点击可锁定。该密码是客户版 UI 访问控制，不替代固件侧权限控制。

BLE 搜索兼容现有两种 BMS 广播名前缀：`BT_` 和 `BT-`。搜索列表同时显示设备 MAC，连接时仍会执行 Modbus 探测确认设备类型。

单体电压按公共协议固定读取 32 个槽位；固件传输值 `61001` 表示该串不存在。客户版和工厂版均按该哨兵值过滤有效串，不把不存在的槽位当作 61.001 V，也不压缩原始串号。

连接探测兼容已部署的串口蓝牙模块：优先检查 `0xD120` 实时窗口 magic；如果该可选窗口未启用，则继续校验稳定的 `0xD000` Legacy 数据窗口或 `0xC002` 生产信息窗口。部分外置蓝牙模块不实现 BMS 的 `0x0000` MAC、`0x0100` 蓝牙名称或空的硬件/软件版本寄存器，此时设备信息页分别使用 BLE 连接地址、BLE 广播名称和“未知”，不影响实时数据和保护数据读取。

## OTA 双架构自动升级

OTA 页默认使用 `Auto（自动识别）`：

- 发现 Telink 专用 OTA service `00010203-0405-0607-0809-0a0b0c0d1912` 时，使用现有 Telink OTA START/DATA/END 流程；
- 发现 BMS Nordic UART service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` 时，使用 STM32 串口 IAP 流程：`0xFFFD` 进入 IAP、`0xFFFE` 每页写 1024 字节、`0xFFFF` 完成。由于现有 IAP 的实际解析约定，页帧使用 `0x10` 的扩展兼容格式：`byte count=0`、`quantity=1024`，不能使用标准 `byte count=0x400`（单字节无法表达）；
- STM32 APP BIN 必须能通过向量表校验（初始栈指针在 SRAM、复位向量在 `0x08001C00..0x0800F800`），按 `0x08001C00` APP 区使用，最大 55 KB，最后一页用 `0xFF` 补齐；已识别为 Telink 格式的文件会被拒绝；
- STM32 在 ENTER 应答后等待 BMS 复位并重新建立 GATT 连接；如果设备已经处于 IAP 且普通 Modbus 探测无响应，上位机会保留 GATT 连接并允许直接进入 OTA；之后每个升级帧按串口兼容分片发送，必须收到完整 Modbus ACK 才发送下一页；ACK 超时会停止，不会对没有序号的旧 IAP 例程盲目重传；
- 升级后统一重连并读取软件版本、实时数据验证。

当前 STM32 例程兼容模式能提供文件边界、Modbus CRC16、页 ACK 和升级后通信验证，但例程 IAP 本身没有镜像签名和完整镜像校验，不能单独作为量产级防篡改安全保证。正式量产前必须启用安全 IAP 协议。

## 客户版发布

发布产物应放在本仓库项目目录内，例如：

```text
BmsTool.Windows\publish\customer-win-x64\BmsTool.Windows.exe
```

使用 `dotnet publish` 生成 Windows x64、自包含、单文件 EXE。发布时应同时记录 EXE 的 SHA-256、源码 commit 和实际目标框架。
