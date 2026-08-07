# Telink B85m BLE OTA 工具（Windows 桌面版）

基于 Telink B85m/TLSR825x BLE Single Connection SDK V3.4.2.8 的稳定、安全 OTA 客户端。

协议事实依据：仓库根目录 [`OTA_PROTOCOL_FACTS.md`](../OTA_PROTOCOL_FACTS.md)（SDK 源码 + 实测固件 BIN + 官方 Telink OTA App 2.1.2 反汇编三方交叉核对）。

## 目录结构

```
ota-app/
├── TelinkOta.sln
├── src/
│   ├── TelinkOta.Core/         协议核心（无 UI、无平台 BLE 依赖，可移植）
│   │   ├── Ota/
│   │   │   ├── OtaConstants.cs         协议常量/UUID/超时
│   │   │   ├── Crc16.cs                CRC-16/MODBUS（数据包 CRC）
│   │   │   ├── Crc32.cs                Telink 固件 CRC32
│   │   │   ├── FirmwareParser.cs       BIN 解析与预检查（Size/Mark/CRC32/自动补齐）
│   │   │   ├── OtaPacketEncoder.cs     分包/补齐/Index/END/命令编码/Notify 解析
│   │   │   ├── OtaResult.cs            设备 Result 码 → 用户可操作建议
│   │   │   ├── OtaStateMachine.cs      严格单向状态机
│   │   │   ├── OtaSession.cs           会话编排（超时/取消/断连/重启重连/版本复核/背压）
│   │   │   ├── IBleTransport.cs        平台 BLE 抽象
│   │   │   └── ModbusRtu.cs            最小 Modbus RTU codec（帧构造/校验/解析）
│   │   └── Bms/
│   │       ├── BatteryStatus.cs        BMS 寄存器映射 + 电池快照解析（0xD120 稳定窗口/0xD000 兼容窗口）
│   │       └── ModbusSppClient.cs      SPP 上的 Modbus 请求/响应客户端（分片重组、单飞）
│   └── TelinkOta.App.Wpf/       Windows 桌面 App（WPF + Windows.Devices.Bluetooth）
│       ├── Ble/BleScanner.cs           扫描
│       ├── Ble/WindowsBleTransport.cs  BLE 传输适配器
│       ├── Services/BatteryMonitor.cs  电池状态轮询服务（1s 周期，稳定窗口优先）
│       └── ViewModels/MainViewModel.cs UI 状态
└── tests/
    └── TelinkOta.Core.Tests/    NUnit 单元测试（含跨平台测试向量 + 真实 BIN 集成向量）
```

## 协议要点（详见 OTA_PROTOCOL_FACTS.md）

- OTA Service `00010203-0405-0607-0809-0A0B0C0D1912` / Characteristic `...2B12`（同一特征收发 + Notify）
- Legacy（0xFF00/0xFF01）与 Extend（0xFF03~0xFF06）双协议，默认 Auto（Extend 优先，超时回退 Legacy）
- 数据包 `[Index(2 LE)][数据][CRC16(2 LE)]`，首包 Index=0，尾包 0xFF 补齐到 16 的倍数后参与 CRC16
- PDU 16~240（16 的整数倍），受 `MTU-7` 限制；本工程设备默认 MTU=23，**只支持 PDU=16**
- Firmware Mark @0x08 = `544C4E4B`("TLNK")；**Size@0x18 = 文件总长（含尾部 4 字节 Telink CRC32）**——`tl_check_fw2.exe` 后处理实测语义；尾部 CRC32 覆盖前 len-4 字节
- OTA End `[index_max][index_max ^ 0xFFFF]`；设备 Result 0x00~0x0E 全映射
- 设备端超时：packet 15s / process 180s（App 侧取 10s / 170s，留余量）

## 构建

需要 .NET 7 SDK（`dotnet --version` ≥ 7.0.400）。

```powershell
dotnet build ota-app\TelinkOta.sln
dotnet test  ota-app\tests\TelinkOta.Core.Tests\TelinkOta.Core.Tests.csproj   # 64 个用例
```

## 使用

1. `dotnet run --project ota-app\src\TelinkOta.App.Wpf`（或运行生成的 exe）。
2. 扫描（默认按名称过滤 `BT_`，可改）→ 选择设备。
3. **电池监控**：点"连接"→ 经 SPP 以 Modbus RTU 读取 0xD120 稳定窗口（总压/电流/SOC/温度/单体极值）+ 产品信息（序列号/硬件/软件版本），每秒刷新；稳定窗口失效自动回退 0xD000 兼容窗口（单体电压 + 状态字）。
4. 选择固件 BIN → 工具自动完成：Size/Mark/CRC32 校验；若 BIN 未带合法尾部 CRC32 会自动补齐并提示。
5. 点击"开始 OTA"。流程：连接 → 发现 OTA 服务 → 订阅通知 → MTU/PDU 协商 → 版本协商 → Start → 有界窗口传输（默认并发 6）→ End → 等待设备 Result → 设备重启 → 自动重连 → 版本复核（OTA 协商 + 尽力读取 BMS 软件版本寄存器 0xC022）→ 判定成功。OTA 期间电池监控自动暂停，升级完成后自动恢复。
6. 任一步失败/超时/断连都会给出明确日志与建议，**不会续传，必须从头重试**；设备双区机制保证失败不损坏旧固件。

