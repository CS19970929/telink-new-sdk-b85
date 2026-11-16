# 无 Comm Tool 的 BMS 通信接入指南

> 固定来源说明：本文来自comm-tool-branch@5521e851，保留当时的行为记录。当前目录、Keil工程、双协议合同及验证范围见[当前入口](../README.md)。


文档状态：CURRENT

源码验证：FULL（BMS App）/ PARTIAL（客户侧硬件需现场确认）

最后更新时间：2026-09-09

## 1. 直接结论

客户没有 Comm Tool 时，仍然可以读取 BMS 信息。优先方案是使用**支持 250 kbit/s、29-bit 扩展帧和 11-bit 标准帧的活动型 USB-CAN**：

- 只需要总压、电流、SOC、SOH、温度、故障和容量摘要：直接监听周期广播即可，不需要发送命令。
- 需要单体电压、全部温度、完整故障字、产品信息：通过标准帧 App 服务发送 `READ_BLOCK`。
- 需要使用现有完整 Windows GUI：当前仍需 Comm Tool；GUI 尚未原生接入 PCAN/Kvaser/slcan。
- 也可走 BMS Modbus RTU 串口，但必须先确认板端电气层是 RS485、TTL UART 还是其他接口，不能直接按软件参数接线。

## 2. 方案对比

| 方案 | 额外硬件 | 可读范围 | 能否使用现有 GUI | 适合场景 |
|---|---|---|---|---|
| A. 通用 USB-CAN 被动监听 | USB-CAN | 广播摘要 | 否，使用厂商工具或现有 CLI | 最快验证、只读监控 |
| B. 通用 USB-CAN 主动读取 | USB-CAN | 完整寄存器与产品信息 | 当前否，需脚本/二次开发 | 客户集成、完整数据采集 |
| C. Modbus RTU 直连 | 匹配电气层的 USB 转换器 | 完整 Modbus 寄存器 | 当前 GUI 的“直连”仍不支持无 Comm Tool | 设备有明确串口接口时 |
| D. 客户 ECU/网关集成 | 车载 ECU/CAN 网关 | 由软件实现决定 | 否 | 量产系统 |
| E. 采购/配套 Comm Tool | Comm Tool | 完整功能及 IAP | 是 | 最低软件改造成本 |

## 3. 方案 A：通用 USB-CAN 被动读取

### 3.1 硬件与总线设置

1. 连接 BMS 的 `CAN_H`、`CAN_L`，建议同时连接信号地。
2. 总线两端各 120 Ω；断电测量 `CAN_H-CAN_L` 约 60 Ω。
3. USB-CAN 设置为 250 kbit/s、Normal/Active、同时显示标准帧和扩展帧。
4. 不要使用 Listen-only/Silent 作为唯一对端。BMS 关闭自动重发，但物理层仍需要其他活动节点产生 ACK。
5. 上电后观察以下扩展 ID：

| ID | 内容 | 实际周期 |
|---|---|---:|
| `0x14F80200` | 总压、电流 | 约 100 ms |
| `0x14F80201` | 当前/设计能量 | 约 500 ms |
| `0x14F80202` | 充电状态、SOC、温度 | 约 100 ms |
| `0x14F80203` | SOH、循环次数 | 约 500 ms |
| `0x14F80204` | 简版协议/软件版本 | 约 500 ms |
| `0x14F80205` | MOS、故障、容量 | 约 500 ms |
| `0x14F80208` | 工厂容量、占位老化数据、日期 | 约 500 ms |

