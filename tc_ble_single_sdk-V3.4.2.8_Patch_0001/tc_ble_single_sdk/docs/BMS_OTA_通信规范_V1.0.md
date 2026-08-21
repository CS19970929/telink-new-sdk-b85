# BMS OTA 通信规范 V1.0

> 面向 BMSAssistantQt、Android、iOS/macOS、微信小程序以及 AI 开发代理。
>
> 本文描述当前 `vendor/ble_sample` 固件实际启用的 Telink B85 BLE OTA Server，并定义客户端 V1 的统一实现基线。

## 1. 结论

当前 BMS 固件已经启用 Telink BLE OTA Server：

- `BLE_OTA_SERVER_ENABLE = 1`
- OTA 总流程超时：`180 s`
- OTA 相邻数据包超时：`15 s`
- Flash Protection 已启用；固件 OTA 回调负责 OTA 写入期间解锁并在结束后恢复保护

客户端 V1 **统一采用 Telink Legacy OTA + 16-byte PDU**。

MUST：

- 不依赖 MTU > 23。
- 每个 OTA data packet 固定 20 byte。
- `adr_index` 从 0 开始连续递增，禁止跳号、重复。
- 每个 data packet 必须做 Telink CRC16。
- 最后一块不足 16 byte 时必须使用 `0xFF` 补齐后再计算 CRC16。
- 所有固件端必须使用同一 OTA Service/Characteristic UUID。
- OTA 成功必须以 `CMD_OTA_RESULT = 0xFF06` 且 result=`0x00` 为最终判据，而不是仅以“数据发送完”判定。

MUST NOT：

- 不得把普通 BMS SPP Characteristic 当成 OTA 通道。
- 不得把 Modbus CRC 与 OTA CRC 当成两个不同算法；两者当前都是 CRC16/IBM(Modbus) 多项式 `0xA001`、初值 `0xFFFF`，但 OTA CRC 计算范围是 `adr_index + firmware data`。
- 不得并发发送两条 OTA 数据流。
- 不得在 OTA 过程中发送普通 Modbus 业务命令。
- 不得因为本地 GATT Write 成功就宣告 OTA 成功。

---

## 2. GATT

### 2.1 OTA Service

```text
00010203-0405-0607-0809-0A0B0C0D1912
```

### 2.2 OTA Characteristic

```text
00010203-0405-0607-0809-0A0B0C0D2B12
```

当前属性支持：

- Read
- Write Without Response
- Write
- Notify

当前客户端首版建议优先使用 **Write With Response** 做可靠性验证；实机稳定后再按平台切换到 Write Without Response 提升速度。

客户端连接流程：

```text
连接 BMS
  -> discover OTA Service
  -> discover OTA Characteristic
  -> 若 Characteristic 暴露 CCCD，则订阅 Notify
  -> OTA ready
```

OTA Characteristic 与普通 BMS SPP 通道是两套独立 GATT 通道。

---

## 3. OTA 协议选择

Telink SDK 同时支持 Legacy 与 Extend OTA。

BMS Client Platform V1 选择：

```text
Legacy OTA
PDU = 16 byte
ATT payload = 20 byte
```

理由：

1. 当前 BMS 普通通信基线就是 ATT MTU 23。
2. B85 对 Big PDU 有额外限制。
3. V1 首要目标是 Windows/Android/iOS/微信四端稳定一致，而不是最大吞吐率。
4. 当前固件 OTA 总超时已放宽到 180 s，相邻包超时 15 s。

后续 V2 可评估 Extend OTA + MTU/DLE + Big PDU。

---

## 4. 字节序

Telink OTA 多字节字段使用 little-endian。

例如：

```text
CMD_OTA_START = 0xFF01 -> 01 FF
CMD_OTA_END   = 0xFF02 -> 02 FF
CMD_OTA_RESULT= 0xFF06 -> 06 FF
index 0x14FA           -> FA 14
```

---

## 5. CRC16

算法：

```text
init = 0xFFFF
poly = 0xA001
LSB first
result little-endian on wire
```

伪代码：

```text
crc = 0xFFFF
for byte in data:
    crc ^= byte
    repeat 8:
        if crc & 1:
            crc = (crc >> 1) ^ 0xA001
        else:
            crc >>= 1
```

OTA data CRC 计算范围：

```text
adr_index[2] + firmware_data[16]
```

共 18 byte。

---

## 6. Firmware BIN 校验

Telink firmware header 至少需要读取：

### 6.1 Firmware mark

当前 Telink B85 镜像在偏移 `0x08` 处应出现：

```text
4B 4E 4C 54
```

ASCII：

```text
KNLT
```

客户端 SHOULD 在选择固件时检查该标记，避免把任意 bin 写入 BMS。

### 6.2 Firmware size

偏移：

```text
0x18 ~ 0x1B
```

字段：

```text
uint32 little-endian firmware_size
```

客户端 MUST：

- `firmware_size > 0`
- `firmware_size <= file_size`
- OTA 只发送 `bin[0 : firmware_size]`

---

## 7. CMD_OTA_START

Opcode：

```text
0xFF01
```

