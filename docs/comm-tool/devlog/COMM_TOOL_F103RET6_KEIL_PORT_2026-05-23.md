# COMM TOOL F103RET6 Keil 工程说明

## 工程入口

- App Keil 工程：`firmware/comm_tool_f103ret6/keil/COMM_TOOL_F103RET6.uvprojx`
  - Target：`COMM_TOOL_Release`，固定链接到 `0x08008000`
- IAP Keil 工程：`firmware/comm_tool_f103ret6/keil/COMM_TOOL_IAP.uvprojx`
  - Target：`COMM_TOOL_IAP`，固定烧录到 `0x08000000`
- MCU：`STM32F103RET6`，Keil 设备选择 `STM32F103RE`
- IAP 区：`0x08000000..0x08007FFF`，32KB
- COMM TOOL App 区：`0x08008000..0x08017FFF`，64KB
- BMS/COMM TOOL 待升级固件缓存区：`0x08018000` 起
- 缓存元数据页：`0x0807F800`
- SRAM mailbox：`0x2000FFE0`，App 复位进入 IAP 使用；Keil RAM 上限保留 32 字节，不给栈和全局变量使用

IAP 独立保留 32KB，App IROM 限制为 64KB，目的是让 COMM TOOL 自身程序不能覆盖从 `0x08018000` 开始的固件缓存区。升级中 IAP 会先擦 App 首页，但把第一页内容暂存在 RAM，完成校验后最后写入 MSP/Reset 向量；如果中途断电，向量表保持无效，重启后继续停在 IAP。

App 和 IAP 使用两个独立 Keil 工程，而不是同一个工程里的两个 Target。这样源码集合、链接地址和下载产物完全隔离，避免 uVision 的 Target/Layer 本地缓存把 App 与 IAP 文件混编。两个工程仍放在同一个 `keil/` 目录，源码和公共配置共用，便于维护。

## 串口选择

COMM TOOL 的 PC 串口由 `firmware/comm_tool_f103ret6/source/app/ct_config.h` 的 `CT_COMM_UART_PORT` 统一控制，App 和 IAP 必须保持同一配置。

当前默认配置：

```c
#define CT_COMM_UART_PORT              CT_COMM_UART_PORT_USART1
```

当前使用 USART1，采用 USART1 重映射引脚：

| 功能 | 引脚 | 配置 |
| --- | --- | --- |
| PC 串口 TX | `PB6` | `USART1_TX`，`GPIO_Remap_USART1` |
| PC 串口 RX | `PB7` | `USART1_RX`，默认 `115200 8N1` |

保留的备选配置是 USART3：

| 功能 | 引脚 | 配置 |
| --- | --- | --- |
| PC 串口 TX | `PC10` | `USART3_TX`，`GPIO_PartialRemap_USART3` |
| PC 串口 RX | `PC11` | `USART3_RX`，默认 `115200 8N1` |

后续切换串口优先使用脚本：

```powershell
.\tools\set_comm_tool_uart.ps1 -Port USART1
.\tools\set_comm_tool_uart.ps1 -Port USART3
```

脚本只改 `CT_COMM_UART_PORT` 这一处。切换后需要重新编译 COMM TOOL 的 App 和 IAP。

## 板级引脚

参考 `E:\TODO\code\c058 from c030`，只移植串口、CAN、供电和 debug LED：

| 功能 | 引脚 | 配置 |
| --- | --- | --- |
| CAN RX | `PA11` | `CAN1_RX` |
| CAN TX | `PA12` | `CAN1_TX` |
| CAN/通信驱动 | `PC12` | 正常运行置 0，对齐 `MCUO_DRV_CMNT=0` |
| PWSV 控制 | `PC13` | 正常运行置 0，对齐 `MCUO_PWSV_CTR=0` |
| PWSV 待机 | `PD2` | 正常运行置 1，对齐 `MCUO_PWSV_STB=1` |
| Debug LED | `PB15` | App 200ms 翻转，IAP 500ms 翻转 |

COMM TOOL 当前复用主仓库 `system_stm32f10x.c`，运行时 HSE 直连，`PCLK1=8 MHz`。CAN 位时序必须按该时钟计算：

| CAN 波特率 | BS1 | BS2 | Prescaler |
| --- | --- | --- | --- |
| `125 kbit/s` | `5tq` | `2tq` | `8` |
| `250 kbit/s` | `5tq` | `2tq` | `4` |
| `500 kbit/s` | `5tq` | `2tq` | `2` |

默认使用 `250 kbit/s`，和 BMS IAP、COMM TOOL IAP 的 CAN1 位时序一致。不要按 36MHz PCLK1 计算 COMM TOOL CAN 预分频，否则工具界面显示 250k，但实际总线速率会错误。

## 看门狗

COMM TOOL App 和 IAP 默认启用 IWDG。公共实现为 `source/bsp/ct_watchdog.c/h`，配置在 `source/app/ct_config.h`：

- `CT_WATCHDOG_ENABLE=1` 默认启用。
- `CT_WATCHDOG_RELOAD_VALUE=1875`，IWDG prescaler 256，按 40 kHz LSI 典型值约 12 秒。
- App 在 `Board_Init()` 后启动 IWDG，主循环喂狗；Flash 缓存擦写循环内必须喂狗。
- IAP 只在确认停留 IAP 后启动 IWDG，正常直接跳 App 前不启动；Flash 擦写、串口响应延迟、CAN 发送等待和 IAP 主循环内必须喂狗。
- 后续改 Flash、串口升级或 CAN-IAP 阻塞等待时，不能删除这些喂狗点；也不能在无界异常死循环中喂狗。

