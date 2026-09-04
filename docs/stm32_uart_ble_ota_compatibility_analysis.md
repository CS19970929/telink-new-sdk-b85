# STM32 + 串口蓝牙模块 OTA 兼容性分析

## 1. 结论

可以在当前 Windows 上位机中兼容这类 STM32 + 串口蓝牙模块架构，但不能把它当作 Telink OTA 的另一个传输速度模式。两者在三个层面不同：

1. **传输入口不同**：Telink 使用专用 OTA GATT service/characteristic；STM32 通过蓝牙模块的串口透传，继续使用 BMS 的 Modbus RTU service/characteristic。
2. **固件格式不同**：Telink BIN 使用 Telink 固件头部的 size 字段；STM32 使用链接到 `0x08001C00` 的 APP 原始 BIN，写入前按 1 KB Flash 页补 `0xFF`。
3. **升级状态机不同**：Telink 有 OTA START/DATA/END/OTA_RESULT；STM32 例程使用 `0xFFFD/0xFFFE/0xFFFF` 三个 Modbus 写寄存器地址。

当前例程能实现“通过串口蓝牙发送升级数据”，但不能直接宣称为安全 OTA。例程只校验单帧 Modbus CRC16，不校验整个镜像的长度、顺序、整包摘要、版本和签名；断电或断链后也没有可靠回滚。因此需要先增强 STM32 App/IAP 协议和 IAP 状态机，再把传输适配接入当前上位机。

## 2. 例程真实数据流

### 2.1 BMS App 进入 IAP

App 在 `Code/Source/Sci_Upper.c:1694-1716` 实现 `Sci_WrRegs_0x10_FlashConnect`：

- 只接受 `0x10` 写多个寄存器；
- 只检查写寄存器数量为 `1`，没有检查写入的数据值；
- 将 `0x0800F800` 擦除后写入 `0x00AB`；
- 设置 `u8FlashUpdateE2PROM`；
- 在 Modbus 应答发送完成后才设置复位标志，随后由 `App_FlashUpdateDet` 复位。

地址定义见 `Code/Include/Flash.h:6-17`：

```text
IAP 起始地址       0x08000000
APP 起始地址       0x08001C00
升级标志页         0x0800F800
进入 IAP 标志值     0x00AB
返回 APP 标志值    0xFFFF
```

因此，进入命令的实际 Modbus 请求应是：

```text
01 10 FF FD 00 01 02 [data_hi] [data_lo] CRC_LO CRC_HI
```

例程当前不关心 `[data_hi] [data_lo]`，但上位机应发送规范的 1 个寄存器，不能省略字节数和 CRC。

### 2.2 IAP 接收数据

IAP 在 `Include/SCI.h:31-34` 定义：

```text
0xFFFD  FLASH_CONNECT     进入/确认升级状态
0xFFFE  FLASH_UPGRATE     写入一个升级数据帧
0xFFFF  FLASH_COMPLETE    升级完成
```

`Source/Sci.c:225-277` 的实际行为如下：

- `0xFFFD`：写寄存器数量为 1 时直接正应答，不执行额外校验；
- `0xFFFE`：只要写寄存器数量 `<= 1033`，就执行一次 APP 页擦除和写入；
- 每次固定从 `s->u16Buffer[7]` 取 **1024 字节**写入 `0x08001C00 + 1024 * u8FlashReceiveCnt`；
- 写完后 `u8FlashReceiveCnt++`，没有帧序号、重传和重复帧检测；
- `0xFFFF`：只检查写寄存器数量为 1，直接把升级标志写成 `0xFFFF`，然后复位进入 APP。

所以当前例程真正期望的数据帧是标准 Modbus `0x10` 帧：

```text
01 10 FF FE 02 00 04 00
<1024 bytes APP data, last page padded by FF>
CRC_LO CRC_HI
```

其中 `0x0200` 是 512 个寄存器，`0x0400` 是 1024 个数据字节。完整帧长度为 1033 字节。APP 区从 `0x08001C00` 到升级标志页前共有 55 KB，因此最多需要 55 个 1 KB 数据帧。

### 2.3 IAP 完成和跳转

`Source/main.c:44-64` 上电后读取升级标志：

- 标志为 `0xFFFF`：直接检查 APP 初始栈地址，然后跳转 APP；
- 其他值：初始化串口并停留在 IAP 接收循环。