Wire：

```text
01 FF
```

客户端发送 Start 后才能发送 OTA data。

---

## 8. OTA data packet

固定格式：

| Offset | Size | Field |
|---|---:|---|
| 0 | 2 | `adr_index`, little-endian |
| 2 | 16 | firmware data |
| 18 | 2 | CRC16 of bytes 0..17, little-endian |

总长度：

```text
20 byte
```

地址关系：

```text
index = 0 -> firmware[0x0000 : 0x0010]
index = 1 -> firmware[0x0010 : 0x0020]
...
```

最后一包不足 16 byte：

```text
有效数据 + FF FF ... -> 补满 16 byte -> 计算 CRC
```

### 8.1 固定回归向量

输入：

```text
index = 0
firmware data = 26 80 00 00 00 00 5D 02 4B 4E 4C 54 30 04 88 00
```

输出必须为：

```text
00 00 26 80 00 00 00 00 5D 02 4B 4E 4C 54 30 04 88 00 1C A3
```

所有平台 OTA codec MUST 通过该向量。

---

## 9. CMD_OTA_END

格式：

| Offset | Size | Field |
|---|---:|---|
| 0 | 2 | `0xFF02` |
| 2 | 2 | `max_index` little-endian |
| 4 | 2 | `max_index ^ 0xFFFF` little-endian |

OTA END 不附加 CRC16。

固定向量：

```text
max_index = 0x14FA
```

输出：

```text
02 FF FA 14 05 EB
```

---

## 10. CMD_OTA_RESULT

设备完成或终止 OTA 时通过 OTA Characteristic 返回：

```text
06 FF <result> ...
```

result：

| Value | Meaning |
|---:|---|
| `0x00` | OTA_SUCCESS |
| `0x01` | OTA_DATA_PACKET_SEQ_ERR |
| `0x02` | OTA_PACKET_INVALID |
| `0x03` | OTA_DATA_CRC_ERR |
| `0x04` | OTA_WRITE_FLASH_ERR |
| `0x05` | OTA_DATA_INCOMPLETE |
| `0x06` | OTA_FLOW_ERR |
| `0x07` | OTA_FW_CHECK_ERR |
| `0x08` | OTA_VERSION_COMPARE_ERR |
| `0x09` | OTA_PDU_LEN_ERR |
| `0x0A` | OTA_FIRMWARE_MARK_ERR |
| `0x0B` | OTA_FW_SIZE_ERR |
| `0x0C` | OTA_DATA_PACKET_TIMEOUT |
| `0x0D` | OTA_TIMEOUT |
| `0x0E` | OTA_FAIL_DUE_TO_CONNECTION_TERMINATE |
| `0x0F` | OTA_MCU_NOT_SUPPORTED |
| `0x10` | OTA_LOGIC_ERR |

客户端必须保存原始 result code 到升级日志。

---

## 11. 状态机

统一状态：

```text
idle
 -> firmware_validated
 -> connecting
 -> ota_ready
 -> starting
 -> transferring
 -> ending
 -> waiting_result
 -> success
 -> waiting_reboot
 -> reconnecting
 -> version_verified
```

失败路径：

```text
任何状态
 -> failed(error_code/message)
```

V1 必须保证一次只运行一个 OTA session。

---

## 12. 首版发送策略

为了优先验证正确性：

```text
write START
wait local GATT write completion

for packet in OTA data:
    write packet with response
    wait local GATT write completion

write END
wait local GATT write completion
wait CMD_OTA_RESULT
```

说明：GATT Write Response 只表示该 ATT 写操作完成，不代表 firmware 已最终校验通过。

最终成功判据只能是：

```text
CMD_OTA_RESULT.result == 0x00
```

之后 BMS 应重启并运行新固件。

---

## 13. 客户端 UI 最低要求

OTA 页面至少显示：

- 当前连接设备
- 当前软件版本
- 固件文件名
- bin 文件大小
- header 声明 firmware size
- packet count
- 进度 0~100%
- 当前阶段
- OTA result
- 原始升级日志

危险操作前必须二次确认：

```text
升级期间禁止断电。
确认当前连接的是目标 BMS。
```

---

## 14. 安全边界

当前固件：

```text
BLE_APP_SECURITY_ENABLE = 0
```

因此当前 OTA Service 没有依赖 BLE SMP 做强制授权。

V1 OTA 仅作为工程调试能力使用。

正式客户版本在开放 OTA 前必须至少完成以下之一：

- BLE SMP pairing/bonding；或
- 应用层 OTA unlock/authentication；或
- 固件签名/secure boot，并限制 OTA 入口。

不能因为 OTA 协议能正常工作就直接视为量产安全方案。

---

## 15. 各端共享要求

以下实现必须共用本文规则与固定测试向量：

```text
BMSAssistantQt/bmsassistantqt/ota.py
BMSAssistantAndroid/.../ota/TelinkOtaCodec.kt
BMSAssistant/.../Protocol/TelinkOTA.swift
BMSAssistantMiniProgram/core/ota.ts
```

以后修改 OTA wire protocol 时，先更新本文档和测试向量，再修改客户端。
