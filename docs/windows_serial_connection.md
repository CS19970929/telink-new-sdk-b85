# Windows 上位机直连串口设计与测试说明

## 目的

在不复制 BMS 协议和页面逻辑的前提下，为客户版 `BmsTool.Windows` 与完整版 `BmsFactoryTest.Windows` 增加直连 COM 口能力。BLE 和 COM 仅是物理传输适配，Modbus RTU、BMS 数据解析、软件保护参数和 Factory Session 继续共用。

## 实际串口参数

参数以仓库外部 STM32 APP/IAP 源码中的 SCI 初始化为准：

| 项目 | 设置 |
|---|---|
| 波特率 | 19200 |
| 数据位 | 8 |
| 校验 | None |
| 停止位 | 1 |
| 硬件流控 | None |
| 协议 | Modbus RTU，CRC16 |

`BmsSerialTransport` 使用 `System.IO.Ports.SerialPort`，关闭 DTR/RTS，接收事件只负责读取当前可用字节；`BmsClient` 继续负责按 Modbus 响应长度组帧和校验 CRC。

## 页面行为

1. 在“通信设置”选择 `串口`。
2. 点击“刷新”枚举 COM 端口，选择目标端口后点击“连接”。
3. 连接成功后依次执行原有 Modbus 探测、身份读取和实时数据读取。
4. 轮询失败达到原有阈值时，对同一 COM 端口关闭并重新打开后重试。
5. “断开”会释放 BMS 客户端、串口事件和串口句柄。

客户版仍不编译 AFE 硬件参数页；完整版即使显示该页，也只有 BLE 连接可以读写 AFE 硬件参数。软件保护参数、OTA 和完整版 Factory Session 可通过公共 Modbus 通道访问。

## STM32 串口 IAP

直连 COM 时 OTA 目标固定为 STM32 Serial IAP，使用现有命令：

- `0xFFFD`：进入 IAP；
- `0xFFFE`：写入 1024 字节页，页数据不足部分补 `0xFF`；
- `0xFFFF`：完成并复位到 APP。

每个命令都等待完整 Modbus 写应答并校验寄存器、数量和 CRC。BLE 传输仍按 20 字节分片；COM 直连使用 IAP 透明串口缓冲能力直接发送约 1033 字节完整页帧，避免把 BLE MTU 限制误用于串口。ACK 超时会停止本次升级，不会对没有页序号/幂等保证的旧 IAP 例程自动重传当前页。

升级结束后，程序重新打开 COM，重新执行 BMS 探测、软件版本读取和实时数据读取。没有真实 STM32 APP/IAP 板卡时，构建检查不能替代串口电气连接、复位时序和整包 ACK 的实机验证。

## 构建

两套版本统一执行：

```powershell
Set-Location "D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\bms-tool-windows"
.\build-release.ps1 -TargetFramework net7.0-windows10.0.19041.0
```

产物分别位于 `BmsTool.Windows\publish\customer-win-x64-<时间戳>` 和 `BmsFactoryTest.Windows\publish\internal-full-win-x64-<时间戳>`，均为 Windows x64、自包含、单文件 EXE。
