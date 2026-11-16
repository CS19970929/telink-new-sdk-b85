# PC 与 Comm Tool 串口协议

## 双协议扩展（App 0.2.7）

外层55 AA、version=1、序号、CRC16及旧命令均保持；双协议只增加下表命令。
IAP不参与一线通接收，更新接收能力只需升级Comm Tool App；上位机不经此链路修改BMS协议宏。

| 命令 | 请求 | 响应 |
|---|---|---|
| 0x45 OWC_INFO | 空 | 8字节能力信息 |
| 0x46 OWC_CONFIG | enable:u8, mode:u8 | 空；停止/重配均清缓存与计数 |
| 0x47 OWC_FRAME | selector:u8 | 36字节v3头+原始报文 |

mode：0自动、1 A1.3.3、2锂神V1.1.1；enable仅0/1。未知长度、模式或selector返回BAD_PARAM。
默认及旧0x43 enable=1始终选择A1.3.3，旧0x44继续输出原v2；双协议模式下旧0x44拒绝，不将锂神raw塞入旧快照。
OWC_INFO为只读能力查询；其它新命令沿用升级互斥。USART3占用PC10/11时能力mask=0且禁止启用。

OWC_INFO：[1, mode_mask, 3, 0, header_bytes:u16, max_raw_bytes:u16]，
当前mask=7、header_bytes=36、max_raw_bytes=259。mask的bit0/1/2分别表示自动/A1.3.3/锂神。

v3头的0..31沿用旧头各字段含义，version为3；增加：
| 偏移 | 字段 |
|---|---|
|32|mode（0/1/2）|
|33|当前锁定protocol：0尚未锁定、1 A1.3.3、2锂神|
|34|该槽raw的protocol；空槽为0|
|35|最近发现的不匹配协议：0无、1 A1.3.3、2锂神|
|36|raw[length]|

selector：00最近完整帧（可含坏SUM）、01/3A/3B/3C为A1.3.3有效缓存、80为锂神有效缓存。
80只是PC查询selector，锂神raw首字节仍为00。有效缓存的raw协议必须与锁定协议一致，不能跨协议或会话展示。
上位机一组查询核对version/session/enable/mode/锁定protocol，变化则整组重读一次，再变化即报错。
旧PC快照v1/v2只按A1.3.3解析；新快照通过0x47显式获取，不凭raw长度猜协议。

### 接收与新鲜度

共用PC11 EXTI边沿队列、TIM2计时和PC10上拉。ISR只记录，主循环有界处理。
30ms以上使用毫秒差保留62ms同步，100ms以上饱和作长空闲；短脉冲保持us精度，处理TIM2和ms tick回绕。
同步低电平可以与空闲低电平相连，由其后的高脉冲定界：
A1.3.3低>=9ms/高0.75..1.25ms；锂神低>=54ms/高1.5..2.6ms。
锂神MSB first：1低1.5..2.5ms/高3.5..4.5ms，0低3.5..4.5ms/高1.5..2.5ms，总周期5.2..6.8ms。
锂神结束低22..28ms先于同步检测处理；高脉冲由>=1.5ms实际边沿或安全的>=3ms轮询确认，数据无边沿35ms超时。
这些是软件接收容差，不替代发送协议标称值或实板波形验收。

锂神固定 [00,SOC%,总压V,温度int8摄氏度,00,SUM8]；SOC<=100、保留字节为0、长度严格6，SUM为前5字节加和低8位。
只展示SOC、总压、电池温度，不产生协议未提供的电流、单体、编码或MOS温度。

自动模式连续两帧通过形状、时序和SUM才锁定；坏帧/溢出打断确认。失联>5秒清全部缓存并解除锁定，重新确认后递增会话。
固定模式收到另一种合法协议仅报告不匹配，不授权其测量；上位机应隐藏旧测量并提示切换模式。
接收控制/模式改变清缓存；原10秒查询租约与停止释放上拉规则保留。5秒在线门限及A1.3.3长周期75秒规则保持。
新上位机查询能力，只有明确UNSUPPORTED响应才回退到旧A1.3.3；超时、CRC或非法能力不能冒充旧固件。
旧固件可用自动选项回退A1.3.3并明确显示限制；手动选择锂神则提示升级Comm Tool App。

以下为0.2.6旧命令及A1.3.3合同，继续供兼容路径使用。