## 源码范围

- `source/app/`：COMM TOOL 协议、Flash 缓存、CAN-IAP 业务逻辑。
- `source/iap/`：COMM TOOL 自身 IAP，支持旧 BMS 串口 IAP 协议和 CAN-IAP 协议。
- `source/bsp/`：RET6 板级适配。
- 标准库复用主仓库已有 `103 + 309/Project/STM32F10x_StdPeriph_Lib_V3.5.0`，没有拷贝无关业务代码。

## 构建命令

```powershell
& 'C:\Keil_v5\UV4\UV4.exe' -b "firmware\comm_tool_f103ret6\keil\COMM_TOOL_IAP.uvprojx" -t "COMM_TOOL_IAP" -o "firmware\comm_tool_f103ret6\keil\build_logs\COMM_TOOL_IAP.log"
& 'C:\Keil_v5\UV4\UV4.exe' -b "firmware\comm_tool_f103ret6\keil\COMM_TOOL_F103RET6.uvprojx" -t "COMM_TOOL_Release" -o "firmware\comm_tool_f103ret6\keil\build_logs\COMM_TOOL_Release.log"
```

构建产物：

- `firmware/comm_tool_f103ret6/keil/Objects/IAP/COMM_TOOL_IAP.axf`
- `firmware/comm_tool_f103ret6/keil/Objects/IAP/COMM_TOOL_IAP.hex`
- `firmware/comm_tool_f103ret6/keil/Objects/IAP/COMM_TOOL_IAP.bin`
- `firmware/comm_tool_f103ret6/keil/Objects/Release/COMM_TOOL_Release.axf`
- `firmware/comm_tool_f103ret6/keil/Objects/Release/COMM_TOOL_Release.hex`
- `firmware/comm_tool_f103ret6/keil/Objects/Release/COMM_TOOL_Release.bin`

首次生产烧录必须同时烧 `COMM_TOOL_IAP.hex` 和 `COMM_TOOL_Release.hex`，或先烧 IAP 再通过 IAP 升级 App。禁止把 `COMM_TOOL_Release.bin` 裸写到 `0x08000000`，否则会覆盖 COMM TOOL IAP。

## PC 联调

```powershell
.\tools\start_comm_tool_host.ps1 -Mode info -Port COM4
.\tools\start_comm_tool_host.ps1 -Mode fw-download -Port COM4 -Bin "103 + 309\Project\Users\Objects\FD_Release.bin" -ConfirmAppAddress 0x08004800
.\tools\start_comm_tool_host.ps1 -Mode upgrade -Port COM4 -ConfirmUpgrade
```

## COMM TOOL 自升级

COMM TOOL 自身升级有两个入口：

| 入口 | 使用场景 | 协议 |
| --- | --- | --- |
| PC 直连当前 COMM TOOL 串口 | 复用旧 BMS 串口升级上位机 | 当前 `CT_COMM_UART_PORT` 对应串口，默认 USART1 `115200 8N1`，`0xFFFD/0xFFFE/0xFFFF` 旧 IAP 命令 |
| 另一只 COMM TOOL 通过 CAN 升级当前 COMM TOOL | 批量升级 COMM TOOL 或现场无线隔离升级 | 当前 COMM TOOL App 响应 BMS App `ENTER_IAP` 服务，进入 IAP 后复用 CAN-IAP 协议 |

串口自升级保持旧 BMS 上位机协议不变，IAP 固件做了以下容错：

- 旧协议 ACK 延迟 20ms 后发送，避开旧上位机每包 `Write()` 后立刻 `DiscardInBuffer()` 造成的 ACK 丢失。
- 串口收半帧超过 500ms 自动丢弃并重新找帧头，避免升级中断后卡在旧半帧状态。
- IAP 写 App 区时按实际覆盖页擦除并记录已擦页，不再依赖每包都从 Flash 页边界开始，最后一包不足 1024 字节也能稳定写入。
- UART 发送等待增加超时保护，串口状态异常时不会在 TXE/TC 等待里死循环。

通过另一只 COMM TOOL 升级当前 COMM TOOL 的步骤：

```powershell
.\tools\start_comm_tool_host.ps1 -Mode fw-download -Port COM4 -Bin "firmware\comm_tool_f103ret6\keil\Objects\Release\COMM_TOOL_Release.bin" -AppAddress 0x08008000 -ConfirmAppAddress 0x08008000
py -3.9 tools\comm_tool_host.py set-can --port COM4 --app-can-addr 14 --node-id 1
py -3.9 tools\comm_tool_host.py enter-iap --port COM4 --confirm-enter-iap
py -3.9 tools\comm_tool_host.py upgrade --port COM4 --confirm-upgrade --long-timeout 120
```

COMM TOOL App/IAP 的周期诊断心跳帧已停用，不再主动发送 `0x05E` 或 `0x05F`。排查升级失败时改用 `CAN_DIAG`、`DEBUG_LOG` 和 CAN-IAP ACK/NACK，仍需确认总线上只有一个 IAP 节点为 `1` 的目标设备在线。