完整字段见 [BMS CAN 通信协议完整说明](https://github.com/CS19970929/103-309-template/blob/5521e851d77e517b76bec679dd215bbd3e2d4528/docs/protocol/can_protocol.md)。

### 3.2 使用仓库现有命令行工具

安装依赖：

```powershell
py -3.9 -m pip install python-can
```

检测接口：

```powershell
.\tools\start_can_bms_host.ps1 -Mode detect
```

以 PCAN 为例监听 30 秒：

```powershell
.\tools\start_can_bms_host.ps1 -Mode listen `
  -Interface pcan `
  -Channel PCAN_USBBUS1 `
  -Bitrate 250000 `
  -Duration 30
```

以 slcan 串口型适配器为例：

```powershell
.\tools\start_can_bms_host.ps1 -Mode listen `
  -Interface slcan `
  -Channel COM3 `
  -Bitrate 250000 `
  -Duration 30
```

具体 interface/channel 名称取决于厂商驱动和 python-can 后端。现有脚本常用 PCAN 默认值为 `pcan / PCAN_USBBUS1 / 250000`。

### 3.3 广播读取的边界

广播足以获得常用摘要，但不能提供：

- 每一节单体电压；
- 全部温度通道；
- 完整故障位图；
- 32 字节序列号、硬件版本、软件版本；
- 任意 Modbus 映射寄存器。

此外，`0x14F80208` 当前把老化进度和剩余时间固定为 100，不能作为真实老化数据。

## 4. 方案 B：通用 USB-CAN 主动读取完整数据

### 4.1 请求规则

默认 BMS App 地址为 0 时：

- 请求标准 ID：`0x060`；
- 应答标准 ID：`0x061`；
- DLC：8；
- 请求格式：`A5 5A CMD ARG0 ARG1 ARG2 CRC_H CRC_L`；
- CRC16：Modbus 算法，计算前 6 字节，但帧尾按高字节、低字节发送。

如果现场地址为 `node`，则 ID 为：

```text
request = (node << 7) | 0x60
ack     = (node << 7) | 0x61
```

### 4.2 首次联调建议帧

先读状态：

```text
TX  STD ID=0x060 DLC=8  A5 5A 01 00 00 00 1F 41
RX  STD ID=0x061 DLC=8  5A A5 01 STATUS SOC SOH CRC_H CRC_L
```

读取基本实时区 63 words：

```text
TX  STD ID=0x060 DLC=8  A5 5A 06 D0 00 3F 82 01
```

BMS 先返回 READ_BLOCK ACK，随后每个 word 一帧：

```text
5A A5 86 SEQ VALUE_H VALUE_L CRC_H CRC_L
```

读取产品信息 48 words：

```text
TX  STD ID=0x060 DLC=8  A5 5A 06 C0 02 30 23 41
```

产品信息返回 96 字节，按 32 字节切分为序列号、硬件版本、软件版本 ASCII。

### 4.3 主机实现要求

- 每条请求都校验 ID、DLC、magic、CMD、STATUS 和 CRC。
- READ_BLOCK 必须检查初始 ACK、每帧 `SEQ` 连续性以及总帧数。
- BMS block 数据帧间隔约 10 ms，完整读取 120 words 时要留足超时。
- 一次只执行一个请求；新命令会终止正在发送的 block stream。
- BMS 无硬件自动重发，主机应在超时后做有限次数应用层重试。
- 读取优先，不要在未核实权限和范围时尝试写寄存器。

仓库现有 `tools/can_bms_host.py` 可以监听广播、读取状态和执行升级，但当前没有暴露 `READ_REG`/`READ_BLOCK` 命令行子命令。因此完整读取有三种现实选择：

1. 使用 USB-CAN 厂商提供的脚本 SDK，按本文组帧；
2. 基于 python-can 编写一个轻量客户脚本；
3. 扩展 `can_bms_host.py`，新增寄存器块读和 CSV/JSON 输出。

## 5. 方案 C：绕过 CAN，使用 Modbus RTU

BMS 软件默认串口参数为 19200、8N1、从站地址 1。Modbus 路径和 CAN App 最终共用寄存器服务，因此理论上可读取同样的数据。

但在接线前必须从原理图、BOM 或实物确认接口电气层：

- 若是 RS485，使用隔离 USB-RS485，并确认 A/B 命名和终端；
- 若是 3.3 V TTL UART，使用 3.3 V USB-UART，并交叉 TX/RX、共地；
- 不能把 RS232 电平、5 V TTL 或 USB 信号直接接入 MCU UART。

当前 `BMS_CommTool_Upgrade_UI.exe` 的“BMS直连串口”仍会先向 Comm Tool 发送 `SET_BMS_UART`，所以没有 Comm Tool 时不能直接使用该模式。客户可先用标准 Modbus 软件或自编客户端；若希望沿用现有 GUI，需要新增原生串口/RS485 transport。

## 6. 方案 D：客户 ECU 或网关集成

量产场景推荐客户 ECU：

1. 常态被动接收 `0x14F80200/02/03/05`；
2. 启动、人工诊断或低频周期时，通过 READ_BLOCK 读取 `0xD000`；
3. 产品信息只在连接或版本变化时读取 `0xC002`，不要高频轮询；
4. 设置通信超时和陈旧数据标志；
5. 对总线错误、休眠无响应和 BMS 重启做状态机处理；
6. 多 BMS 同网前先解决固定广播 ID 冲突，不能直接并联后假设地址可区分广播。

建议数据策略：

| 数据 | 推荐来源 | 建议频率 |
|---|---|---|
| 总压、电流、SOC | 周期广播 | 随帧接收 |
| MOS/故障摘要 | 周期广播 | 随帧接收 |
| 单体与完整温度 | READ_BLOCK `0xD000` | 1～5 s 或按需 |
| 完整故障字 | READ_BLOCK `0xD000` | 1～5 s 或故障时 |
| 序列号/硬软版本 | READ_BLOCK `0xC002` | 连接时一次 |
| 参数 | 对应寄存器 | 维护时按需 |

## 7. 无 Comm Tool 的 CAN IAP

仓库现有通用工具支持直接通过兼容 python-can 的适配器执行：

```powershell
.\tools\start_can_bms_host.ps1 -Mode app-enter-iap `
  -Interface pcan -Channel PCAN_USBBUS1 -Bitrate 250000 `
  -ConfirmEnterIap

.\tools\start_can_bms_host.ps1 -Mode upgrade-dry-run `
  -Bin '.\103 + 309\Project\Users\Objects\FD_Release.bin'

.\tools\start_can_bms_host.ps1 -Mode upgrade `
  -Interface pcan -Channel PCAN_USBBUS1 -Bitrate 250000 `
  -Bin '.\103 + 309\Project\Users\Objects\FD_Release.bin' `
  -ConfirmAppAddress 0x08004800 -WaitAck
```

执行真实升级前必须满足：

- 已确认 BMS 的 App 地址为 `0x08004800`；
- 镜像不得越过 `0x0801F800`；
- 先做 dry-run；
- 保证供电和 CAN 稳定；
- 具备 ST-Link 或安全烧录脚本等恢复手段；
- 不把 App bin 写到 `0x08000000`。

详细帧级协议见 [BMS CAN 通信协议完整说明](https://github.com/CS19970929/103-309-template/blob/5521e851d77e517b76bec679dd215bbd3e2d4528/docs/protocol/can_protocol.md)。

## 8. 推荐决策

客户只想立即读取电池基本信息时，选用隔离型通用 USB-CAN，250 kbit/s 活动模式监听广播，是成本最低、风险最小的方案。

需要完整单体、版本和诊断信息时，在同一适配器上实现 READ_BLOCK；若希望无开发即可使用完整 GUI，则继续配套 Comm Tool。已明确存在可用 RS485/TTL 接口时，Modbus RTU 也是可行替代，但必须先确认电气层。

## 9. 建议给客户的交付包

- 本文和 [BMS CAN 通信协议完整说明](https://github.com/CS19970929/103-309-template/blob/5521e851d77e517b76bec679dd215bbd3e2d4528/docs/protocol/can_protocol.md)；
- CAN 连接器针脚和线束图；
- DBC 文件（当前仓库未见权威 DBC，建议后续生成并实车验证）；
- python-can 最小读取示例或扩展后的 `can_bms_host.py`；
- 已验证的 USB-CAN 型号、驱动版本和配置截图；
- BMS 固件版本、节点地址、波特率和寄存器映射版本；
- CAN 抓包样例和预期解码结果；
- 若开放升级，再单独提供升级安全说明和恢复流程。

## 10. 现场排障

| 现象 | 优先检查 |
|---|---|
| 完全无帧 | BMS 是否唤醒、收发器使能、250 kbit/s、CAN_H/L、共地 |
| 有错误帧但无有效数据 | 波特率、终端、线序、适配器模式 |
| 只有首次帧或发送不稳定 | 适配器是否 Silent，是否有节点 ACK |
| 有广播但请求无应答 | 标准/扩展帧选错、节点地址、DLC、CRC 字节序 |
| READ_BLOCK 中途停止 | 是否并发发送新命令、接收队列是否丢帧、超时是否过短 |
| 休眠后不响应 | 当前 CAN 不能唤醒 STOP，需使用既有唤醒源 |
| 多台 BMS 数据混杂 | 周期广播 ID 固定，存在同 ID 冲突 |
