# D011 产品硬件与固件配置基线

> 适用分支：`feature/sh3673510-d011-bms`。
>
> 产品目标：**HS-D011-10S50A-V1 + TLSR8251F512ET32 + SH3673510 + 10S**。
>
> 本文以当前源码、用户提供的 `hs-d011-10s50a-v1.pdf`、`SH36735XX CV1.0A` 为依据。源码存在不等于实板已验收；没有证据的内容标为 **未确认**。

## 1. 证据优先级

1. D011 原理图：IO 网络、器件连接、装配事实。
2. SH36735XX CV1.0A：SH3673510/3514/3517/3520 系列寄存器、SPI、保护、状态机事实。
3. 当前分支源码：当前实际固件行为。
4. 实板测试记录：用于关闭原理图/BOM/时序歧义。

SH36735XX CV1.0A 明确覆盖 SH3673510/3514/3517/3520；当前仓库以 `sh3673520_*` 命名承载系列共用寄存器/SPI驱动，不代表板上器件变成 SH3673520。

## 2. 产品事实

| 项目 | 当前事实 |
|---|---|
| MCU | TLSR8251F512ET32 |
| AFE | SH3673510 |
| 串数 | 10S；B0..B10 接入 VC0..VC10 |
| Rsense | 原理图 RS1..RS8 均为 2 mΩ；全部装配时 8 并联 = 250 µΩ |
| AFE 通信 | SPI Mode 3，源码目标 500 kHz；SH 手册规定 SCK high/low 均至少 500 ns（上限 1 MHz） |
| 主通信 | Modbus RTU over RS485，`MODBUS_RS485_ENABLE=1` |
| 产品 ID | `FD_BMS_TYPE=D11`，`SeriesNum=10`，硬件版本字符串 `D011` |
| 当前容量默认 | `CapacityFactory=116`（源码已有产品数据，非由原理图推导） |

文件名“50A”不是软件/AFE 过流阈值的授权来源；OC/SCD 参数必须从产品要求和实板波形签核。

## 3. MCU IO：严格按 D011 原理图与源码对照

| MCU GPIO | 原理图网络 | 当前代码符号 | 当前用途/方向 | 结论 |
|---|---|---|---|---|
| PD4 | `CMNT-EN` | `D011_CMNT_EN_PIN` | 通信供电控制输出；连接 C-3V3 支路 | 已对照；完整上电/稳定/关断时序仍需实板验证 |
| PD7 | `SCLK` | `D011_AFE_SCLK_PIN` | SPI SCK 输出 | 已对照 |
| PA0 | `DI1` | `D011_SWITCH_PIN` | 开关检测输入 | 已对照 |
| PA1 | `485-EN` | `D011_RS485_EN_PIN` | RS485 DE 与 /RE 方向控制输出 | 已对照 |
| PA7 | `SWS-A7` | `D011_SWS_PIN` | SWS 下载/调试 | 已对照；保留专用 |
| PB1 | `INT-WK-MCU` | `D011_INT_WK_MCU_PIN` | 外部检测/唤醒输入 | 已对照；电平/去抖需实板验证 |
| PB4 | `HT-CHG` | `D011_HEATER_CHG_PIN` | 加热控制输出 | 已对照 |
| PB5 | `HT-RF-EN` | `D011_HEATER_FUSE_TRIGGER_PIN` | **不可逆加热保险丝触发路径**；安全电平=0 | 已对照；常规运行不得拉高 |
| PB6 | `MISO` | `D011_AFE_MISO_PIN` | SPI MISO 输入 | 已对照 |
| PB7 | `MOSI` | `D011_AFE_MOSI_PIN` | SPI MOSI 输出 | 已对照 |
| PC0 | `ALARM` | `D011_AFE_ALARM_PIN` | AFE ALARM 输入，代码作为唤醒/告警来源 | 已对照 |
| PC1 | `RESET` | `D011_AFE_RESET_OUT_PIN` | 网络接 AFE RESET；**当前宏名带 OUT 不能作为方向证据** | 原理图已对照；当前运行方向必须以 control/port 代码和实板为准 |
| PC2 | `SCI1-TX` | `D011_SCI1_TX_PIN` | UART TX -> 隔离 RS485 DI | 已对照 |
| PC3 | `SCI1-RX` | `D011_SCI1_RX_PIN` | UART RX <- 隔离 RS485 RO | 已对照 |
| PC4 | `DB-LED1` | `D011_DEBUG_LED_PIN` | 调试 LED；原图连接推导为低电平点亮 | 已对照 |
| PD2 | `CS-M` | `D011_AFE_CS_PIN` | SPI CS 输出 | 已对照 |
| PD3 | `CMNT-WK` | `D011_CMNT_WK_PIN` | 隔离通信唤醒输入 | 已对照；当前低功耗闭环仍需验证 |

