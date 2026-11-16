# COMM TOOL 串口选择说明

## 当前配置

COMM TOOL 的 App 和 IAP 串口统一由 `firmware/comm_tool_f103ret6/source/app/ct_config.h` 控制：

```c
#define CT_COMM_UART_PORT              CT_COMM_UART_PORT_USART1
```

当前使用 USART1，硬件引脚为 USART1 重映射后的 `PB6/TX`、`PB7/RX`。

## 覆盖范围

- App 侧 `BoardUart_Init()`、`CtBoard_UartWrite()` 和串口接收中断跟随 `CT_COMM_UART_PORT`。
- IAP 侧串口初始化、轮询接收、发送、跳转 APP 前关闭串口也跟随 `CT_COMM_UART_PORT`。
- 旧的 USART3 配置仍保留为可选项，USART3 使用部分重映射 `PC10/TX`、`PC11/RX`。

## 一键切换

后续需要切换串口时，优先使用脚本：

```powershell
.\tools\set_comm_tool_uart.ps1 -Port USART1
.\tools\set_comm_tool_uart.ps1 -Port USART3
```

脚本只修改 `CT_COMM_UART_PORT` 这一处配置。切换后需要重新编译 COMM TOOL 的 App 和 IAP 工程。
