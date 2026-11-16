# comm tool 当前功能、运行逻辑、协议与升级方案

本文以当前源码为准，梳理 `firmware/comm_tool_f103ret6` 的真实能力、协议边界和后续上位机扩展方案。旧文档和历史需求只作为背景，维护时优先核对本文列出的源码入口。

## 1. 结论

当前 comm tool 已具备以下能力：

1. PC 通过私有串口协议连接 comm tool，读取设备信息、设置 CAN 目标、读写 BMS 寄存器、读写固件缓存、触发 BMS CAN-IAP、查询升级状态和 CAN 诊断。
2. PC 也可以发送原始 Modbus RTU 到同一个 PC 串口，由 comm tool 通过 USART2 透传给 BMS，支持 BMS 直连读写和 BMS 串口 IAP 经过 comm tool 转发。
3. comm tool 可把缓存中的 BMS App 固件通过 CAN-IAP 写入 BMS。
4. comm tool 可通过 PA6 离线按键，从已有 BMS App 缓存启动 CAN-IAP，脱离上位机批量升级 BMS。
5. comm tool 自身 IAP 固件支持旧 BMS 串口 IAP 协议，也支持 CAN-IAP 协议；当前 GUI 尚未提供“升级 comm tool”专用入口。

需要注意的限制：

- 当前 GUI 的“写入缓存/一键升级”固定按 BMS App 地址 `0x08004800` 处理，不等价于升级 comm tool。
- 当前 comm tool App 主循环没有调用 `CtSelfIap_FeedUartByte()`；运行态 PC 原始 Modbus `0xFFFD` 会优先走 BMS 透传，不会触发当前 comm tool 自身进入 IAP。
- 通过 CAN 升级 comm tool 自身时，需要“另一台 comm tool”作为升级器；当前设备不能依赖自己发送 CAN-IAP 来升级自己。

## 2. 关键源码索引

| 功能 | 源码 |
| --- | --- |
| App 主循环 | `firmware/comm_tool_f103ret6/source/main.c` |
| App 命令分发、PA6 离线升级 | `firmware/comm_tool_f103ret6/source/app/ct_app.c` |
| PC 私有串口帧协议 | `firmware/comm_tool_f103ret6/source/app/ct_protocol.c/.h` |
| 原始 Modbus RTU 透传 | `firmware/comm_tool_f103ret6/source/app/ct_modbus_bridge.c/.h` |
| BMS App CAN 服务与 CAN-IAP 封装 | `firmware/comm_tool_f103ret6/source/app/ct_can_gateway.c/.h` |
| 缓存到 CAN-IAP 的升级状态机 | `firmware/comm_tool_f103ret6/source/app/ct_upgrade_manager.c/.h` |
| Flash 固件缓存 | `firmware/comm_tool_f103ret6/source/app/ct_flash_store.c/.h` |
| comm tool App 进自身 IAP 的 mailbox | `firmware/comm_tool_f103ret6/source/app/ct_boot_control.c/.h` |
| comm tool 自身 IAP | `firmware/comm_tool_f103ret6/source/iap/ct_iap.c/.h` |
| BSP UART/CAN/按键/LED | `firmware/comm_tool_f103ret6/source/bsp/` |
| PC CLI | `tools/comm_tool_host.py` |
| 图形上位机 | `tools/comm_tool_upgrade_ui.py` |

## 3. 硬件、地址与基础配置

| 项目 | 当前值 |
| --- | --- |
| MCU | `STM32F103RET6` |
| PC 私有协议 UART | 默认 USART1 重映射，`PB6=TX`、`PB7=RX` |
| PC UART 波特率 | `115200 8N1` |
| BMS 透传 UART | USART2，`PA2=TX`、`PA3=RX` |
| BMS 透传 UART 波特率 | `19200 8N1` |
| CAN | CAN1，`PA11=RX`、`PA12=TX` |
| CAN 默认波特率 | `250 kbit/s`，也支持 `125k/250k/500k` |
| 离线升级按键 | `PA6`，上拉输入，低电平有效，稳定 60 ms 触发 |
| 调试 LED | `PB15`，App 200 ms 翻转，IAP 500 ms 翻转 |
| 看门狗 | App/IAP 默认启用 IWDG |

Flash 地址：

| 区域 | 地址 |
| --- | --- |
| comm tool IAP | `0x08000000..0x08007FFF`，32 KB |
| comm tool App | `0x08008000..0x08017FFF`，64 KB |
| 固件缓存区 | `0x08018000..0x0807F7FF` |
| 缓存元数据页 | `0x0807F800` |
| SRAM mailbox | `0x2000FFE0` |
| BMS App 地址 | `0x08004800` |

