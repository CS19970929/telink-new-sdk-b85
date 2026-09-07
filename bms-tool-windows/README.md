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

BLE 搜索兼容现有两种 BMS 广播名前缀：`BT_` 和 `BT-`。对于 STM32 IAP 可能出现的空广播名，上位机会先确认设备确实提供 Nordic UART service，再加入列表，避免把周围其他无名称 BLE 外设显示为 BMS。搜索列表同时显示设备 MAC，连接时仍会执行 Modbus 探测确认设备类型。

单体电压按公共协议固定读取 32 个槽位；固件传输值 `61001` 表示该串不存在。客户版和工厂版均按该哨兵值过滤有效串，不把不存在的槽位当作 61.001 V，也不压缩原始串号。

连接探测兼容已部署的串口蓝牙模块：优先检查 `0xD120` 实时窗口 magic；如果该可选窗口未启用，则继续校验稳定的 `0xD000` Legacy 数据窗口或 `0xC002` 生产信息窗口。部分外置蓝牙模块不实现 BMS 的 `0x0000` MAC、`0x0100` 蓝牙名称或空的硬件/软件版本寄存器，此时设备信息页分别使用 BLE 连接地址、BLE 广播名称和“未知”，不影响实时数据和保护数据读取。

## 直连串口

通信设置左侧可切换 `BLE` / `串口`。选择串口后可在波特率框选择或输入波特率，再点击“刷新”枚举 Windows 当前可用的 COM 端口；默认参数来自 BMS/IAP 的 SCI 初始化：`19200 baud、8 data bits、No parity、1 stop bit、No hardware flow control（19200 8N1）`。波特率必须与 BMS/UART 模块两端一致。串口接收按任意长度分片交给同一套 Modbus RTU 组帧器，不依赖一次接收完整响应，因此普通监控、身份读取、BMS 软件参数和内部 Factory Session 测试均复用 BLE 相同的上层逻辑。

串口连接会记录 `OPEN/WRITE/RX_FRAGMENT/READ_FAIL` 日志，并在轮询连续失败后自动重连。串口没有 BLE 地址，设备身份读取仍以 BMS 寄存器为准；如果 MAC 或蓝牙名称寄存器不存在，界面使用 `串口 COMx` 作为连接端点回退信息。AFE 硬件保护参数写入只允许 BLE 连接，防止把 BLE 专用硬件参数通道误用到直连串口。

## OTA 双架构自动升级

OTA 页默认使用 `Auto（自动识别）`：

