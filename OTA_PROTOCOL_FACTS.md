# OTA Protocol Facts (B85m / TLSR825x BLE Single Connection SDK V3.4.2.8)

> 本文档是 BLE OTA App 实现的协议事实依据。所有结论均来自：
> 1. 当前 SDK 源码（`stack/ble/service/ota/ota.h`、`ota_server.h`、`vendor/ble_sample/*`、`boot/B85/cstartup_825x.S`、`boot.link`）；
> 2. 实际 Firmware BIN（`c11 d002 13s 10.4Ah ...bin`，91076 Byte）逐字节分析；
> 3. 官方 Telink OTA Android App v2.1.2（`OTA_Telink_2.1.2`，com.telink.lt.ota）DEX 反汇编；
> 4. SDK 后处理工具 `tl_check_fw` 实跑验证（`bin_append_crc/bin_append.exe` 属于 2.4G UART 工程，其 CRC16=0x8408(CCITT) 与 BLE GATT OTA 无关，BLE OTA 数据包 CRC16 以 ota.h 与官方 App 为准为 Modbus 0xA001）。
>
> 结论与任务书中的手册描述冲突时，以本文档为准（本文档依据源码与官方 App 行为）。

---

## 1. 设备端配置（vendor/ble_sample，本次目标工程）

| 项 | 值 | 依据 |
|---|---|---|
| 芯片 | TLSR8258（`__PROJECT_8258_BLE_SAMPLE__`，MCU_CORE_825x） | subdir.mk 编译参数 |
| Flash 容量 | 512 KB（`FLASH_ADDR_LAYOUT_512K_*`） | flash_store_cfg.h |
| `BLE_OTA_SERVER_ENABLE` | 1 | app_config.h:32 |
| OTA 启动区 | **默认 `MULTI_BOOT_ADDR_0x20000`**（未调用 `blc_ota_setFirmwareSizeAndBootAddress`） | ota_server.h:62 默认说明；app.c 无调用 |
| 单个 Firmware 最大尺寸 | **124 KB**（0x20000 区，最后 4K 保留） | ota_server.h:62 |
| 设备端 OTA 超时 | process timeout = **180 s**，packet timeout = **15 s** | app_config.h:62-63 |
| 设备端版本号 | 未调用 `blc_ota_setFirmwareVersionNumber` → 设备本地版本为默认值，**不建议依赖 Extend 版本比较** | app.c 无调用 |
| 连接参数 | 连接后固件请求 interval 10ms / latency 99 / timeout 4s | docs（对接文档 5.3） |
| 安全 | `BLE_APP_SECURITY_ENABLE = 0`（无配对/加密要求） | app_config.h:31 |
| Flash 保护 | `APP_FLASH_PROTECTION_ENABLE = 1`，OTA 写新区前解锁、成功后重锁 | app.c app_flash_protection_operation |
| 低压保护 | `APP_BATT_CHECK_ENABLE = 0`（本工程未启用，OTA 前 App 应提示用户确保供电） | app_config.h:46 |

## 2. GATT 服务与 UUID

| 项 | UUID | 依据 |
|---|---|---|
| OTA Service | `00010203-0405-0607-0809-0A0B0C0D1912` | uuid.h `TELINK_OTA_UUID_SERVICE`；官方 App `UuidInfo.OTA_SERVICE_UUID` |
| OTA Characteristic | `00010203-0405-0607-0809-0A0B0C0D2B12` | uuid.h `TELINK_SPP_DATA_OTA`；官方 App `UuidInfo.OTA_CHARACTERISTIC_UUID` |
| 属性 | Read | Write | WriteWithoutResponse | Notify | CCCD（同一个 Characteristic） | app_att.c:368-372 |

- OTA 写入和 Notify 使用**同一个 Characteristic**（`blc_ota_setAttHandleOffset` 未调用，默认 0）。
- **App 必须通过 GATT 发现 UUID，禁止硬编码 Handle**（当前固件 Handle 0x002E~0x0032 仅作参考）。

## 3. Firmware BIN 文件事实（实测 "c11 d002 ... 20260615" bin）