### 关键设置

| 设置 | 默认 | 说明 |
|---|---|---|
| 协议 | Auto | Extend 优先，版本协商超时自动回退 Legacy |
| PDU 长度 | 16 | 设备 MTU=23 时只能 16；更大需固件启用大 MTU（`app_config.h` 改 `MTU_SIZE_SETTING`） |
| 发送窗口 | 6 | Write Without Response 有界并发窗口（平台背压，非固定延时） |
| 版本复核 | 开 | 升级后重连并复核版本 |
| 版本比较 | 关 | 本工程设备端未配置版本号，不建议开启 |

### 测试数据

`tests/TelinkOta.Core.Tests/TestData/real_fw.bin` 为真实设备固件（91076 B），用于：
- FW Size=0x163C4 / Mark=TLNK / SDK 版本串 `$$$tc_ble_single_sdk_V3.4.2.8$$$` 解析；
- 前 91072 字节 CRC32 = 0x814C0E73（= BIN 尾部存储值）实机向量。

## 可靠性设计

- **有界发送队列**：窗口内并发写入，窗口满即节流；窗口 10s 不释放判定停滞并中止（设备 packet timeout 15s）。
- **严格状态机**：任意失败收敛到 Cancelled/Failed/Disconnected/TimedOut；升级成功 = Result 确认 + 重启 + 重连 + 版本复核。
- **自动降级**：首包写入失败（PDU 超出设备 MTU）时自动以 PDU=16 从头重试一次。
- **超时全覆盖**：连接/发现/版本响应/停滞/Result/重启/重连/总超时，均小于设备端策略。
- **断点续传不实现**：断连后从头重试（协议未定义续传）。
- **电池监控与 OTA 互斥**：OTA 期间暂停轮询并释放链路，完成后自动恢复。

## 电池信息（Modbus RTU over SPP）

依据 `vendor/ble_sample/modbus_rtu.c` 实码与对接文档：

| 窗口 | 地址 | 内容 |
|---|---|---|
| 稳定窗口 | `0xD120` 起 11 寄存器 | Magic(0x4253)、版本、总压(V*100)、电流(int16 A*10，充电正/放电负)、SOC、最高/最低/MOS 温度((+40℃)*10)、单体最高/最低/压差(mV) |
| 兼容窗口 | `0xD000` 起 32 寄存器 | u16VCell[32]（[29]=电池温度ADC、[30]=MOS温度ADC、[31]=总压mV） |
| 状态字 | `0xD115` 起 2 寄存器 | SystemStatus |
| 序列号 | `0xC002` 起 16 寄存器 | ASCII |
| 硬件版本 | `0xC012` 起 16 寄存器 | ASCII |
| 软件版本 | `0xC022` 起 16 寄存器 | ASCII |

寄存器均为 Modbus 大端 u16；0x03 一次最多 125 寄存器；响应由固件按 20 字节分片 Notify，客户端自动重组。

> **重要（实机联调结论）**：SPP 写必须使用 **WriteWithResponse**。实机验证（TelinkOta.Diag）：该固件会丢弃 WriteWithoutResponse 的 SPP 写（Echo 无响应），而 WriteWithResponse 正常（与 Qt 上位机一致）。OTA 通道不受影响（按 Telink 官方规范保持 WriteWithoutResponse）。
>
> 若连接正常、Echo/异常响应能收到但 0x03 读完全无响应：说明**设备固件未实现 Modbus 0x03 读**（早期验证版固件只有 0x7F Echo + 异常回退），需先刷入含完整 Modbus 处理器的当前固件。

## 已知限制与后续

- Windows 不公开 ATT MTU 查询接口，`MaxWriteLength` 取自 `GattSession.MaxPduSize-7`；错误超限由首包失败自动降级兜底。
- 未打包为 MSIX：直接运行需系统允许该应用使用蓝牙（首次扫描可能弹出系统权限提示）。
- 升级后版本复核中的 BMS 版本读取（Modbus 0x03 @ 0xC022）为尽力而为，读不到不影响成功判定。
- Android/iOS 版按同一协议事实另起工程（协议核心不依赖平台 BLE API，可平移）。
