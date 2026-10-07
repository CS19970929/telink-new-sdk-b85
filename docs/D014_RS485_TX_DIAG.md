# D014 RS485 发送链路诊断

当前入口：`bms/products/d014/bms_product.h` 的宏及 `bms/platform/telink/modbus_uart.c/.h`。日常验证用 D014 `link/resources`；台架需要镜像时另按 [构建指南](BUILD_AND_TEST.md) 明确生成。本页末尾旧 MAP 地址仅为历史观察，调试时使用当前 ELF。

## 用途与边界

`BMS_RS485_TX_DIAG_ENABLE` 默认是 `0`。临时设为 `1` 后，固件每约 500 ms 通过正式的 `s_tx_pkt`、B85 `uart_send_dma()`、PC2 UART TX 和 PA1 方向控制发送一帧。此时 RX DMA 和 Modbus 请求处理关闭，上位机发来的读命令不会获得回复。AFE、SOC 和保护任务仍运行，但测试帧不读取它们的数据。测试完成后恢复宏为 `0` 并重新编译。

诊断模式每 21 帧循环一次，依次是以下四组，每组长度为 8、32、64、81、128 字节：

| 组 | 字节内容 |
|---|---|
| 0 | 全 `55` |
| 1 | 全 `AA` |
| 2 | 从 `00` 开始递增，按字节回绕 |
| 3 | `EE 49` 重复 |

最后一帧固定为 81 字节：`01 03 4C` + `00..4B`（76 字节）+ `C9 B8`。`C9 B8` 是前 79 字节的 Modbus CRC16，低字节在前。串口工具应以 **115200、8N1、HEX 接收**记录完整字节流；普通重复图案帧没有 CRC，按帧间约 500 ms 的间隔和预期长度切分。固定帧可直接做 CRC 校验。不要把诊断模式的固定 81 字节帧当成真实 `0xD000` 寄存器响应。

## 调试计数与发送状态

调试器可直接观察全局变量 `g_bms_rs485_tx_diag`：

| 字段 | 含义 |
|---|---|
| `tx_start_count` | DMA 启动次数 |
| `tx_dma_done_count` | UART TX DMA IRQ 次数（仅计当前帧第一次） |
| `tx_uart_done_count` | DMA IRQ 后首次观察到 B85 `TX_DONE` 的次数 |
| `tx_complete_count` | `TX_DONE` 且完整帧时间到达后，PA1 切回 RX 的次数 |
| `tx_timeout_count` | 活动帧超过 50 ms 的中止次数；主循环停止 DMA、重置 UART 并释放 DE，不计成功完成 |
| `tx_busy_reject_count` | 上一帧仍活动或 UART 繁忙时被拒绝的新发送次数 |
| `last_tx_len` | 最近一次启动的 DMA 数据长度 |
| `de_state` | 最近一次软件写入 PA1 的方向，`1` 为发送 |
| `pattern_index` | 下一帧编号，范围 `0..20`；`20` 为固定 CRC 帧 |

TX IRQ 只交付完成标志；UART 状态、最小 hold、超时中止和 DE 切换都由主循环处理。超时恢复保持原 UART 分频/引脚及诊断模式 RX 策略，重新发送必须作为新帧申请。主循环若被长期阻塞，50 ms 是恢复判定阈值，不是硬实时响应上限。

计数是诊断线索，不是物理线上的字节计数。正常模式也记录同一组计数；当前没有新增 Modbus 寄存器或修改协议。长时间暂停调试器可能令 `tx_timeout_count` 增加。正常完成时四个 `tx_*_count` 应同步增加，`de_state` 应在帧间为 `0`。

## 实板定位顺序

1. 记录板号、BOM、固件 commit、宏值、电源和串口工具设置。接收端只监听，先不要轮询。每种长度至少收几十轮，并记录完整原始 HEX、时间戳和 CRC 错误次数。
2. 同时用逻辑分析仪测 PC2、PA1、PD4；用隔离测量方式测 CA-IS2092A 的 A/B 差分输出及 C-3V3/ISO 侧供电。不要把非隔离仪器地直接跨接隔离两侧。
3. 先比较 PC2 字节与测试帧，再比较 A/B 解码结果与 PC2。检查 PA1 从首字节前保持高电平至最后停止位后，帧间回到低电平。

| 观测 | 优先检查 |
|---|---|
| PC2 已有同样的错字节或缺字节 | UART DMA 启动/长度/地址、TX DMA IRQ、`TX_DONE`、BLE/PM 时序及时钟；结合计数和波形复核 |
| PC2 完整、A/B 错误 | CA-IS2092A、PA1 时序、PD4、C-3V3/ISO 侧供电、终端/偏置/线缆和总线冲突 |
| PC2 和 A/B 都完整、上位机错 | USB-RS485 适配器、接收配置、接地/共模和采集软件分帧 |
| 仅长帧异常 | 特别比较 32/64/81/128 字节的 DMA 数据、供电压降和末尾 DE 时序 |
| `tx_start_count` 大于 `tx_dma_done_count` | TX DMA IRQ 未到或被漏处理；检查 DMA 状态/屏蔽寄存器 |
| DMA 完成但 `tx_uart_done_count` 不增长 | UART 移位器未报告完成，不能据 DMA IRQ 提前切回 RX |
| `tx_busy_reject_count` 增长 | 有重叠发送请求；正式模式应检查请求频率和主站帧间隔 |

源码审核确认原 `modbus_uart_send()` 在复制 `s_tx_pkt` 和再次调用 `uart_send_dma()` 前没有繁忙检查；已加保护，拒绝重叠发送并计数。B85 驱动中的 `uart_send_dma()` 本身不检查上一帧状态。`uart_recbuff_init()`只写 RX DMA0 寄存器，发送用 TX DMA1；源码未发现 RX 重新装载直接改写 TX DMA1。现有固定 `bus_mux` 状态使普通 BLE Suspend 在稳态通常已被关闭，但现在 PM 路径还显式检查 `uart_tx_is_busy()` 与 RS485 TX active。以上均是源码结论；需要波形确认 PC2、DE、A/B 和隔离侧电源的实际行为。

DMA 包使用 4 字节对齐的 `u32` 长度字段加 payload，容量是 268 字节；旧 MAP 中 `s_tx_pkt` 位于 `0x8451BC`，没有跨越 64 KiB 边界。B85 SDK 的 `uart_send_dma()` 写 DMA1 低地址和长度，却没有在该函数中写 `reg_dma1_addrHi`；本次没有芯片复位值或板上寄存器读回证据，不据此猜测性修改驱动。若 PC2 已出错，需在 DMA 启动时读回 DMA1 地址/长度寄存器，并对照 `s_tx_pkt` 实际链接地址和内存内容。