安全规则：

- `COMM_TOOL_Release.bin` 不能裸写到 `0x08000000`，否则覆盖 comm tool IAP。
- comm tool App 自升级镜像地址必须是 `0x08008000`。
- BMS App 镜像地址必须是 `0x08004800`。
- `FW_END` 校验失败时缓存无效，禁止执行 `UPGRADE`。

## 4. 当前 App 主循环逻辑

`main.c` 的核心循环如下：

1. 从 PC UART 取字节。
2. 每个字节同时喂给：
   - `CtModbusBridge_FeedPcByte()`：识别原始 Modbus RTU，转发到 USART2。
   - `CtProtocol_Feed()`：识别 `55 AA` 私有协议帧，解析成功后进入 `CtApp_HandleFrame()`。
3. `CtModbusBridge_Task()` 处理 BMS USART2 返回并转发给 PC。
4. `CtApp_Poll()` 处理：
   - PA6 离线升级按键；
   - CAN-IAP 升级状态机；
   - comm tool 自身 CAN App `ENTER_IAP` 入口；
   - 自身 IAP reset 延迟任务。
5. `Board_Poll()` 处理 LED。
6. 喂 IWDG。

这个设计让同一个 PC UART 同时支持两类入口：

- `55 AA` 开头：comm tool 私有协议。
- 非 `55 AA` 且符合 Modbus RTU：透明转发到 BMS。

## 5. PC 与 comm tool 私有串口协议

### 5.1 帧格式

小端字段，CRC 为 Modbus CRC16。

| 字段 | 长度 | 说明 |
| --- | --- | --- |
| magic | 2 | `0xAA55`，线上字节为 `55 AA` |
| version | 1 | 当前 `1` |
| flags | 1 | bit0=`ACK` |
| seq | 2 | PC 递增序号，响应必须匹配 |
| cmd | 1 | 命令 |
| status | 1 | 响应状态 |
| length | 2 | payload 长度 |
| payload | 0..512 | 命令数据 |
| crc16 | 2 | 覆盖 header + payload |

响应状态：

| status | 含义 |
| --- | --- |
| `0x00` | OK |
| `0x01` | CRC_ERROR |
| `0x02` | UNSUPPORTED |
| `0x03` | BAD_PARAM |
| `0x04` | BAD_STATE |
| `0x05` | FLASH_ERROR |
| `0x06` | CAN_TIMEOUT |
| `0x07` | BMS_ERROR |

### 5.2 命令表

| cmd | 名称 | payload | 说明 |
| --- | --- | --- | --- |
| `0x01` | `GET_INFO` | 空 | 读取协议版本、固件版本、CAN 参数、缓存地址/大小、目标节点 |
| `0x02` | `SET_CAN` | `bitrate:u32 node:u8 app_can_addr:u8 reserved:u16` | 设置 CAN 波特率、IAP node、BMS App CAN 地址 |
| `0x10` | `BMS_READ` | `addr:u16 count:u16` | 通过 CAN 读 BMS 寄存器 |
| `0x11` | `BMS_WRITE` | `addr:u16 count:u16 words[count]:u16_le[]` | 通过 CAN 写 BMS 寄存器 |
| `0x12` | `BMS_AGING_CTRL` | `action:u8` | 老化 start/stop/reset-time |
| `0x13` | `BMS_AGING_STATUS` | 空 | 等待并解析 BMS `0x14F80208` 老化广播 |
| `0x14` | `BMS_AGING_SET_HOURS` | `hours:u16` | 设置 BMS 老化时长，范围 1..168 h |
| `0x20` | `FW_BEGIN` | `app_addr:u32 size:u32 crc16:u16 crc32:u32` | 初始化 Flash 缓存 |
| `0x21` | `FW_DATA` | `offset:u32 data[n]` | 写缓存数据 |
| `0x22` | `FW_END` | `size:u32 crc16:u16 crc32:u32` | 结束并校验缓存 |
| `0x23` | `FW_INFO` | 空 | 读取缓存元数据 |
| `0x30` | `ENTER_IAP` | 空 | 让目标 BMS App 或目标 comm tool App 进入 IAP，取决于 `app_can_addr` |
| `0x31` | `UPGRADE` | 空 | 从缓存启动 CAN-IAP |
| `0x32` | `UPGRADE_STATUS` | 空 | 查询升级状态 |
| `0x33` | `UPGRADE_ABORT` | 空 | 中止当前升级状态机 |
| `0x40` | `RAW_CAN_TX` | `id:u32 ide:u8 dlc:u8 data[8]` | 调试用原始 CAN 发送 |
| `0x41` | `CAN_DIAG` | `clear:u8` | 读取 CAN 诊断计数和最后帧 |
| `0x42` | `DEBUG_LOG` | 空 | 读取调试环形日志 |

