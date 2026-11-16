# BMS CAN-IAP 升级协议

## CAN ID

使用扩展帧，避开现有 BMS 周期广播 `0x14F802xx`。

| 方向 | 扩展帧 ID | 说明 |
| --- | --- | --- |
| comm tool -> BMS IAP | `0x14F8F000 | node` | 控制帧 |
| BMS IAP -> comm tool | `0x14F8F100 | node` | ACK/NACK |
| comm tool -> BMS IAP | `0x14000000 | (seq << 8) | node` | 数据帧 |

默认 `node=1`，CAN 波特率第一阶段为 `250 kbit/s`。

多设备总线中，IAP 节点必须在同一时刻唯一。当前流程推荐先用 BMS App CAN 地址选中目标板并发送 `ENTER_IAP`，使只有目标板进入 IAP；其它同总线 BMS 保持 App 运行时不会响应 CAN-IAP 扩展帧。如果多个 BMS 已经同时停留在 IAP 且节点相同，ACK/NACK ID 完全一致，comm tool 无法判断是哪一块板响应，禁止直接升级。

## 控制命令

| 命令 | payload | 说明 |
| --- | --- | --- |
| `0x01 HELLO` | `01 version node FF FF FF FF FF` | 握手 |
| `0x02 START` | `02 version size:u32 crc16:u16` | 开始升级 |
| `0x03 COMMIT` | `03 block_seq:u16 block_len:u16 block_crc:u16 FF` | 提交一个块 |
| `0x04 END` | `04 frame_count:u16 crc16:u16 FF FF FF` | 结束升级 |
| `0x05 ABORT` | `05 reason FF FF FF FF FF FF` | 终止升级 |
| `0x79 ACK` | `79 cmd state expect_seq:u16 code FF FF` | 成功响应，`code=0` 表示命令执行成功 |
| `0x1F NACK` | `1F cmd code expect_seq:u16 code FF FF` | 失败响应，`code` 为错误原因 |

ACK 中的第 3 字节是 IAP 当前状态，不是错误码。典型状态值为：`0=IDLE`、`1=RECEIVING`、`2=DONE`、`3=ERROR`。上位机或 comm tool 判断命令成功必须检查 ACK 命令字匹配且 `code=0`，不能要求 `state=0`。

## 数据与提交

- 每个数据帧 8 字节。
- `seq` 从 0 开始递增。
- 推荐 32 个数据帧组成一个 256B 块。
- BMS IAP 只有收到 `COMMIT` 且块 CRC 正确后，才擦写或写入 Flash。
- 每个块写入后必须回读校验。
- 整包 `END` 后再做总 CRC 和 App 向量校验。

## App 有效性检查

写入完成后必须检查：

- 初始 MSP 在 SRAM 范围内。
- ResetHandler 在 App Flash 范围内。
- 固件长度不越过 App 区上限和参数区。
- 整包 CRC16 与 `START/END` 一致。

## 断电恢复

- `START` 后立即清除 App 有效标志。
- 任意升级中断后，下次启动停留在 IAP。
- 未通过 `END` 总校验时禁止跳 App。
- 升级成功后再写 App 有效标志并跳转。