## 一线通扩展（Comm Tool App 0.2.6，快照版本 2）

保持原 `55 AA` 外层帧、版本、序号、小端和 CRC16 不变，新增以下命令。接收器默认关闭，只监听 BMS，不发送业务请求或修改 BMS 参数。

| 命令 | 请求 payload | 响应 |
|---|---|---|
| `0x43 OWC_CONTROL` | `enable:u8`，只能为 0 或 1 | 空 payload；切换时清空本次会话缓存和计数 |
| `0x44 OWC_SNAPSHOT` | `id:u8`，只能为 `00/01/3A/3B/3C` | 32 字节头 + 原始一线通主报文 |

`id=0` 查询最近完整帧，包含校验失败帧；其余 ID 只返回对应的最近有效帧。没有数据时 length=0、available=0、age_ms=`0xFFFFFFFF`。有效查询续期接收器的 10 s 租约；停止命令或租约到期关闭 OW 上拉。升级运行时沿用既有命令准入规则，拒绝这两个新增命令。

响应头布局（所有 `u16/u32` 为小端）：

| 偏移 | 字段 | 含义 |
|---|---|---|
| 0 | `version:u8` | 当前固件发送 2；上位机同时识别已交付固件的 1，两者使用相同的 32 字节头和 raw 布局 |
| 1 | `enabled:u8` | 接收器是否启用 |
| 2 | `available:u8` | 本槽是否存在完整报文 |
| 3 | `checksum_ok:u8` | 原始报文累加校验是否通过 |
| 4 | `age_ms:u32` | 距离结束低脉冲上升沿的时间，不是 PC 查询时间 |
| 8 | `good_count:u32` | 本次接收启用期间有效帧数；换电池不清诊断计数 |
| 12 | `checksum_errors:u32` | 完整帧的校验失败次数 |
| 16 | `timing_errors:u32` | 脉宽、位周期、长度、协议版本或接收超时错误次数 |
| 20 | `overflows:u32` | 主循环发现的边沿队列溢出批次，不等于丢失边沿数 |
| 24 | `sequence:u32` | 本槽完整报文序号，跨校验成功/失败递增 |
| 28 | `length:u16` | 原始主报文字节数，最多 259 |
| 30 | `session:u16` | v2 电池接入会话，0 表示尚未收到有效帧；回绕跳过 0。v1 此处是保留字段，必须为 0，不代表会话 |
| 32 | `raw[length]` | ID 到 SUM，保留 BMS 实际字节 |

波形规则以 A1.3.3 为依据：先低后高、LSB first、低脉冲 0.5 ms 为 1、1.5 ms 为 0，位周期 2 ms。接收窗口为短脉冲 300..750 us、长脉冲 1250..1750 us、位周期 1700..2300 us、同步低电平至少 9000 us（10 ms 的接收容差）、同步高电平 750..1250 us；结束低电平 4500..6500 us，结束高电平用毫秒 tick 等待至少 51 ms，覆盖 tick 取整误差。数据阶段超过 25 ms 无边沿则丢弃。未知 ID、版本半字节为 F、非法私有长度或多余字节均不发布。

`0x01` 固定 20 字节；`0x3A/0x3B/0x3C` 总长为 `raw[2]+4`。所有 ID 的 SUM 均校验之前所有字节之和低 8 位。本项目线版本选用 `0x13`（高4位主版本1、低4位次版本3），文档末位修订号3不另占字节；这与PC快照version=2是两个独立字段。`0x3A` 数据长度 27..29（加密结果 16..32 bit），`0x3B` 为非空电压对，`0x3C` 为非空编码。用户明确不实现加密、充电策略和互认；不宣称完整支持 A1.3.3 智能充电。

上位机只使用 A1.3.3 标准字段，已删除 legacy/profile 参数：温度减 40，电流乘 0.1 后减 500（正放电、负充电）。无效或超范围测量显示 `--`。一线通单体无效值统一为 `0xFFFF`，BMS 内部及其它协议的 `61001` 约定不变。本地 BMS `key=0` 明确标记未实现加密（不是合法认证报文），状态及加密结果填 `FF`，不解码或推断 MOS/许可。工作状态无有效测量或静置时为本项目未知值 `FF`；文档没有定义静置码，不把未知状态伪造为回馈。请求应答位只在有效充放电状态下为 0（不请求应答），未实现应答业务。

