# D008 架构与数据所有权

| 路径 | 源码所有者 |
|---|---|
| 启动、200 ms 采样、业务顺序 | `bms/app/app.c` |
| 普通 Suspend、保护性关机、ACC | `bms/app/app_power.c` |
| CRC I2C、单体、CC2、NTC 与寄存器编码 | `bms/afe/dvc1124/` |
| 双路 MOS 最终许可和通信恢复 | `bms/core/bms_afe_guard.c` |
| 软件保护、加热、均衡 | `bms_sw_protection.c`、`bms_features.c` |
| SOC 数值和业务接线 | `bms/core/bms_soc.c`、`bms/app/bms_soc_app.c` |
| 参数、检查点及历史 | `bms_config_store.c`、`bms_state_store.c`、`bms_event_log.c` |
| 协议、UART/BLE、SIF | `modbus_rtu.c` 和 `bms/platform/telink` |

Config 在候选通过校验与持久提交后发布。DVC 硬件保护独立于软件保护，提交顺序见 [Flash 存储](STORAGE.md)。有效 CC2 电流允许远程单路关断时临时反向放行。双路明确禁用和共同保护优先。保护锁存按物理证据恢复，普通通信失败在持续 1 秒后确认。

## 6. 通信与采样所有权

BLE GATT 和 UART 使用相同的 Modbus 解析器。UART 115200 8N1，PC2/PC3 与一线通共享，接管时丢弃首次 DMA 残帧，空闲约 10 秒后回到 SIF。SIF 主循环组包，500 µs TIMER0 ISR 只发送已交付缓冲。支持锂神 V1.1.1 和 A1.3.3 两个编译协议路径。

有效配置串供极值、保护和计量使用。Modbus 未装单体为 61001，A1.3.3 对应槽编码 0xFFFF。公开温度为 `(°C+40)*10`，电流正放电、负充电。报文容量 0.01 Ah，额定容量参数 0.1 Ah。保护、历史、实时及 AFE 窗保留原业务合同，SOC 扩展见 [SOC 与两个客户参数](SOC.md)。