禁止把 D008 的 PC0/PC1 I2C、PB1 CHG-IN、PD4 MCC-EN-RF、PC4 MCU-LDO 等含义套到 D011。

## 4. AFE SPI 与外部连接

原理图与源码一致的 SPI 组：

- PB6 -> AFE SDO/MISO；
- PB7 -> AFE SDI/MOSI；
- PD7 -> AFE SCK；
- PD2 -> AFE CS；
- PC0 <- AFE ALARM；
- PC1 <-> AFE RESET 网络（方向语义需按实际代码/手册验证）。

系列协议源码使用：write command `0x01`、read `0x02`、reset `0x0B + 0xBB + 0xCC`、ACK `0xA5`、CRC8 poly `0x07`/init `0x00`。这些常量集中在 `sh3673520_reg.h`。

## 5. SH3673510 静态寄存器配置：当前源码实际值

下表解析 `sh3673510_project_config.h`。保护阈值寄存器 0x49..0x54 不是固定产品镜像，它们由独立 AFE HW profile 运行时量化写入。

| 寄存器 | 当前静态值 | 关键字段 |
|---|---:|---|
| SCONF1 0x40 | `0x00` | Normal |
| SCONF2 0x41 | `0x50` | PD_EN=1、PUMP_EN=1；PDSGMOS/DSGMOS/CHGMOS boot=0 |
| SCONF3 0x42 | `0x44` | CGR_WK=1、LD_WK=OFF、CRLD_EN=CPLUS、OWD_EN/TRG=0 |
| SCONF4 0x43 | `0x6A` | PDSGT code3=490 ms，CN=10 |
| SCONF5 0x44 | 初始 `0x3C` | MOS_EN=1、OCC_EN=1、CADC_EN=1、WDT_EN=1、WDT code0 |
| SCONF7 0x46 | `0x04` | RLD=0、CADCT=4S code0、CDV=4 |
| OWV/ALARMH 0x47 | `0x57` | OWV code5、LOADOFF/VADC/CADC interrupt=1，LOADON=0 |
| ALARML 0x48 | `0xFF` | WK/WDT/OWD/TEMP/OCC/OCD/UV/OV interrupt 全开 |

### 5.1 SCONF5/SCONF6 的运行时覆盖

AFE hardware profile 的 `enable_mask` 是最终硬件保护使能来源：

- OCC1 enable 会运行时更新 SCONF5 `OCC_EN`；
- COV -> SCONF6 OV_EN；
- CUV -> SCONF6 UV_EN；
- OCD1/OCD2 任一使能 -> OCD_EN；
- SC -> SC_EN；
- TEMP -> TS1_EN + TS2_EN；
- TS3（heater MOS）和 TS4（power MOS）当前不作为 AFE common temperature hardware protection，仍由软件策略处理。

因此不能只看 `SH3673510_D011_SCONF6_VALUE` 的编译常量判断实际运行使能；必须看 persisted `bms_afe_hw_profile_t.enable_mask` 和 AFE readback。

## 6. SH3673510 硬件保护量化

当前 `sh3673510_control.c` 使用独立 AFE profile：

| 项目 | 当前代码量化 |
|---|---|
| OV/UV | 5 mV/code；delay 从手册离散表选择“向上满足请求”的 code |
| OCD1 | sense threshold=(code+1)×5 mV；delay 使用 140/280/490/980/2030/3010/4970/10010 ms 表 |
| OCD2 | sense threshold=(code+1)×10 mV；delay=(code+1)×25 ms |
| OCC | sense threshold=(code+1)×1.375 mV；delay使用同 8 档表 |
| SC | 阈值为 effective OCD2 × {2,3,4,6}；delay={2,4,8,16,32,64,128,256} µs |
| HW Temperature | 10K NTC 电阻表 -> AFE divider code；充/放高低温分别写 OTC/OTD/UTC/UTD |