v2 固件收到第一帧有效报文、超过 5 s 无有效帧后恢复、或公有帧在其它有效 ID 之后重新出现时，清空全部原始缓存并递增 session；连续公有帧属于同一会话。坏校验不切换会话。上位机读取五个 ID 后必须检查 version/session/enable 一致，跨会话整组重读，仍不一致则报错。无电气断开证据、无新公有帧、且间隔不超过 5 s 的设备替换无法可靠区分；换电池应停止再开始监控。

v1 固件没有会话号及上述自动清缓存机制；界面显示 `snapshot=v1, session=--`，不伪造会话号。开始监控的 `0x43 enable=1` 会清空缓存，换电池或 BMS 重启后必须停止再开始，避免复用旧身份或单体。上位机对两种快照都检查长度、ID、SUM 和数据有效期，BMS 字段仍仅按 A1.3.3 标准解析；支持旧 PC 快照不恢复 Telink legacy 字段解析，也不要求为本次上位机修复重刷 Comm Tool。

任一有效帧年龄不超过 5 s 才认为链路在线；`3A` 的 5 s、`3B/3C` 的 75 s 有效期不能替代在线判断。离线时不显示旧测量和编码；最新3A电压无效或3A已过期时立即隐藏旧单体，不等待下次60s单体帧；Comm Tool收到有效3A的电压FFFF时清单体缓存，采样恢复后也必须等新的单体帧；`01` 只保留本次会话的静态接入资料，并标明离线，不显示其中的历史电流/电压。接收关闭时清空显示。GUI/CLI 发现租约到期后保持关闭 2 s，再重新启用，等待新的 BMS 接入资料。

Telink `0x3C` 使用已提交的 `Config.user.serial[32]`，接入顺序为三帧 `01`、一帧 `3B`、三帧 `3C`，随后 `3C` 跟随约60秒周期的 `3B` 发送。当前接入触发为已有 OW 上拉检测，未新增或假定独立 DETD 硬件。格式为 `3C 13 len ASCII... SUM`：len 为字符字节数、不含 NUL，总长 `len+4`，SUM 覆盖 ID/版本/len/全部字符。正常载荷为1～32字节可打印 ASCII（0x20..0x7E），保留其原始空格；空配置/全空格/非法字节/读取失败发 `len=1, payload=FF`，上位机显示 `--`。不采用 `0xC002` 显示用的共享编译默认 SN，也不产生或推断新条码。Comm Tool 继续校验长度和 SUM，原样返回编码与无效载荷；字段显示由上位机负责。

源码归属：`ct_owc.*` 只在主循环解码和保存报文；`board_owc.*` 独占 TIM2 1 MHz 自由计数、PC11 EXTI11 双边沿和 PC10 上拉控制。ISR 只采样并写 512 项环形队列；溢出后整批丢弃并重新找同步头。超过 30 ms 的边沿间隔用毫秒 tick 识别，避免 16 bit TIM2 回绕变成伪脉宽。

硬件来自 `TYPE-C-CAN-OWC-V1.pdf`：`SCI3-RX=PC11`，经 Q2/Q1 两次反相后与 OW 同极性；`SCI3-TX=PC10` 高电平通过 Q4/Q3 使能 R61 上拉。PC UART 必须使用默认 USART1 `PB6/PB7`，若改为 USART3，OWC_CONTROL 返回 UNSUPPORTED，不复用 PC UART 引脚。原理图标注 RCT6，而当前工程及 Flash 缓存布局为 RET6；实板型号确认前不得把本次软件验证视为该原理图整板已适配。

## 串口参数

默认参数：`115200 8N1`，无流控。PC 端固定使用 Windows Python Launcher：

```powershell
py -3.9 tools\comm_tool_host.py ...
```

## 帧格式

所有多字节字段为小端。

| 字段 | 长度 | 说明 |
| --- | --- | --- |
| magic | 2 | 固定 `55 AA` |
| version | 1 | 当前 `01` |
| flags | 1 | bit0 为 ACK 帧 |
| seq | 2 | PC 发起递增序号，响应沿用请求序号 |
| cmd | 1 | 命令号 |
| status | 1 | 请求固定 `00`，响应为状态码 |
| length | 2 | payload 字节数 |
| payload | N | 命令数据 |
| crc16 | 2 | CRC16-Modbus，覆盖 crc 前所有字段 |

