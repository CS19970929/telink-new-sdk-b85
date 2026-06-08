# SH3673520 20 串蓝牙保护板重构说明

## 目标范围

本分支以 `ble_sample` 为基础，新增 SH3673520 20 串 AFE 支持，并把 AFE 访问、参数存储、Modbus 参数窗口和上位机连接方式拆分成可继续扩展的结构。

本次重构覆盖：

- 固件默认项目切换为 `SH3673520_20S`，串数为 20 串。
- 新增 `bms_afe` 抽象层，保留 SH367309 旧路径，新项目走 SH3673520 SPI 路径。
- 新增 SH3673520 SPI Mode3 驱动、采样解析和参数镜像。
- AFE 参数通过内部 Flash KV 保存，不再依赖外置 EEPROM。
- Modbus 保持原有保护参数窗口 `0x2100`，新增 SH3673520 AFE 参数窗口 `0x2200`。
- 上位机新增 BLE / 串口两种连接方式，同一套 Modbus RTU 命令可通过任一链路收发。

## 固件结构

主要文件：

- `vendor/ble_sample/conf.h`：新增 `SH3673520_20S` 项目类型，定义 20 串、容量、软硬件版本和 `BMS_AFE_TYPE_SH3673520`。
- `vendor/ble_sample/bms_afe.h/.c`：AFE 抽象层，应用层只调用 `bms_afe_*` 接口。
- `vendor/ble_sample/sh3673520_afe.h/.c`：SH3673520 SPI 通信、参数镜像、采样解析。
- `vendor/ble_sample/bms_cold_kv_store.h/.c`：新增 AFE 参数 KV key 区。
- `vendor/ble_sample/modbus_rtu.c`：新增 `0x2200` AFE 参数读写窗口。
- `project/tlsr_tc32/B85/825x_ble_sample/vendor/ble_sample/subdir.mk`：加入新源文件。

应用初始化流程：

1. `app.c` 调用 `bms_afe_bus_init()` 初始化 AFE 总线。
2. `bms_afe_reset()` 复位 AFE。
3. `bms_afe_apply_params()` 将 Flash 中保存的 AFE 参数镜像写入 SH3673520。
4. 周期采样调用 `bms_afe_sample()`，新项目内部读取 SH3673520 RAM 并刷新 `g_stCellInfoReport`。

## SH3673520 SPI

参考文档：`SH36735XX CV0.2B.PDF`。

当前配置：

- SPI 模式：Mode3，CPOL=1，CPHA=1。
- 速率：500 kHz，低于规格书 1 MHz 上限。
- CS：`GPIO_PD6`。
- SPI 管脚组：`SPI_GPIO_GROUP_A2A3A4D6`。
- RESET：沿用 `AFE_CTL_PIN`。
- CRC8：多项式 `0x07`，初值 `0x00`。

帧格式：

- 写寄存器：`0x01 + Reg + Data + CRC8 + dummy`，最后一个 SDO 字节期望 `0xA5`。
- 读寄存器：`0x02 + Reg + Length + dummy...`，SDO 校验 `0xFF + 0x02 + Reg + Length + Data... + CRC8`。
- 软件复位：`0x0B + 0xBB + 0xCC + CRC8 + dummy`。

采样读取范围为 `0x58~0x96`，当前解析：

- `0x58~0x5A`：FLAG1/2/3。
- `0x5B~0x5C`：BSTATUS1/2。
- `0x5D~0x64`：TEMP1~4。
- `0x67~0x68`：CUR。
- `0x69~0x90`：CELL1~CELL20。
- `0x95~0x96`：C+。

换算：

- 单体电压：`mV = CELL * 5 / 32`。
- 外部温度：`RT(kohm) = TEMP / (32768 - TEMP) * 10`，再复用现有 10K NTC 表换算为旧工程温度格式。
- 电流：`mA = 100 * CUR / (29127 * RSENSE)`，`CUR.15=1` 表示放电，`CUR.15=0` 表示充电。

## AFE 参数窗口

SH3673520 参数通过 Modbus holding register 暴露在 `0x2200~0x221E`，共 31 个 word。

| 地址 | 偏移 | 含义 | 读写 |
| --- | ---: | --- | --- |
| `0x2200` | 0 | 固定型号字 `0x3520` | 只读 |
| `0x2201` | 1 | 串数，当前固定 20 | 只读 |
| `0x2202` | 2 | 采样电阻，单位 uohm | 读写 |
| `0x2203` | 3 | 最近一次 BSTATUS1/2 | 只读 |
| `0x2204` | 4 | 最近一次 FLAG1 | 只读 |
| `0x2205` | 5 | 最近一次 FLAG2 | 只读 |
| `0x2206` | 6 | 最近一次 FLAG3 | 只读 |
| `0x2207` | 7 | 预留 | 读写 |
| `0x2208~0x221E` | 8~30 | SH3673520 `0x41~0x57` 配置寄存器镜像，低 8 位有效 | 读写 |

写 `0x2200` 窗口时：

1. 单寄存器写 `0x06` 或多寄存器写 `0x10` 都会先更新 RAM 镜像。
2. 写入成功后调用 `bms_afe_param_commit_and_apply()`。
3. 参数保存到内部 Flash KV。
4. 配置寄存器镜像重新写入 SH3673520。

## 内部 Flash 存储

`bms_cold_kv_store` 新增 AFE 参数 key 区：

- key 起始：`0x5000`。
- 数量：31。
- 用途：保存 `0x2200` 参数窗口的 word 镜像。

首次启动如果没有发现 `0x3520` 型号字，会写入默认镜像。默认镜像会固定型号、串数、采样电阻，并设置 20 串相关配置位。

## 上位机

路径：`vendor/ble_sample/BMSAssistantQt`。

新增能力：

- 左侧连接区域增加连接方式选择：`BLE` / `串口`。
- 串口模式可刷新串口、选择端口和波特率，默认 115200 8N1。
- BLE 和串口共用同一套 Modbus RTU 编解码。
- 调试页新增 `SH3673520 AFE 参数` 区，可读 `0x2200` 窗口，也可按偏移写入多个 word。
- 设置会保存连接方式、串口、波特率和 AFE 参数输入。

验证方式：

```powershell
python -m compileall -q "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/BMSAssistantQt/bmsassistantqt"
```

## 已知待联调项

- SH3673520 的 `CS=GPIO_PD6`、`RESET=AFE_CTL_PIN`、SPI 管脚组需要按最终原理图确认。
- `0x41~0x57` 配置镜像目前提供可读写框架和基础默认值，保护阈值位域仍需结合 SH3673520 规格书逐项标定。
- 旧实时区 `u16VCellTotle` 是 16 位，20 串满电总压可能超过 65535 mV。当前写旧字段时做了限幅，后续建议扩展 32 位总压寄存器供上位机使用。
- SH3673520 读写帧已按资料实现，但仍需要用逻辑分析仪确认 ACK、CRC 和数据相位。
- 本机未安装 `tc32-elf-gcc` 时无法完成固件全量构建，需要在 Telink TC32 工具链环境验证最终 ELF/BIN。