升级过程中只允许：

- `GET_INFO`
- `FW_INFO`
- `UPGRADE_STATUS`
- `UPGRADE_ABORT`
- `CAN_DIAG`
- `DEBUG_LOG`

其它命令返回 `BAD_STATE`。

## 6. 原始 Modbus RTU 透传逻辑

`ct_modbus_bridge.c` 负责 PC UART 到 USART2 的透明桥。

支持的 Modbus 功能码：

| func | 含义 |
| --- | --- |
| `0x03` | Read Holding Registers |
| `0x06` | Write Single Register |
| `0x10` | Write Multiple Registers |

关键规则：

- PC UART 保持 `115200 8N1`。
- BMS UART2 固定 `19200 8N1`，`PA2=TX`、`PA3=RX`。
- 最大 RTU 帧长 `1040` bytes。
- PC 侧帧间隔超时 `100 ms`。
- BMS 侧响应帧间隔超时 `100 ms`。
- BMS 总响应超时 `3000 ms`。
- 请求转发前检查 Modbus CRC。
- BMS 返回后再次检查 CRC，通过后原样转发给 PC。
- 如果 PC 发送 `55 AA` 私有协议头，则该帧被私有协议过滤，不进入 Modbus 透传。
- `0x10 / 0xFFFE` IAP 数据块允许 `byte_count=0`，此时真实长度取 count 字段，用于兼容旧串口 IAP。

安全边界：

- 当前运行态 App 不把 PC 原始 Modbus `0xFFFD` 作为 comm tool 自身进 IAP 入口处理。
- 这样做是为了让 BMS 直连串口 IAP 经 comm tool 透传时，不会误触发 comm tool 自身复位。

## 7. comm tool 到 BMS App CAN 服务协议

CAN App 服务用于 BMS 正常 App 运行态下读写寄存器、进入 IAP 和老化控制。

| 方向 | CAN ID |
| --- | --- |
| comm tool -> BMS App | `(app_can_addr << 7) | 0x60` |
| BMS App -> comm tool | `(app_can_addr << 7) | 0x61` |

请求帧：

| byte | 含义 |
| --- | --- |
| 0..1 | `A5 5A` |
| 2 | cmd |
| 3..5 | 参数 |
| 6..7 | CRC16，覆盖 byte0..5 |

响应帧：

| byte | 含义 |
| --- | --- |
| 0..1 | `5A A5` |
| 2 | cmd |
| 3 | status |
| 4..5 | 返回参数 |
| 6..7 | CRC16，覆盖 byte0..5 |

命令：

| cmd | 名称 | 用途 |
| --- | --- | --- |
| `0x01` | `GET_STATUS` | 读 SOC/SOH |
| `0x02` | `ENTER_IAP` | guard `C3 3C can_addr`，让目标 App 复位进 IAP |
| `0x03` | `READ_REG` | 读单寄存器 |
| `0x04` | `WRITE_PREP` | 写寄存器准备，高字节 |
| `0x05` | `WRITE_COMMIT` | 写寄存器提交，低字节 |
| `0x06` | `READ_BLOCK` | 块读，最多 120 words |
| `0x07` | `AGING_START` | 老化开始 |
| `0x08` | `AGING_STOP` | 老化停止 |
| `0x09` | `AGING_RESET_TIME` | 老化累计时间清零 |
| `0x0A` | `AGING_SET_HOURS` | 设置老化时长 |
| `0x86` | `READ_BLOCK_DATA` | BMS 返回块读数据帧 |

块读逻辑：

1. comm tool 先发 `READ_BLOCK`。
2. BMS ACK 返回数量。
3. BMS 以 `READ_BLOCK_DATA` 返回每个 word。
4. comm tool 按 seq 去重组包，最多等待 6 秒。
5. 如果目标不支持块读且 count `<=4`，comm tool 回退到逐个 `READ_REG`。
6. count 大于 4 时不回退，避免长时间低效轮询。

## 8. 固件缓存逻辑