`Source/main.c:177-192` 只按 APP 起始地址的初始栈指针是否落在 SRAM 区间判断 APP 是否存在，然后跳转复位向量。

`Source/main.c:232-240` 在完成帧应答发送后复位。例程不会校验 APP 总长度、最后一帧有效长度、整包 CRC32，也没有签名验证。

## 3. 当前上位机为什么不能升级 STM32 板

当前 `BmsTool.Windows/Ota.cs` 中：

- `FirmwareImage.Load` 在文件偏移 `0x18` 读取 Telink 专用 size 字段；STM32 原始 BIN 不具备该字段，加载阶段就会失败；
- `OtaBleTransport` 只寻找 Telink 专用 UUID：
  `00010203-0405-0607-0809-0a0b0c0d1912` 和对应 OTA characteristic；
- `TelinkOtaClient` 发送 Telink 专用 `START/DATA/END` 帧，不是 Modbus `0x10`；
- 当前 `BmsBleTransport` 虽然已经连接串口蓝牙模块使用的 Nordic UART 风格 service：
  `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`，但其普通 `WriteAsync` 不是面向 1033 字节升级帧的分片传输器。

因此，当前 OTA 失败不是 STM32 IAP 的 UUID 小差异，而是 OTA 协议、固件格式和传输器全部不同。

## 4. 兼容上位机的建议架构

### 4.1 OTA 后端分层

保留现有 Telink 后端，新增 STM32 后端，不修改普通 BMS Modbus 逻辑：

```text
OTA 页面
  └─ OtaTargetDetector
       ├─ TelinkOtaClient
       │    └─ Telink 专用 OTA GATT
       └─ Stm32SerialBleOtaClient
            └─ BmsBleTransport / Nordic UART
                 └─ Modbus RTU 0x10 + STM32 IAP 扩展协议
```

自动选择应依据真实 GATT service/characteristic 和设备能力探测，不应只依据蓝牙名称。页面上应显示“Telink OTA”或“STM32 串口 IAP OTA”，并禁止用错误类型的固件启动升级。

### 4.2 兼容现有例程的临时模式

如果只是为了验证链路，可以先增加一个“STM32 兼容模式”：

1. 读取并校验 STM32 APP BIN，最大 55 KB；
2. 关闭实时轮询，使用现有 Nordic UART 通道；
3. 发送 `0xFFFD` 进入 IAP，等待标准 `0x10` 应答；
4. 将 APP BIN 按 1024 字节补 `0xFF`，每页构造 `0xFFFE` 写多个寄存器帧；
5. 每帧等待 8 字节 `0x10` 正应答后再发送下一页；
6. 发送 `0xFFFF` 完成帧，等待应答；
7. 断开后轮询重连，读取 APP 版本和实时数据。

这只能保证上位机不会把 Telink 帧发给串口 IAP，并能降低串口蓝牙分片导致的失败概率；它不能弥补固件端没有整包校验和回滚的问题。

### 4.3 正式安全模式

正式实现应在 STM32 App/IAP 共同升级协议中增加：

- 明确的升级会话 ID；
- 目标镜像长度、目标版本、目标设备/产品类型；
- 16 位或 32 位数据帧序号；
- 每页写入后读回或校验确认；
- 允许按序重传，重复帧必须幂等，不得因为重传而写入错误页；
- 整包 CRC32 或 SHA-256；
- APP 起始栈、复位向量、地址范围、长度和版本合法性检查；
- 只有全部检查通过后才将镜像标记为可启动；
- IAP 超时不应把设备静默留在不可恢复状态；至少要保持可重新升级；
- 生产安全要求下增加镜像签名验证和版本回滚保护。

推荐把镜像元数据放在 IAP 可识别的升级会话中，而不是复用普通客户寄存器。数据帧可以继续使用 `0xFFFE`，但应在其数据区加入会话 ID、序号和有效长度；完成帧 `0xFFFF` 应携带镜像长度、整包摘要和版本，并由 IAP 验证通过后才清除升级标志。

## 5. 例程中的安全和可靠性问题

