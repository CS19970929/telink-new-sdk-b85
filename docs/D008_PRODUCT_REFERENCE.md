# D008 产品硬件与固件配置基线

> 适用分支：`feature/sh3673510-d013-bmsdvc`（历史名称；实际产品为 **HS-D008 + TLSR8251F512ET32 + DVC1124-2**）。
>
> 本文是 D008 当前唯一产品级配置说明。结论优先来自当前源码、用户提供的 `HS-D008-24S100A-V1(2).pdf`、DVC1124-2 Reference Manual V1.2。没有证据的内容明确标为 **未确认**，不得由常见 BMS 经验补齐。

## 1. 证据优先级

1. 用户提供的 HS-D008 原理图/BOM：板级网络、器件连接、装配事实。
2. DVC1124-2 Reference Manual V1.2：AFE 寄存器、bit、时序、量化和状态语义。
3. 当前分支源码：实际固件配置与运行行为。
4. DVC DemoCode：仅作为辅助实现/时序参考，不能覆盖 V1.2 手册。

若原理图、手册和源码不一致，本文分别记录“硬件事实”和“当前代码事实”，不自行选一个当成已验证量产结论。

## 2. 当前产品身份

| 项目 | 当前事实 |
|---|---|
| MCU | TLSR8251F512ET32 |
| AFE | DVC1124-2，源码默认 `DVC1124_MODEL_22` |
| 默认装配 | 24S LFP |
| 可选装配 | 20S NMC；由 `D008_PRODUCT_PROFILE` 编译选择 |
| AFE 总线 | I2C，PC0=SDA、PC1=SCL，100 kHz |
| DVC transfer write address | `0x40`；读地址由驱动使用 `0x41` |
| 分流器 | 原理图 RS1..RS10 为 10 × 2 mΩ 并联；全部装配时等效约 200 µΩ |
| 板载 NTC | NTC1/NTC2 型号标注 `SNC103B13435F0603E` |

### 2.1 必须保留的源码事实/技术债

`conf.h` 目前仍先选择历史 `FD_BMS_TYPE D3PRO`，因此容量和部分历史产品默认值仍来自 D3PRO 分支；随后 D008 只覆盖 `SeriesNum=DVC1124_DEFAULT_CELL_COUNT` 和硬件版本字符串为 `D008`。因此：

- `CapacityFactory`、历史 `AFE_ODC1/AFE_ODC2` 等 **不能作为 D008 已签核产品参数**；
- 24S/20S Product Profile 只解决物理串数、化学体系/SOC profile 身份，不自动证明容量、OV/UV、OC、温度阈值正确；
- 后续产品参数签核应消除 `FD_BMS_TYPE D3PRO` 对 D008 的残留依赖。

## 3. MCU IO：原理图与源码对照

下表只把能在 D008 原理图和当前源码中对应起来的网络列为“已对照”。方向为当前源码可证明的方向；未检查到明确 GPIO 初始化的项不推断方向。

| GPIO | 原理图网络 | 当前代码符号/用途 | 当前代码方向/电平 | 结论 |
|---|---|---|---|---|
| PD4 | `MCC-EN-RF` | `RF_EN_PIN` | `board_init()` 配置 GPIO 输出，启动写 0 | 已对照；网络具体受控电路功能不从名字扩展推断 |
| PD7 | `MCU-AFE-EN` | `AFE1_PRO_EN_PIN`；`DVC1124_AFE_Reset()` 直接使用 PD7 | 输出；AFE reset 路径拉高，源码注释为 active high | 已对照 |
| PA0 | `ACC-MCU` | `SW_PIN` / key | 输入；`IsKeyWakeupActive()` 低有效 | 已对照 |
| PA1 | `MCC-EN-HT` | `HEATER_EN_PIN` | 源码有板级符号；本文未把未检查到的初始化方式写成事实 | 网络已对照，运行方向需实板/调用链复核 |
| PA7 | `SWS-A7` | Telink SWS | 调试/下载专用 | 已对照；禁止普通业务复用 |
| PB1 | `CHG-IN` | `CHG_IN_PIN` | 输入，1 MΩ pull-up；低有效；同时作为低电平深睡唤醒源 | 已对照 |
| PB4 | `SOC25` | `SOC25_PIN` | 当前 `conf.h` 定义 | 原理图/代码网络已对照；具体 LED 驱动极性需按实际调用确认 |
| PB5 | `SOC50` | `SOC50_PIN` | 当前 `conf.h` 定义 | 同上 |
| PB6 | `LED_BLUE` | `LED_BLUE_PIN` | 当前 `conf.h` 定义 | 同上 |
| PB7 | `SOC75` | `SOC75_PIN` | 当前 `conf.h` 定义 | 同上 |
| PC0 | `SDA` | DVC I2C SDA | `i2c_gpio_set(I2C_GPIO_GROUP_C0C1)` | 已对照 |
| PC1 | `SCL` | DVC I2C SCL | `i2c_gpio_set(I2C_GPIO_GROUP_C0C1)` | 已对照 |
| PC2 | `OWC-TX` | `OWC_TX_PIN` | One-wire/UART 业务网络 | 已对照；具体复用状态由 bus mux 控制 |
| PC3 | `OWC-RX` | `OWC_RX_PIN` | One-wire/UART 业务网络 | 已对照 |
| PC4 | `MCU-LDO` | `MCU_LDO_PIN` | 当前 `conf.h` 定义 | 网络已对照；本文不推断其上电时序 |
| PD3 | `SOC100` | `SOC100_PIN` | 当前 `conf.h` 定义 | 原理图/代码网络已对照 |