最大 payload 第一阶段限制为 `512` 字节。固件下载 `FW_DATA` 默认数据分块为 `496` 字节，实际 payload 为 `offset:u32 + data[496]`，低于协议上限。

## 状态码

| 状态码 | 含义 |
| --- | --- |
| `0x00` | 成功 |
| `0x01` | CRC 错 |
| `0x02` | 命令不支持 |
| `0x03` | 参数错误 |
| `0x04` | 状态不允许 |
| `0x05` | Flash 错误 |
| `0x06` | CAN 超时 |
| `0x07` | BMS 返回错误，常见原因是 BMS App 未响应、地址非法、参数越界或写权限关闭 |

## 设备目标

comm tool 默认目标为：CAN 波特率 `250000`、BMS App CAN 地址 `0`、IAP 节点 `1`。comm tool 自身 App 的 CAN 服务地址固定为 `14`，用于另一台 comm tool 触发它进入自身 IAP。

多设备总线中，读写和进入 IAP 先按 BMS App CAN 地址选择目标；只有被选中的 BMS App 会响应 `ENTER_IAP` 并复位进入 IAP。BMS 进入 IAP 后使用 IAP 节点帧升级。当前 IAP 默认节点为 `1`，因此如果总线上已有多个 BMS 同时停留在 IAP 且节点相同，comm tool 无法区分它们，禁止直接升级。

`UPGRADE_STATUS.last_error` 为 comm tool 内部阶段码，常用值：

| 阶段码 | 含义 |
| --- | --- |
| `0x02` | IAP `HELLO` 未收到 ACK |
| `0x03` | IAP `START` 未收到 ACK 或被拒绝 |
| `0x06` | IAP `COMMIT` 未收到 ACK 或被拒绝 |
| `0x07` | IAP `END` 未收到 ACK 或被拒绝 |
| `0x21` | 一键升级自动发送 App `ENTER_IAP` 未收到 ACK |

`0x02` 出现在写入前，表示还没有收到 BMS IAP 的 `HELLO` ACK。comm tool 固件会在 App 复位进入 IAP 后等待启动窗口，并在 8 秒内每 250ms 重发 `HELLO`；如果仍失败，应优先检查 BMS IAP 是否真正运行、CAN 位时序、IAP 节点是否唯一，以及总线上是否已有其它相同节点的 IAP 设备。

## 命令

| 命令 | 方向 | payload | 说明 |
| --- | --- | --- | --- |
| `0x01 GET_INFO` | PC -> comm | 空 | 读取 comm tool 版本、Flash 缓存能力、CAN 波特率、目标节点、USART2/BMS 波特率 |
| `0x02 SET_CAN` | PC -> comm | `bitrate:u32 node:u8 app_can_addr:u8 reserved:u16` | 设置 CAN 参数、IAP 节点和 BMS App CAN 地址 |
| `0x10 BMS_READ` | PC -> comm | `addr:u16 count:u16` | 通过 CAN 读取 BMS 寄存器 |
| `0x11 BMS_WRITE` | PC -> comm | `addr:u16 count:u16 words[count]` | 通过 CAN 写 BMS 寄存器 |
| `0x12 BMS_AGING_CTRL` | PC -> comm | `action:u8` | 通过 CAN 单独控制 BMS 老化模式，`0x51` 开启，`0x50` 关闭并提前结束本轮老化时间，`0x5A` 重置时间 |
| `0x13 BMS_AGING_STATUS` | PC -> comm | 空 | 等待并解析 BMS `0x14F80208` 广播，返回 `state:u8 remaining_minutes:u16_le`，用于 UI 显示老化剩余时间 |
| `0x14 BMS_AGING_SET_HOURS` | PC -> comm | `hours:u16_le` | 修改 BMS 老化总时长，单位小时，范围 `1..168`；成功后返回 `state:u8 remaining_hours:u8 applied_hours:u16_le`，板端会同步重置累计老化时间 |
| `0x15 SET_BMS_UART` | PC -> comm | `baud:u32_le` | 设置 comm tool USART2/BMS 侧波特率；成功返回实际应用的 `baud:u32_le` |
| `0x20 FW_BEGIN` | PC -> comm | `app_addr:u32 size:u32 crc16:u16 crc32:u32` | 开始下载 BMS App 或 comm tool App 到 comm tool 缓存 |
| `0x21 FW_DATA` | PC -> comm | `offset:u32 data[n]` | 写入固件缓存 |
| `0x22 FW_END` | PC -> comm | `size:u32 crc16:u16 crc32:u32` | 结束下载并校验缓存 |
| `0x23 FW_INFO` | PC -> comm | 空 | 查询缓存固件信息 |
| `0x30 ENTER_IAP` | PC -> comm | 空 | 让 BMS App 写标志并复位进入 IAP |
| `0x31 UPGRADE` | PC -> comm | 空 | 启动使用缓存固件给 BMS 一键升级，命令返回后通过 `UPGRADE_STATUS` 查询进度 |
| `0x32 UPGRADE_STATUS` | PC -> comm | 空 | 查询升级状态 |
| `0x33 UPGRADE_ABORT` | PC -> comm | 空 | 终止当前升级 |
| `0x40 RAW_CAN_TX` | PC -> comm | `id:u32 ide:u8 dlc:u8 data[8]` | 调试用原始 CAN 发送 |
| `0x41 CAN_DIAG` | PC -> comm | `clear:u8` | 读取 comm tool CAN 发送、接收和错误寄存器诊断；`clear!=0` 表示读取前清零 |