| 偏移 | 长度 | 内容 | 值（实测） |
|---|---|---|---|
| 0x00 | 4 | reset 向量（thumb 跳转） | `56 80 00 00` |
| 0x08 | 4 | **Firmware Mark / 启动标志：`0x544C4E4B`（ASCII "TLNK"，LE 存储 `4B 4E 4C 54`）** | `4B 4E 4C 54` |
| 0x0C | 4 | retention 信息 `0x00880000 + use_size/16` | `A5 03 88 00` |
| 0x10 | 4 | IRQ 向量 | `B6 80 00 00` |
| **0x18** | **4** | **Firmware Size，小端 u32。本 SDK 后处理格式：= 文件总长（含尾部 4 字节 CRC32）**（tl_check_fw2.exe 追加 CRC 后回写；实测 0x163C4 = 91076 = 文件长度） | `C4 63 01 00` = 0x163C4 = 91076 |
| 尾部 | 21+ | SDK 版本串 `$$$tc_ble_single_sdk_V3.4.2.8$$$` | 实测 bin 尾部 |
| 尾部 | 4 | **Telink CRC32（官方后处理追加，LE 存储）** | 本工程实测 BIN 带有效 CRC，见下方向量 |

结论：
- **Firmware Mark = 偏移 0x08 的 4 字节 `0x544C4E4B`**。设备收到非此 Mark 返回 `OTA_FIRMWARE_MARK_ERR (0x0A)`。
  这也与 2p4g_feature/ota/ota.c:724 一致（`bin_buf[8..11] = 4B 4E 4C 54`）。
- **Firmware Size = 偏移 0x18 的小端 u32 = 文件总长（含尾部 CRC32）**。依据：`tl_check_fw2.exe`（本 SDK 后处理工具）实测——对无尾 CRC 的 BIN 执行后，追加 4 字节 CRC32 并把 Size@0x18 回写为总长（91072 → 91076）。设备按 Size@0x18 校验"应接收的总字节数"，最后 4 字节作为 CRC32 校验区。
- **CRC32 算法（官方 App `Crc.calCrc32` + `OtaController.checkCRC` 反汇编）**：
  - 初始值 `0xFFFFFFFF`，查表法（reflected，多项式 `0xEDB88320`），**无最终异或**；
  - 附加位置：文件尾 4 字节，按小端存储（App 用大端读回比较）；
  - 覆盖范围：文件除去最后 4 字节的全部内容。
  - 测试向量：`calCrc32("123456789") = 0x340BC6D9`（init 0xFFFFFFFF，无 final xor）。
  - **实机向量（本工程固件 BIN，91076 B）**：前 91072 字节的 CRC32 = `0x814C0E73`，与 BIN 尾部 LE 存储值完全一致 → 该 BIN 已自带合法尾部 CRC32。
- 本工程后处理跑 `tl_check_fw`（`tl_check_fw2.exe`），该工具**追加 4 字节 CRC32 并回写 Size@0x18 = 总长**。
  OTA App 的策略：文件尾部 4 字节是合法 Telink CRC32（覆盖前 len-4 字节）→ **原样发送**；否则按"原始构建产物"处理——**自动追加 CRC32 并回写 Size@0x18 = len+4**（与工具语义一致），并在日志中提示。
- **BLE OTA 数据包 CRC16 = CRC-16/MODBUS（poly 0xA001）**，依据：`ota.h:crc16()`（poly 表 `{0, 0xa001}`）+ 官方 App `OtaPacketParser.crc16` fill-array 数据 `{0x0000, 0xA001}`（DEX 实测）+ 手册第 17.1 节。`bin_append.exe`（0x8408/CCITT）是 2.4G UART 工程的工具，与本协议无关。

## 4. 命令与数据包格式（官方 App + ota.h 双源确认）

所有多字节字段均为**小端**。所有命令通过 Write Without Response 写入 OTA Characteristic。

| Opcode | 名称 | 方向 | 载荷格式 | 总长 |
|---|---|---:|---|---|
| `0xFF00` | CMD_OTA_VERSION | C→S（Legacy 可选） | 无 | 2 |
| `0xFF01` | CMD_OTA_START | C→S（Legacy） | 无 | 2 |
| `0xFF02` | CMD_OTA_END | C→S（全部） | `adr_index_max(2)` + `adr_index_max_xor(2)` | 6 |
| `0xFF03` | CMD_OTA_START_EXT | C→S（Extend） | `pdu_length(1)` + `version_compare(1)` + rsvd(16) | 20 |
| `0xFF04` | CMD_OTA_FW_VERSION_REQ | C→S（Extend） | `version(2)` + `version_compare(1)` | 5 |
| `0xFF05` | CMD_OTA_FW_VERSION_RSP | S→C（Extend Notify） | `version(2)` + `accept(1)` | 5 |
| `0xFF06` | CMD_OTA_RESULT | S→C（全部 Notify） | `result(1)` | 3 |
| `0xFF80` | CMD_OTA_SET_FW_INDEX | C→S（可选） | `fw_index(1)` | 3 |