### 3.1 D008 原理图仍需明确的板级项

- GP2/GP3 被引到外部连接器，当前产品/BOM 是否实际安装外部 NTC **没有从现有原理图材料得到唯一结论**。
- `MCC-EN-HT`、`MCU-LDO` 的完整电源所有权和低功耗时序需结合调用链/实板测量确认。
- 原理图支持 24S LFP 与 20S NMC 两种装配，但 BOM 差异（尤其 GP2/GP3）不能由化学体系自动推断。

## 4. DVC GP 配置：当前源码实际值

`dvc1124_project_config.h` 当前默认：

| GP | 当前功能 | 编码 |
|---|---|---:|
| GP1 | NTC；源码指定为 MOS NTC | 1 |
| GP2 | NTC | 1 |
| GP3 | NTC | 1 |
| GP4 | NTC；源码指定为 Battery NTC | 1 |
| GP5 | low-side CHG | 7 |
| GP6 | low-side DSG | 7 |

因此当前 GP 寄存器语义编码为：

- `0x74 GP123_MODE = 0x49`；
- `0x75 GP456_MODE = 0x7F`。

**注意：GP2/GP3 配成 NTC 是当前源码默认，不等于已证明所有 D008 BOM 都装 NTC。** 若 BOM 未装，应通过产品 profile 明确改为 OFF，而不是继续沿用默认。

## 5. DVC 静态工作配置

以下是当前源码直接可证明的默认/owned-field 配置。寄存器中有保留/未命名位时，驱动采用 read-modify-write 保留它们，因此不把整个字节伪装成固定镜像。

| 项目 | 当前配置 | 寄存器/说明 |
|---|---|---|
| High-side FET mask | 1 | 0x55 `HSFM=1`；D008 使用 GP5/GP6 low-side CHG/DSG |
| CADC work | 1 | 0x55 `CAEW=1` |
| Current-wake engine | 0 | 0x55 `CAES=0`，同时 CWT=0 |
| CC1 work time | 4 ms | 0x56 code 3 |
| CC1 sleep wake time | 32 ms | 0x56 code 3 |
| Charge pump | 10 V | 0x6D `CPVS=101` |
| Cell signed mode | 0 | 0x6D `CVS=0` |
| VADC | enable | 0x6E bit7=1 |
| VADC sync CC2 | enable | 0x6E bit6=1 |
| VADC period | every 1 CC2 | period code 0 |
| VADC time | 1.54 ms | time code 1 |
| V3P3 sleep/work | enable/enable | 0x77 bits7/6=1 |
| I2C timeout restart V3P3 | 0 | 0x77 bit5=0 |
| I2C watchdog | 0 s / disabled | 0x77 watchdog code disabled by product policy |
| Timed wake | OFF | 0x78 |
| Interrupt mask | `0xFF` | 0x79；1 表示屏蔽；当前不消费 DVC GP interrupt |
| DSG pull-down DPC | 16 | 0x52 named DPC field |
| Core OT shutdown | 0 / disabled | 0x76 threshold code 0 |
| Current-wake threshold | 0 / disabled | 0x65 |
| Body-diode threshold | 0 / disabled | 0x66 |
| SCD threshold/delay | 0 / disabled | 0x62/0x63，等待产品实板签核 |
| I2C timeout close CHG/DSG | 0/0 | 由 0x53/0x54 mask policy 表达 |

0x53/0x54 使用 V1.2 的 reset-equivalent 完整 mask policy：默认分别以 `0x59` / `0xF9` 为基线，并由已持久化的 watchdog timeout-close 语义项覆盖对应位。mask 的语义是 **1=屏蔽该来源对输出的动作，0=允许**。