电流 effective 值使用当前产品 `Rsense=250 µΩ` 反算。产品 requested 值与硬件可表示 effective 值必须分别记录。

### 6.1 WDT

当前源码 `WDT_EN=1`、WDT code0。CV1.0A 对 code `00` 的标称溢出时间为约 32.34 s（代码名称简写为 32S）。WDT 进入 Powerdown 的后续条件和恢复必须按手册状态机验证，不能仅按“32 s 自动关机”简化描述。

## 7. 温度通道：原理图、用户确认与源码必须分开写

| 通道 | 原理图标注 | 当前代码用途 | 证据状态 |
|---|---|---|---|
| TS1 | 外接/板载温度网络，RN6=10K-3435 | Battery NTC1 | 原图支持 10K |
| TS2 | 外接/板载温度网络，RN5=10K-3435 | Battery NTC2 | 原图支持 10K |
| TS3 | `TS3-NC`，RN3 图纸标 `10M`，并注明靠近加热 MOS | Heater NTC | **原理图值与实际装配不一致**；用户曾明确实际装 10K，源码按10K；仍建议 BOM/实测留证 |
| TS4 | `TS4-MOS`，RN4 图纸标 `10M`，并注明靠近充放电 MOS | MOS NTC | 同上：图纸10M，用户确认实际10K，源码按10K |

本文不把“用户确认的实际10K”改写成“原理图就是10K”。正式生产资料应修订 BOM/原理图，消除这项双重事实。

## 8. 电芯与均衡

- 10S 有效通道为 VC1..VC10；上部 VC11..VC20 在图中并到顶端节点，不能作为独立电芯。
- 软件有效 cell count=10；所有 min/max、压差、保护、SOC、均衡必须只处理有效通道。
- 均衡具体并发规则、热、采样干扰和长期时间限制仍需按 SH 手册与实板测试，不由晶体管数量推断。

## 9. 软件保护与 AFE 硬件保护

- 软件保护：统一 `bms_sw_protection.*`，参数来自 `g_tParam.protect`，First/Second/Third/Recover/Filter。
- AFE 硬件保护：`bms_afe_hw_profile_t`，没有三级概念，单独持久化/修改。
- 普通软件参数写入不得触发 AFE profile 改写。
- AFE profile 写入执行 validate -> persist -> apply -> readback/effective -> verify；失败回滚，rollback 失败进入 `CONFIG_INCONSISTENT`。

## 10. 通信、休眠与仍未闭环项

当前生产通信为 RS485 Modbus RTU。以下项仍不能写成“已完成硬件事实”：

1. C-3V3 / `CMNT-EN` 的完整供电所有权、稳定延时与关闭时序；
2. PD3 `CMNT-WK` 真正通信唤醒链；
3. AFE Sleep/Wake 故障传播与 MCU deep sleep 的竞态；
4. PB5 `HT-RF-EN` 不可逆 fuse 触发条件；当前安全策略是保持 LOW；
5. SC/OCD/OCC 的最终产品阈值和实际栅极波形；
6. TS3/TS4 BOM 文档修订；
7. 均衡、Open-Wire、低功耗、Flash/OTA 实板验收。

## 11. 当前权威源码入口

- `vendor/ble_sample/conf.h`：D011 产品选择、通信模式、产品兼容值。
- `vendor/ble_sample/sh3673510_project_config.h`：D011 IO、串数、Rsense、静态 AFE 配置。
- `vendor/ble_sample/sh3673520_reg.h`：SH36735xx CV1.0A 寄存器/协议真值。
- `vendor/ble_sample/sh3673520*.c`：SPI 驱动。
- `vendor/ble_sample/sh3673510_control.c`：AFE 静态/保护配置、量化、FET/温度控制。
- `vendor/ble_sample/sh3673510_bms.c`：BMS适配、保护恢复、通信 fail-safe。
- `vendor/ble_sample/bms_sw_protection.*`：统一软件三级保护。
- `vendor/ble_sample/bms_afe_hw_profile.*`：独立 AFE 硬件保护 profile。

未出现在这些证据中的参数，不得在 D011 文档中补成“默认值”。