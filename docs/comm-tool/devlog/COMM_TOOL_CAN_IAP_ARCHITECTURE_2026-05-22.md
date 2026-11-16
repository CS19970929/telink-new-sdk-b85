# comm tool CAN-IAP 系统实施方案

## 目标

本方案实现一条固定升级与维护链路：

`PC 上位机 <UART> comm tool(STM32F103RET6) <CAN> BMS`

PC 不直接连接 BMS CAN。comm tool 负责串口协议解析、BMS CAN 读写转发、BMS App 固件缓存，以及对 BMS IAP 发起一键 CAN 升级。

## 工程范围

1. PC 工具：`tools/comm_tool_host.py` 与 `tools/start_comm_tool_host.ps1`。
2. comm tool 固件：`firmware/comm_tool_f103ret6/source/app/`。
3. BMS App：增加 CAN 服务命令，先支持状态读取和进入 IAP，后续扩展为寄存器读写。
4. BMS IAP：在 `E:\work\a002\new 030\IAP 103CB` 单独分支重构为 CAN-IAP。

## 地址与分区

第一阶段保留当前 BMS App 地址，降低迁移风险：

| 区域 | 地址 | 说明 |
| --- | --- | --- |
| BMS IAP | `0x08000000` | Bootloader 起始地址 |
| BMS App | `0x08004800` | 当前 `FD_Release` 链接地址 |
| BMS App 写入上限 | `0x0801F800` | 预留升级标志页和睡眠标志页 |
| BMS 升级标志 | `0x0801F800` | App 请求进入 IAP |
| comm tool IAP | `0x08000000..0x08007FFF` | comm tool 自身 Bootloader，支持旧串口 IAP 和 CAN-IAP |
| comm tool App | `0x08008000..0x08017FFF` | comm tool 自身程序 |
| comm tool 固件缓存 | `0x08018000` 起 | F103RET6 后半段 Flash，用于缓存 BMS App 或 comm tool App bin |

如果重构后的 IAP 超过 `0x4800` 字节，必须整体切换到新 App 地址，例如 `0x08008000`，并同步更新 scatter、烧录脚本、检查脚本和本文档。

## 失败恢复原则

- BMS IAP 只能擦写 App 区，禁止擦写 IAP 区。
- App 有效标志只能在整包 CRC、向量表、ResetHandler 范围全部通过后写入。
- 升级中断、CRC 错误、CAN 超时、Flash 写失败后，BMS 重启必须停留在 IAP。
- comm tool 缓存完整固件后再启动 BMS 升级；BMS 升级失败时可以不依赖 PC 重新一键刷入。
- 所有串口/CAN/Flash 等待都必须有超时，禁止死等。
- 多设备总线中必须先通过 BMS App CAN 地址锁定目标板，再让目标板进入 IAP；禁止多个相同 IAP 节点同时处于 IAP 后再发起升级。
- comm tool 自升级时，App 镜像地址必须是 `0x08008000`，IAP 与 App 使用独立 Keil 工程，禁止把 App bin 裸写到 `0x08000000`。

## 实施阶段

1. 固化协议文档和 PC 串口工具。
2. 补齐 comm tool 固件源码骨架：串口协议、Flash 缓存、CAN 网关、升级状态机。
3. BMS App 增加 CAN 命令入口：状态读取、进入 IAP。
4. IAP 工程独立分支重构：CAN 协议、Flash 写入、校验、跳转和失败恢复。
5. 扩展 BMS CAN 寄存器读写，复用现有 RS485 地址语义。
6. 联调断电、丢帧、错误 CRC、BusOff 和重复升级场景。

## 验证入口

PC 侧 dry-run：

```powershell
.\tools\start_comm_tool_host.ps1 -Mode fw-dry-run -Bin "103 + 309\Project\Users\Objects\FD_Release.bin"
```

下载固件到 comm tool：

```powershell
.\tools\start_comm_tool_host.ps1 -Mode fw-download -Port COM4 -Bin "103 + 309\Project\Users\Objects\FD_Release.bin" -ConfirmAppAddress 0x08004800
```

设置目标 BMS 地址和 IAP 节点：

```powershell
.\tools\start_comm_tool_host.ps1 -Mode set-can -Port COM4 -CanBitrate 250000 -AppCanAddr 0 -NodeId 1
```

一键升级 BMS：

```powershell
.\tools\start_comm_tool_host.ps1 -Mode upgrade -Port COM4 -ConfirmUpgrade
```