- `adr_index_max_xor = adr_index_max ^ 0xFFFF`（官方 App sendOtaEndCommand 用 `index ^ 0xFFFFFFFF` 取低 16 位，等价）。
- END 命令中的 index = **最后一个已发送数据包的 Adr_Index**（不是包数）。
- 版本比较：`version_compare=1` 时设备端拒绝不高于当前版本的升级（`OTA_VERSION_COMPARE_ERR`）。本工程设备端未配置版本号，**默认 `version_compare=0`（不做比较）**。

### 4.1 数据包（OTA Data PDU）

```
[Adr_Index(2, LE)] [Data: n * PDU_Length] [CRC16(2, LE)]
总长 = 2 + PDU_Length + 2
Firmware offset = Adr_Index * PDU_Length
```

- **首个 Adr_Index = 0**（官方 App `OtaPacketParser`：index 初始 -1，`getNextPacketIndex()=index+1` → 首包 0）。
- 中间包：数据长度 = PDU_Length，包长 = PDU_Length + 4。
- 尾包：剩余数据不足 PDU_Length 时，**以 `0xFF` 补齐到 16 的整数倍**（官方 App 先补到 16 的倍数，再拼 Index+CRC），CRC16 覆盖 Index+数据+补齐位。
  - 裁定：任务书手册建议"补齐到完整 PDU Length"与官方 App"补齐到 16 倍数"存在冲突。设备端对 `fw_size` 之后的字节不参与 CRC32 校验，补齐位不影响正确性；**以官方 App 行为（16 的倍数）为默认实现**，并在 App 设置中提供"补齐到完整 PDU"选项供排障。
- **CRC16 = CRC-16/MODBUS**：poly `0xA001`（reflected `0x8005`）、init `0xFFFF`、无最终异或、wire 上低字节在前。
  - 测试向量：`crc16("123456789") = 0x4B37`。
  - CRC16 覆盖范围为**整包去掉最后 2 字节 CRC**（官方 App `crc16(buf)` 对 `len-2` 字节计算，即含 Adr_Index）。
- **PDU_Length 限制**：16~240，必须为 16 的整数倍；且必须满足 `PDU_Length + 7 <= MTU`（官方 App：`min(配置, MTU-7)`）。

### 4.2 版本流程（Extend）

1. C→S：`CMD_OTA_FW_VERSION_REQ`（version 取 BIN 偏移 0x02 的 2 字节小端；compare 默认 0）。
2. S→C Notify：`CMD_OTA_FW_VERSION_RSP`：`accept==1` 继续，否则终止（3 s 内未收到视为超时）。
3. C→S：`CMD_OTA_START_EXT`。

### 4.3 OTA Result 码（设备 Notify，`data[2]`，只报一次）

| 值 | 名称 | 用户提示建议 |
|---|---|---|
| 0x00 | OTA_SUCCESS | 成功 |
| 0x01 | OTA_DATA_PACKET_SEQ_ERR | 连接不稳定/丢包，重连后从头升级 |
| 0x02 | OTA_PACKET_INVALID | 非法命令/Index/长度 |
| 0x03 | OTA_DATA_CRC_ERR | 检查 CRC16 实现、补齐、PDU Length |
| 0x04 | OTA_WRITE_FLASH_ERR | 电压/Flash 解锁/MID/目标地址异常 |
| 0x05 | OTA_DATA_INCOMPLETE | 尾包丢失 |
| 0x06 | OTA_FLOW_ERR | 流程状态错误（命令顺序） |
| 0x07 | OTA_FW_CHECK_ERR | BIN 被修改/尾部 CRC32 缺失或错误 |
| 0x08 | OTA_VERSION_COMPARE_ERR | 目标版本不高于当前 |
| 0x09 | OTA_PDU_LEN_ERR | Start 声明与实际不一致/非 16 倍数 |
| 0x0A | OTA_FIRMWARE_MARK_ERR | 非 Telink SDK 产物/选错文件 |
| 0x0B | OTA_FW_SIZE_ERR | 尺寸非法（无/过小/超过 124K） |
| 0x0C | OTA_DATA_PACKET_TIMEOUT | 相邻包间隔超 15 s |
| 0x0D | OTA_TIMEOUT | 总流程超 180 s |
| 0x0E | OTA_FAIL_DUE_TO_CONNECTION_TERMINATE | 连接终止 |