缓存由 `ct_flash_store.c` 管理。

| 项目 | 当前值 |
| --- | --- |
| 缓存数据区 | `0x08018000..0x0807F7FF` |
| 缓存元数据页 | `0x0807F800` |
| magic | `0x43544657` |
| valid | `0xA55A5AA5` |

缓存支持两类镜像：

| 镜像 | app_addr | 大小限制 |
| --- | --- | --- |
| BMS App | `0x08004800` | 不超过 `0x08020000 - 0x08004800` |
| comm tool App | `0x08008000` | 不超过 `0x10000` |

下载流程：

1. PC 校验 bin 向量表和地址范围。
2. PC 发送 `FW_BEGIN(app_addr, size, crc16, crc32)`。
3. comm tool 擦除缓存数据区对应页和元数据页。
4. PC 多次发送 `FW_DATA(offset, data)`；CLI 默认 data 为 496 bytes。
5. PC 发送 `FW_END(size, crc16, crc32)`。
6. comm tool 回读缓存计算 CRC16，匹配后写入 valid 元数据。

当前 GUI 固定使用 BMS App 地址 `0x08004800`；CLI 可显式传入 `--app-address 0x08008000` 下载 comm tool App。

## 9. BMS CAN-IAP / comm tool CAN-IAP 协议

BMS IAP 和 comm tool IAP 当前复用同一套 CAN-IAP 扩展帧协议。

CAN ID：

| 方向 | 扩展帧 ID |
| --- | --- |
| comm tool -> IAP 控制帧 | `0x14F8F000 | node` |
| IAP -> comm tool ACK/NACK | `0x14F8F100 | node` |
| comm tool -> IAP 数据帧 | `0x14000000 | (seq << 8) | node` |

控制命令：

| cmd | payload | 说明 |
| --- | --- | --- |
| `0x01 HELLO` | `01 version node FF FF FF FF FF` | 握手 |
| `0x02 START` | `02 version size:u32_be crc16:u16_be` | 开始升级 |
| `0x03 COMMIT` | `03 block_seq:u16_be block_len:u16_be block_crc:u16_be FF` | 提交一个块 |
| `0x04 END` | `04 frame_count:u16_be crc16:u16_be FF FF FF` | 结束升级 |
| `0x05 ABORT` | `05 reason FF FF FF FF FF FF` | 终止 |
| `0x79 ACK` | `79 cmd state expect_seq:u16_be code FF FF` | 成功/状态响应 |
| `0x1F NACK` | `1F cmd code expect_seq:u16_be code FF FF` | 失败响应 |

数据规则：

- 每个数据帧固定 8 bytes。
- `seq` 从 0 递增。
- comm tool App 每 32 帧组成一个 256-byte 块。
- 最后一块不足 256 bytes 时，缓存块内补 `0xFF`，但 `COMMIT.block_len` 只覆盖真实长度。
- IAP 只有在 `COMMIT` 块 CRC 正确后才写 Flash。
- `END` 校验整包 CRC16、frame_count 和 App 向量。

升级状态机：

1. 快速 HELLO：先判断目标是否已经在 IAP。
2. 如果 700 ms 内没有有效 HELLO ACK，则发送 App `ENTER_IAP`。
3. 等待 1200 ms 启动窗口。
4. 8 秒内每 250 ms 重发 HELLO。
5. START，等待 2 秒 ACK。
6. 循环发送 8-byte DATA，32 帧后 COMMIT，等待 2 秒 ACK。
7. END，等待 5 秒 ACK。
8. 成功后 `UPGRADE_STATUS.state=2`，`percent=100`。

错误码由 `last_error` 暴露给 PC，常见值：

| error | 含义 |
| --- | --- |
| `0x01` | 缓存无效、地址不支持或目标类型不匹配 |
| `0x02` | HELLO 失败 |
| `0x03` | START 失败 |
| `0x04` | 读缓存块失败 |
| `0x05` | CAN 数据帧发送失败 |
| `0x06` | COMMIT 失败 |
| `0x07` | END 失败 |
| `0x21` | App `ENTER_IAP` 失败 |
| `0x22` | 状态机非法 phase |

## 10. BMS 一键升级流程

GUI 的“comm tool/CAN桥”一键升级当前流程：