### GET_INFO / CAN_DIAG 扩展字段

`GET_INFO` 前 20 字节保持兼容：

| offset | 长度 | 含义 |
| --- | --- | --- |
| 0 | 4 | `proto, major, minor, patch` |
| 4 | 4 | CAN 波特率 |
| 8 | 4 | 固件缓存起始地址 |
| 12 | 4 | 固件缓存大小 |
| 16 | 4 | flags |
| 20 | 1 | 当前 IAP 节点 |
| 21 | 1 | 当前 BMS App CAN 地址 |
| 22 | 2 | reserved |
| 24 | 4 | 当前 USART2/BMS 侧波特率 |

`CAN_DIAG` 第 61 字节为 IAP 节点，第 62 字节为 BMS App CAN 地址。旧工具只解析前 62 字节时仍可工作。

### BMS_READ / BMS_WRITE 细节

- `BMS_READ` 读取的是 BMS 原串口寄存器地址空间，不重新定义参数含义。
- `BMS_READ` 响应 payload 为 `words[count]`，每个寄存器 16 位小端。
- `BMS_WRITE` 请求 payload 为 `addr:u16 count:u16 words[count]`，每个寄存器 16 位小端。
- comm tool 当前一次最多转发 `120` 个寄存器；读多个寄存器时内部使用 CAN App `READ_BLOCK` 块读，BMS App 每 10ms 返回一个寄存器数据帧。
- BMS App 写入仍走 `Sci_Upper.c` 的地址、范围、副作用和权限检查。当前 Release 默认 `PROJECT_CFG_HOST_WRITE_ENABLE=1`，允许 UI 写保护参数和其它参数。
- UI 写 SH309 AFE 保护参数时不会逐个地址直接覆盖。`0x2400..0x2417` 会先读取整块、合并修改项、一次写回 24 个寄存器并回读校验；`0x2132..0x2136` MOS 过温参数同理整块写入。二级过流和短路电流列表由 BMS 当前 `0x231D` 采样电阻、`0x231E` 采样电阻数换算生成。
- UI 的 `读取BMS信息` 和 `实时监控` 使用 `0xD000` 起连续 `63` 个只读寄存器；`读取BMS状态` 使用 `0xD034/0xD035` 读取 SOC/SOH；`读取BMS日志` 使用 `0xC008` 完整事件记录窗口，数量固定为 `100` 个寄存器。`0xC008` 是旧串口协议特殊窗口，不能按 `0xC008 + offset` 拆分读取；UI 对完整窗口读取最多重试 3 次。

## 安全规则

- 下载 BMS App 时 `FW_BEGIN.app_addr` 必须等于 `0x08004800`，PC 工具真实下载必须显式传入 `-ConfirmAppAddress 0x08004800`。
- 下载 comm tool App 给另一台 comm tool 自升级时，`FW_BEGIN.app_addr` 必须等于 `0x08008000`，PC 工具真实下载必须显式传入 `-ConfirmAppAddress 0x08008000`。
- comm tool App 不能裸写到 `0x08000000`；该地址是 comm tool 自身 IAP。
- comm tool 写缓存前必须擦除缓存页，不能覆盖自身程序区。
- `FW_END` 校验失败时缓存固件无效，禁止执行 `UPGRADE`。
- 多个 BMS 同时挂在 CAN 总线时，必须保证目标 BMS App CAN 地址唯一；多个相同地址或多个相同 IAP 节点同时在线会造成响应串扰。