| 项目 | 当前例程 | 风险 |
|---|---|---|
| 认证 | 无 | 任意能连上的客户端可触发 IAP |
| 单帧校验 | Modbus CRC16 | 只能发现传输错误，不能证明镜像来源 |
| 全包校验 | 无 | 错页、漏页或错误 BIN 也可能被标记完成 |
| 帧顺序 | RAM 计数器 `u8FlashReceiveCnt` | 无法安全重传，丢帧后后续页错位 |
| 长度检查 | `<=1033`，实际固定写 1024 字节 | 短帧可能使用残留缓冲区数据 |
| Flash 写入 | `FlashWrite` 忽略半字写入结果 | Flash 错误可能继续完成升级 |
| 断电恢复 | 无双备份 | 写到一半断电后旧 APP 可能已损坏，只能依赖 IAP 重新刷完整包 |
| 完成条件 | 只收到 `0xFFFF` 就清标志 | 没有证明镜像已经完整、正确 |
| 启动检查 | 只检查初始栈指针 | 未检查复位向量、镜像边界和完整性 |
| 超时 | 只清 RAM 接收计数 | 不会恢复旧 APP，也没有失败状态报告 |

另外，IAP 的 SCI2 接收完成路径 `Source/Sci.c:736-744` 使用了 `USART1->CR1` 而不是 `USART2->CR1`。如果串口蓝牙模块接在 USART2，这会导致接收/中断状态处理异常；正式适配前必须按实际接线修正并分别验证 SCI1、SCI2。

特别需要注意：在 STM32F030C8T6 的 64 KB Flash 中，当前布局没有空间保存一份完整的 55 KB 旧 APP 副本。因此在不改变 Flash 布局或增加外部存储的前提下，不能承诺真正的断电回滚；可以实现的是“安全验证后激活 + IAP 可重新恢复”。

## 6. 已实现的 Windows 兼容层

当前仓库已在客户版和内部完整测试版同时接入以下功能：

- OTA 架构自动识别：Telink 专用 OTA service 优先；否则识别 BMS Nordic UART service；
- Telink 和 STM32 固件格式分开加载，阻止错误架构的 BIN 进入升级流程；
- STM32 APP BIN 最大 55 KB，按 1 KB 页补 `0xFF`；
- 通过现有 BMS GATT 通道按 MTU 分片发送 1033 字节 Modbus IAP 帧；
- `0xFFFD`、`0xFFFE`、`0xFFFF` 逐步执行，每页等待并校验 `0x10` ACK；
- 响应拆包、CRC16 校验、硬超时和取消处理；旧例程没有序号，因此 ACK 超时直接终止，不自动重传造成页计数错位；
- 完成后沿用现有重连、版本和实时数据验证流程。

## 7. 后续固件安全改造顺序

### 阶段 A：上位机兼容现有例程（已完成）

- 新增 STM32 原始 BIN 加载器和 55 KB 边界检查；
- 新增 GATT 分片发送和 Modbus 响应拼包；
- 新增 `0xFFFD/0xFFFE/0xFFFF` 兼容流程；
- 在每个 1 KB 页之后等待 ACK；
- 加入取消、超时、重连和明显的“例程模式不具备整包安全校验”提示；
- 用离线协议单元测试验证 CRC、帧长度、补 FF、页地址和 55 KB 上限。

### 阶段 B：先改 IAP，再启用正式升级

- 固定最大数据长度为 1024 字节并严格验证 byte count；
- 增加序号、会话和有效长度；
- 检查并返回 Flash 擦除/编程/读回错误；
- 在完成阶段验证整包长度和 CRC32/SHA-256；
- 增加有效 APP 标志和更严格的向量表检查；
- 明确断链、超时、复位后的可恢复行为。

### 阶段 C：增加生产安全

- 镜像签名验证，公钥固化在 IAP；
- 设备型号/硬件版本匹配；
- 最低版本或单调版本计数，防止回滚；
- 对升级入口增加会话认证/授权；
- 记录升级结果和失败原因。

## 8. 当前判断

**兼容性：已在上位机实现。** 当前上位机已经有串口蓝牙模块的 Nordic UART 连接、通知和 Modbus 基础，新增的 STM32 OTA 后端复用了这些代码；Telink OTA 后端保持不变。

**仅靠上位机：不能保证升级安全。** 上位机可以保证固件类型选择、帧 CRC、发送顺序、每帧 ACK 和重连验证，但无法在 IAP 不支持的情况下凭空获得整包完整性、真实性和断电原子性。

**下一步：** 用一份明确链接到 `0x08001C00` 的 STM32 APP BIN 做真机链路验证；随后修改 App/IAP 为带会话、序号、镜像长度、整包摘要、严格 Flash 错误处理和签名认证的正式协议。当前仓库的上位机自动目标识别和双版本发布流程已经具备。