1. 用户选择 BMS App bin。
2. GUI 按 `0x08004800` 校验向量和大小。
3. GUI 自动应用 CAN 设置：bitrate、IAP node、BMS App CAN address。
4. GUI 通过 `FW_BEGIN/FW_DATA/FW_END` 把 bin 写入 comm tool 缓存。
5. GUI 读取 `FW_INFO`，核对 app_addr、size、CRC16、CRC32、valid。
6. GUI 发 `UPGRADE`。
7. comm tool 状态机通过 CAN 执行 HELLO、ENTER_IAP、START、DATA、COMMIT、END。
8. GUI 轮询 `UPGRADE_STATUS` 显示进度。
9. 完成后 GUI 读 BMS 状态/版本确认 App 恢复。

“使用缓存升级”流程：

1. 用户仍需选择本地 bin。
2. GUI 读取 comm tool 缓存。
3. 缓存 app_addr、size、CRC16、CRC32 与本地 bin 完全一致才允许升级。
4. 不重新下载缓存，直接发 `UPGRADE`。

## 11. PA6 离线批量升级流程

PA6 用于脱离 PC 后批量升级 BMS。

触发条件：

- PA6 低电平稳定 60 ms。
- 当前没有升级正在运行。
- 缓存有效。
- 缓存 `app_addr == 0x08004800`，即 BMS App 缓存。

触发后：

1. comm tool 使用当前内存里的 `s_node_id` 和 `s_app_can_addr`。
2. 从 Flash 缓存读取 BMS App。
3. 执行同一套 CAN-IAP 状态机。

限制：

- PA6 只启动 BMS App 缓存升级，不会启动 comm tool App 自升级。
- 离线状态下无法通过 PC 查询进度，只能通过 BMS 行为、CAN 抓包或 LED/调试手段判断。
- 批量夹具必须保证同一时间目标 IAP node 唯一；多个相同 IAP node 同时响应会造成 ACK 串扰。

## 12. comm tool 自升级当前能力

### 12.1 IAP 固件侧能力

comm tool 自身 IAP 支持两条协议：

1. 旧 BMS 串口 IAP 协议。
2. 当前 CAN-IAP 协议。

串口 IAP：

| 地址 | 含义 |
| --- | --- |
| `0xFFFD` | connect / 初始化升级会话 |
| `0xFFFE` | 数据块，最大 1024 bytes |
| `0xFFFF` | 完成升级 |

IAP 串口和 App 的 PC UART 使用同一 `CT_COMM_UART_PORT` 配置，当前默认 USART1 `115200 8N1`。

CAN-IAP：

- 目标 comm tool App CAN 地址固定 `14`。
- 收到 App 服务 `ENTER_IAP` 后，comm tool App 写 SRAM mailbox 并复位进入 IAP。
- 进入 IAP 后复用第 9 节的 CAN-IAP 扩展帧协议。

### 12.2 当前 CLI 支持的升级另一台 comm tool 流程

PC 连接“主控 comm tool”，目标是 CAN 总线上的“另一台 comm tool”：

```powershell
.\tools\start_comm_tool_host.ps1 -Mode fw-download -Port COM4 -Bin "firmware\comm_tool_f103ret6\keil\Objects\Release\COMM_TOOL_Release.bin" -AppAddress 0x08008000 -ConfirmAppAddress 0x08008000

py -3.9 tools\comm_tool_host.py set-can --port COM4 --app-can-addr 14 --node-id 1

py -3.9 tools\comm_tool_host.py enter-iap --port COM4 --confirm-enter-iap

py -3.9 tools\comm_tool_host.py upgrade --port COM4 --confirm-upgrade --long-timeout 120
```

这条路径使用现有协议，不需要修改固件。

限制：

- 需要两台 comm tool：一台作为升级器，一台作为目标。
- 目标总线上同一时刻只能有一个 IAP node 为 `1` 的目标。
- `COMM_TOOL_Release.bin` 必须按 `app_addr=0x08008000` 下载缓存。

### 12.3 当前 GUI 的限制

当前 `BMS_CommTool_Upgrade_UI.exe`：

- `_write_firmware_to_cache()` 固定使用 `APP_BASE_ADDR = 0x08004800`。
- `_assert_cache_matches()` 也固定要求缓存地址为 `0x08004800`。
- 因此 GUI 不能直接用于 comm tool App 镜像 `0x08008000` 的缓存下载和升级。

## 13. 后续上位机支持 comm tool 升级的推荐方案

### 13.1 方案 A：GUI 支持“通过另一台 comm tool 升级目标 comm tool”

这是最小改动方案，不需要改固件。

UI 改动：

1. 固件目标增加选择：
   - `BMS App`：地址 `0x08004800`。
   - `comm tool App`：地址 `0x08008000`。