- 发现 Telink 专用 OTA service `00010203-0405-0607-0809-0a0b0c0d1912` 时，使用现有 Telink OTA START/DATA/END 流程；
- 发现 BMS Nordic UART service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` 时，使用 STM32 串口 IAP 流程：`0xFFFD` 进入 IAP、`0xFFFE` 每页写 1024 字节、`0xFFFF` 完成。由于现有 IAP 的实际解析约定，页帧使用 `0x10` 的扩展兼容格式：`byte count=0`、`quantity=1024`，不能使用标准 `byte count=0x400`（单字节无法表达）；
- 直连 `COM` 时自动选择 STM32 Serial IAP，使用同一组 `0xFFFD/0xFFFE/0xFFFF` 命令；进入 IAP 和每一页发送后都等待并校验 Modbus ACK。BLE 链路按 20 字节分片，直连串口按 IAP 缓冲区能力直接发送约 1033 字节页帧，不把 BLE 分片规则误套到 COM；ACK 超时仍立即停止，禁止对无页序号的例程盲目重传；
- STM32 APP BIN 必须能通过向量表校验（初始栈指针在 SRAM、复位向量在 `0x08001C00..0x0800F800`），按 `0x08001C00` APP 区使用，最大 55 KB，最后一页用 `0xFF` 补齐；已识别为 Telink 格式的文件会被拒绝；
- STM32 在 ENTER 应答后等待 BMS 复位并重新建立 GATT 连接；如果设备已经处于 IAP 且普通 Modbus 探测无响应，上位机会保留 GATT 连接并允许直接进入 OTA；之后每个升级帧按串口兼容分片发送，并对每个分片使用带响应写入，按 19200 波特率和安全余量动态排空，不重复增加固定等待，必须收到完整 Modbus ACK 才发送下一页；ACK 超时会停止，不会对没有序号的旧 IAP 例程盲目重传；
- 升级后统一重连并读取软件版本、实时数据验证。

当前 STM32 例程兼容模式能提供文件边界、Modbus CRC16、页 ACK 和升级后通信验证，但例程 IAP 本身没有镜像签名和完整镜像校验，不能单独作为量产级防篡改安全保证。正式量产前必须启用安全 IAP 协议。

## 客户版发布

发布产物应放在本仓库项目目录内，例如：

```text
BmsTool.Windows\publish\customer-win-x64\BmsTool.Windows.exe
```

使用 `dotnet publish` 生成 Windows x64、自包含、单文件 EXE。发布时应同时记录 EXE 的 SHA-256、源码 commit 和实际目标框架。


## SH3673520 保护参数扩展（2026-09-07）

客户版和完整版新增“3520 保护参数”页，共用 `Shared` 中的界面和物理单位编解码。客户版随高级功能解锁可见；此前“客户版不包含 AFE 硬件参数”的限制仅针对旧 SH367309 页面。
连接后先读取，分别编辑和保存 MCU 软件保护、AFE 硬件保护。当前保护模式、同/分口、WDT和SPI类型只读显示；未启用的兼容软件字段只读。
新固件通过0x2500型号/版本识别，旧0x2400寄存器含义不变。写入范围/步进/滞回由两端校验；硬件ACK还必须结合回读应用状态。
详细参数、协议及当前固件Flash容量阻碍见 [3520 参数说明](docs/3520_parameters.md)。尚未完成实板联调，不能给未升级的板子写新参数。
BLE整组写要求MTU≥60及桥接模块支持57字节整帧，否则使用直连串口。旧BLE、Factory Session保留；已识别3520不能使用起址不同的旧STM32 OTA。
运行 `./test-3520-parameters.ps1` 检查共享编解码；运行 `./build-release.ps1` 同时生成两版EXE。构建中间文件转到 `%LOCALAPPDATA%/CodexTemp/bms-tool-windows`，正式EXE仍在各项目publish目录。


## 事件日志100/500条（2026-09-07）

客户版与完整版的事件日志页自动识别100或500条容量，无需用户选择，每次点击都重新识别。先读前100条，再读第二页；仅第二页返回本固件的地址非法异常（功能0x03、错误码0x01）时识别为100条，其余成功读取5页后识别为500条。超时、CRC错误（本协议0x02）、短响应、其他异常以及第三页及以后失败均报错，不降级，保留此前显示数据。不能用有效日志数量判断设备容量。

继续使用`0xC008`起始窗口，每条占一个寄存器，高字节为事件、低字节为间隔。每次读100字；500条依次读取`C008/C06C/C0D0/C134/C198`，共5帧，BLE和串口共用，未改变Modbus帧格式或OTA/Factory Session逻辑。读取期间停轮询并防止重复点击，全部成功后更新列表。

`test-event-log.ps1`对两版页面一致性、真实分页读取代码的100/500条顺序、旧100条边界、短响应和中途失败进行测试，生成文件仅位于用户临时区。固件对应`Flash.h`的`FLASH_STORAGE_LOG_RECORD_COUNT`宏（100U或500U）。

最新双版EXE通过`build-release.ps1 -ReleaseTag log-auto-20260907`生成：

- 客户版：`BmsTool.Windows/publish/customer-win-x64-log-auto-20260907/BmsTool.Windows.exe`
- 完整版：`BmsFactoryTest.Windows/publish/internal-full-win-x64-log-auto-20260907/BmsFactoryTest.Windows.exe`

两版发布和分页测试通过；尚未进行实板串口/BLE的500条读取验证。跨帧期间BMS仍可能产生新日志，当前协议不提供冻结快照。