## comm tool 自升级兼容协议

comm tool IAP 同时支持旧 BMS 串口升级协议和当前 CAN-IAP 协议：

- 串口：由 `CT_COMM_UART_PORT` 统一选择，当前默认 USART1 `PB6/TX`、`PB7/RX`，`115200 8N1`。旧 BMS `0x10` 写多个寄存器命令地址仍为 `0xFFFD` 连接、`0xFFFE` 数据块、`0xFFFF` 完成。App 运行时收到 `0xFFFD` 后会用 SRAM mailbox 请求复位进入 IAP，并先 ACK 再复位，旧串口上位机可复用。
- CAN：目标 comm tool App 地址固定 `14`，收到 App 服务 `ENTER_IAP` 后复位进入 IAP；IAP 使用扩展帧 `0x14F8F000/0x14F8F100/0x14000000` 的 CAN-IAP 协议，节点默认 `1`。

另一台 comm tool 升级当前 comm tool 时，PC 先把 `COMM_TOOL_Release.bin` 以 `app_addr=0x08008000` 下载到主控 comm tool 缓存，然后设置 `app-can-addr=14`、`node-id=1`，执行 `enter-iap` 和 `upgrade`。目标 IAP 收到完整镜像并校验后才写入 App 首页向量，升级中断会停留在 IAP。

旧串口上位机的数据块帧中，`0xFFFE` 每包最大 1024 字节，`byte_count` 字段可能为 0，真实数据长度使用寄存器数量字段表示。comm tool IAP 按这个旧行为解析，不要求改旧上位机。旧 BMS 上位机还可能用 `0x03` 读取 `0xD000` 或 `0xD050` 状态；comm tool App 和 IAP 均返回兼容状态帧，避免上位机等待状态读时卡住。为了避免旧上位机每包发送后立刻清空接收缓存导致 ACK 被清掉，comm tool App 进入 IAP ACK 和 IAP 写块 ACK 都延迟 20ms 后发送；同时 IAP 对半帧接收增加 500ms 超时重同步，UART TX 等待增加超时，Flash 写入改为按页范围自动擦除。
## 2026-05-25 串口选择补充

COMM TOOL 串口不再在 App 或 IAP 里分散写死。当前统一由 `firmware/comm_tool_f103ret6/source/app/ct_config.h` 的 `CT_COMM_UART_PORT` 控制，默认 `CT_COMM_UART_PORT_USART1`，对应 USART1 重映射 `PB6/TX`、`PB7/RX`，波特率仍为 `115200 8N1`。后续需要切换回 USART3 时，使用 `.\tools\set_comm_tool_uart.ps1 -Port USART3`，不要手工只改 App 或只改 IAP。

## 2026-06-24 USART2/BMS 波特率补充

comm tool 串口1/PC UART 固定 `115200 8N1`，不再由上位机波特率选择框控制。上位机波特率选择框表示 comm tool USART2/BMS 侧波特率，默认 `19200`，通过 `0x15 SET_BMS_UART` 在 App 运行态动态应用；断电重启后恢复 `CT_BMS_UART_BAUD` 默认值。旧固件不支持 `0x15` 时，只能继续使用默认 `19200`。


## 2026-11-16：本项目锂神保留字节扩展E1

App0.2.8起，在原模式2、selector80和快照v3内接收标准六字节以及本项目扩展，不改变PC命令或快照头。
标准字节0/4必须均00；扩展字节4满足(b4&0xC0)==0xC0且低6位不大于0x29，字节0为int8电流1A/bit，0x80无效，正放负充。
SUM仍覆盖前五字节。SOC/总压/温度单位不变，故障低6位复用A1.3.3映射。未知故障值在范围内显示未定义，不生成新保护。
标准00不解释为0A或无故障。旧App0.2.7不支持非零保留位，三端必须配套升级才能显示新增数据。
该扩展不等同第三方锂神标准，不承诺第三方仪表接受非零保留位；BMS宏关闭扩展即可恢复标准。
统一主页面、客户/测试区别和长期记录见[监控说明](../../../bms-tool-windows/docs/OWC_MONITOR.md)。