## 5. 启动标志与 0x20008 / 0x20020 冲突裁定

- **启动标志地址 = 各 Firmware 区偏移 0x08，值 `0x544C4E4B`**（"TLNK"）。
  - 本工程运行区 `0x00000` → 标志 `0x00008`；OTA 目标区 `0x20000` → 标志 **`0x20008`**。
  - 依据：实测 BIN 偏移 0x08 为 `4B 4E 4C 54`；2p4g ota.c:753-763 写入 `FlashAddr+8=0x544C4E4B`、清除 `0x20008`。
- 手册中 `0x20020` 的说法与当前 SDK 实现不符，**以 `0x20008` 为准**（App 不需要操作该地址，仅用于文档与诊断）。
- 该机制由设备端 lib 完成，App 无需读写 Flash。

## 6. 官方 App 的完整 OTA 流程（作为状态机基准）

```
startOta:
  1. 使能 OTA Characteristic Notify（CCCD=0x0100）
  2. （可选）CMD_OTA_SET_FW_INDEX
  3. （可选）CMD_OTA_VERSION（Legacy 版本请求）
  4. Legacy → CMD_OTA_START；Extend → CMD_OTA_FW_VERSION_REQ（等 3 s 版本 RSP）
  5. CMD_OTA_START_EXT 成功 → 开始发数据包
  6. 逐包 WRITE_NO_RESPONSE；尾包发完后 → CMD_OTA_END
  7. Legacy：END 写完即成功；Extend：等待 CMD_OTA_RESULT Notify
  8. Result==0x00 且 Legacy → 成功；Result!=0 → 失败并映射错误码
  超时：总流程 300 s（App 端，可配置）；单命令超时另有处理
```

官方 App 无"重启后重连+版本复核"（那是本任务书要求 App 端增强的部分）。

## 7. 与任务书的差异裁定（待确认项已从源码闭环）

| 任务书疑问 | 裁定 |
|---|---|
| 首包 Index 0 还是 1 | **0**（官方 App + 文档示例一致） |
| Big PDU 支持 | 16~240、16 的整数倍；实际受 `MTU-7` 限制 |
| Firmware Size 语义 | 偏移 0x18 小端，官方后处理后等于文件总长，包含尾部 4 字节 CRC32 |
| Firmware Mark | 偏移 0x08 的 `0x544C4E4B` |
| CRC32 | init 0xFFFFFFFF、reflected 0xEDB88320、无 final xor、尾部 4 字节 LE |
| CRC16 | CRC-16/MODBUS（0xA001），覆盖 Index+Data+补齐，wire 低字节在前 |
| 启动标志 | 区偏移 0x08（0x20008），`0x544C4E4B`，与 0x20020 冲突时以 0x20008 为准 |
| 设备端超时 | process 180 s / packet 15 s（本工程配置） |
| 版本比较 | 本工程设备端未配置版本号，App 默认 version_compare=0 |
| 断点续传 | 协议未定义；断连后必须从头重试 |

## 8. 跨平台测试向量（协议核心单元测试用）

| 向量 | 期望值 |
|---|---|
| `crc16("123456789")` | `0x4B37` |
| `calCrc32("123456789")`（init FFFFFFFF，无 final xor） | `0x340BC6D9` |
| 真实 BIN 前 91072 B 的 `calCrc32` | `0x814C0E73`（= BIN 尾部 LE 存储值） |
| 包 Index 编码 | Index=1 → `01 00` |
| END xor | max=0x0005 → xor=0xFFFA |
| 尾包补齐 | 剩余 17 Byte、PDU=16 → 补 15 个 0xFF |
| FW Size 解析 | `C4 63 01 00` → 0x163C4 |