## 6. 电芯通道与 24S/20S

- DVC1124-2 支持 4..24S；当前默认 24S。
- 20S 编译时 `D008_PRODUCT_CELL_COUNT=20`，源码会把未使用的 Cell21..Cell24 加入 measurement mask。
- 24S 编译不屏蔽上部通道。
- 物理串数由 compile-time Product Profile 决定，不再依赖历史 `SeriesNum` 去决定 AFE 通道连接。

## 7. AFE 硬件保护：独立于软件三级参数

当前保护分为两套独立参数：

- 软件保护：`g_tParam.protect`，First / Second / Third / Recover / Filter；
- AFE 硬件保护：`bms_afe_hw_profile_t`，没有 First/Second/Third。

AFE profile 使用统一 V2 协议：requested 35 words、metadata、effective 35 words。首次迁移可从旧参数初始化一次，此后两套参数独立演进；修改软件保护不得自动改 AFE 硬件保护。

### 7.1 DVC 量化规则（按 V1.2 + 当前代码）

| 保护 | 硬件量化事实 |
|---|---|
| COV/CUV | 12-bit threshold；delay 离散 200..8000 ms，当前编码选择不晚于请求的支持值 |
| OC1 | threshold code × 0.25 mV；code 0=disable；delay=(code+1)×8 ms |
| OC2 | enable bit6；threshold=(code+1)×4 mV；delay=(code+1)×4 ms |
| SCD | enable bit6；threshold=code×10 mV；delay=code×7.81 µs |
| Current wake | 0=off，否则 code×10 µV |
| Body diode | 0=off，否则 code×40 µV |

对 200 µΩ shunt，硬件可表示的电流档位必须由 sense voltage / Rsense 换算；上位机显示 requested 与 effective，不能把请求值当成芯片实际值。

## 8. 采样、通信与 fail-safe

- I2C 使用 PC0/PC1、100 kHz；每次硬件 BUSY 等待有 `DVC1124_I2C_CMD_TIMEOUT_US=5000` µs 上限。
- 失败重试次数默认 3；总线恢复会 reset I2C module 并重新初始化。
- AFE reset 后等待 300 ms 再进入正常访问。
- 公共 AFE guard 在无效 snapshot 时立即 inhibit 输出；连续异常会有界 reinit，恢复后仍要求连续有效 snapshot 才允许重新输出。
- 软件侧 FET off 在物理 I2C dead-bus 时只能 best effort；DVC WDT/PD7 power-cycle 的最终安全策略仍必须实板验证。

## 9. Balance / Open-Wire

- DVC 0x67..0x69 balance bit 约 60 s 自动清除；当前软件用 45 s 条件续期。
- 续期要求有效采样、存在充电电流、无充/放保护阻断；Open-Wire 流程期间暂停 Balance。
- Open-Wire 已实现 trigger/wait/fresh-snapshot/raw-result 状态机，但“哪些测量特征判定真实开线”的最终 evaluate 阈值仍需依据手册和实板开线实验签核。

## 10. 当前明确未签核项

1. DVC SCD 产品阈值、延时和恢复策略；默认保持关闭。
2. DVC I2C WDT / PD7 AFE power-cycle 的最终 dead-bus 安全路径。
3. Body-Diode 自动恢复策略。
4. GP2/GP3 实际 BOM 选件状态。
5. `SNC103B13435F0603E` NTC R-T 与温箱校准；当前软件表是历史 10K 表，不等于该料号已验证。
6. 24S LFP 与 20S NMC 的最终容量、OV/UV、OC、温度、SOC OCV/端点。
7. Open-Wire 判定、Balance 温升/采样干扰、Flash/断电故障注入。

## 11. 当前权威源码入口

- `vendor/ble_sample/conf.h`：D008 MCU 网络名、历史产品参数兼容层。
- `vendor/ble_sample/d008_product_profile.h`：24S LFP / 20S NMC 物理 profile。
- `vendor/ble_sample/dvc1124_project_config.h`：DVC 板级默认配置。
- `vendor/ble_sample/dvc1124_reg.h`：DVC1124-2 V1.2 寄存器真值。
- `vendor/ble_sample/dvc1124.c`：I2C、量化、采样、静态配置、Balance/Open-Wire。
- `vendor/ble_sample/dvc1124_bms.c`：BMS 故障/FET/恢复适配。
- `vendor/ble_sample/bms_sw_protection.*`：统一软件三级保护。
- `vendor/ble_sample/bms_afe_hw_profile.*`：独立 AFE 硬件保护 profile。

本文只描述当前源码和现有证据；任何未列出的“常见配置”都不是本产品事实。