2. 当选择 `comm tool App`：
   - 只允许 `comm tool/CAN桥` 模式。
   - bin 默认建议选择 `firmware/comm_tool_f103ret6/keil/Objects/Release/COMM_TOOL_Release.bin`。
   - 强制目标 App CAN 地址为 `14`，或弹窗明确提示并自动设置。
   - IAP node 默认 `1`，允许用户改，但必须保证总线唯一。
   - 缓存校验改为按 `0x08008000` 判断。
3. “一键升级”流程复用现有 `FW_BEGIN/FW_DATA/FW_END + ENTER_IAP + UPGRADE + UPGRADE_STATUS`。
4. 完成后不再尝试读 BMS `0xD034/0xC002`，改为提示用户重新读取主控/目标 comm tool 信息或断电重连验证。

安全校验：

- `COMM_TOOL_Release.bin` 的 MSP 必须在 SRAM。
- ResetHandler 必须落在 `0x08008000..0x08017FFF`。
- 镜像大小不得超过 `0x10000`。
- 禁止 app_addr 为 `0x08000000`。
- 明确提示“该功能用于升级另一台 comm tool，不是当前 PC 正连接的主控 comm tool”。

### 13.2 方案 B：GUI 支持“PC 串口升级当前 comm tool”

这需要固件新增一个安全入口，否则当前 App 运行态不会通过 PC 串口进入自身 IAP。

推荐固件改动：

1. 在 PC 私有协议中新增命令，例如：
   - `CT_CMD_SELF_ENTER_IAP = 0x34`
2. payload 必须带 guard，例如：
   - `magic:u32 = 0x43544950`
   - `guard:u32 = 0xA55A5AA5`
3. App 收到后：
   - 确认没有升级正在运行。
   - 调用 `CtBoot_RequestIap()`。
   - 先返回 ACK。
   - 延迟 20 ms 后 reset。
4. IAP 启动后，GUI 切换到旧 BMS 串口 IAP 协议：
   - 写 `0xFFFD` 初始化。
   - 写 `0xFFFE` 数据块。
   - 写 `0xFFFF` 完成。

UI 改动：

1. 新增“升级当前 comm tool”入口，和 BMS 升级入口分开。
2. 固件地址固定 `0x08008000`。
3. 串口固定当前 PC UART，波特率 `115200`。
4. 进入 IAP 后使用 IAP slave `1`。
5. 写块失败或 ACK 超时，不原地重发；提示必须从 `0xFFFD` 和第 0 块重新开始。

安全边界：

- 不复用原始 Modbus `0xFFFD` 作为 App 运行态自升级入口，避免和 BMS 透传 IAP 冲突。
- 新命令必须只能在私有协议帧里触发，且必须带 guard。
- 升级当前 comm tool 时，禁止同时启用 BMS Modbus 透传任务中的等待状态。

### 13.3 推荐落地顺序

1. 先实现方案 A：GUI 增加 `comm tool App` 目标类型，复用现有缓存和 CAN-IAP。
2. 再实现方案 B：固件新增 `SELF_ENTER_IAP` 私有命令，GUI 增加当前设备串口自升级。
3. 最后补充自动化验证：
   - BMS App 缓存下载仍为 `0x08004800`。
   - comm tool App 缓存下载为 `0x08008000`。
   - BMS 一键升级流程不受影响。
   - Modbus 透传 `0xFFFD` 仍进入 BMS，不误触发 comm tool 自身 IAP。
   - comm tool 当前设备串口自升级必须只由 `SELF_ENTER_IAP` 触发。

## 14. 维护检查清单

改 comm tool 或上位机升级相关逻辑时，至少检查：

- `ct_config.h` 地址常量是否仍一致。
- App 和 IAP 的 `CT_COMM_UART_PORT` 是否一致。
- GUI 是否仍覆盖生成 `dist/BMS_CommTool_Upgrade_UI.exe`。
- `FW_BEGIN.app_addr` 是否按目标类型区分。
- `UPGRADE` 时 `app_can_addr=14` 是否只用于 comm tool App 目标。
- PA6 离线升级是否仍只允许 BMS App 缓存。
- BMS 直连串口经 comm tool 透传时，`0xFFFD/0xFFFE/0xFFFF` 是否仍能到达 BMS。
- CAN-IAP 总线上 IAP node 是否唯一。
- 看门狗喂狗点是否覆盖 Flash 擦写、串口等待、CAN 等待和 IAP 主循环。
