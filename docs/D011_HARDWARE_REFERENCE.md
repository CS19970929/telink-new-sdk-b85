# HS-D011-10S50A-V1 硬件事实与固件检查依据

> 适用对象：HS-D011 / TLSR8251F512ET32 / SH3673510 / 默认 10S。
> 本文是原理图的结构化阅读索引，不替代原始 PDF、BOM、器件手册或实板验证。
> 项目状态见 [D011_DEVELOPMENT_STATUS.md](D011_DEVELOPMENT_STATUS.md)。

## 1. 来源与证据等级

| 项目 | 值 |
|---|---|
| 原始文件 | `hs-d011-10s50a-v1.pdf` |
| 本次用户提供位置 | `/Users/cs/Downloads/hs-d011-10s50a-v1.pdf`（不是云端可访问路径） |
| PDF SHA-256 | `7938cbf411ca051724b42acedb38243fcd7b1b3269e40ee5dfd30eefe823b473` |
| 页数 | 1 |
| 图纸日期 | Friday, August 21, 2026 |
| 标题栏限制 | Title / Doc / RevCode 为占位内容；V1 来自文件名，不代表标题栏已填写版本 |
| 核对方法 | 原始文本抽取 + 单页渲染 + MCU、AFE、供电、接口、温度与功率区逐区目视核对 |
| 初次代码对照 | `cf1d92e5edca66a616e3b54406c83b41a54d90e3` |
| 当前维护分支 | `feature/sh3673510-d011-bms` |

证据标签：

- **SCH**：图纸明确标注的器件、网络、引脚号、数值或可追踪连线。
- **DERIVED**：由图纸连接计算或推导，注明前提，仍需 BOM/实测确认。
- **CODE**：当前代码实现；不表示手册证明或硬件测试通过。
- **TODO_VERIFY_HW**：图纸不足以确认的器件语义、装配选项、产品参数或实板行为。

本文件只使用用户此次指定的 D011 图纸。D008/DVC1124 与 C099/APM32 的引脚、采样电阻和产品参数不适用于 D011。
原先 SH3673520 项目名称不能覆盖图纸中的 **SH3673510** 标注。
SH36735XX CV1.0A 明确说明 SH3673510/3514/3517/3520 仅支持串数不同，其余功能相同；仓库继续由 `sh3673520.*` 承载该系列共用寄存器与 SPI 协议驱动。

## 2. 产品与关键器件

| 对象 | 图纸事实 | 固件含义 |
|---|---|---|
| MCU U3 | TLSR8251F512ET32，5x5_32pin，另有 33 号 GND 焊盘 | TC32 / Telink B85；不得换 ARM 编译器 |
| AFE | SH3673510，符号包含 VC0..VC20、TS1..TS4、SPI、CHG/DSG 等 | 器件行为须核对 SH3673510 对应版本手册 |
| 电芯 | B0..B10 接入 VC0..VC10 | 默认 10S；不是 20S |
| MCU 晶体 Y1 | 24M，XC1/XC2 | 外部晶体频率不等于固件系统时钟；系统时钟查 app_config.h |
| U1 | HT7533-3.3V | 输入稳压/保护网络后生成 3V3 |
| U2 | CA-IS2092A | 隔离 RS485，原边 C-3V3，隔离侧 ISO-5V / ISO-GND |
| U4 | EL3H7-C | RS485 侧信号经光耦生成 CMNT-WK |
| 功率 MOS | QD1..QD9，PW016N10TS | 主充放电与加热路径不能混用 |
| 电流电阻 | RS1..RS8，各 2mR，并联 | 八只全部装配时等效 250 uOhm |
| 温度 | 实装 NTC 均为 10K；原理图 RN3/RN4 的 10M 为图纸标注错误（用户已确认） | 软件按 10K NTC 曲线处理 |
| 保护/加热相关 F1 | DPM4022，三端符号 | 动作语义必须另查器件资料与产品要求 |

文件名的 50A 不是软件过流/短路阈值定义。容量、化学体系、OV/UV、充放电电流、加热功率、恢复条件不能仅凭图名推断。

## 3. MCU GPIO 与网络表

方向从 MCU 视角描述。方向中标为 CODE 的项须再由器件手册验证；引脚号和网络名称为 SCH。

| U3 管脚 | MCU GPIO | 原理图网络 | 连接/用途与方向 | 代码符号 |
|---:|---|---|---|---|
| 1 | PD4 | CMNT-EN | 输出至 R16/Q4/Q2，控制 C-3V3 支路 | D011_CMNT_EN_PIN |
| 2 | PD7 | SCLK | SPI 时钟输出，经 R48 到 AFE SCK | D011_AFE_SCLK_PIN |
| 3 | PA0 | DI1 | SW1 开关检测输入，经 R23；存在上拉网络 | D011_SWITCH_PIN |
| 4 | PA1 | 485-EN | 输出至 U2 DE 与 /RE，共用方向控制 | D011_RS485_EN_PIN |
| 5 | PA7 | SWS-A7 | SWS1 下载/调试；保留专用 | D011_SWS_PIN |
| 6 | PB1 | INT-WK-MCU | 外部检测电路输入；CODE 使用高有效唤醒 | D011_INT_WK_MCU_PIN |
| 14 | PB4 | HT-CHG | 输出至 R100/Q17，加热/保护功率网络 | D011_HEATER_CHG_PIN |
| 15 | PB5 | HT-RF-EN | **加热回路保险丝熔断触发输出**；正常/故障诊断阶段必须保持低，只有经验证的不可逆熔断状态机才允许拉高 | D011_HEATER_FUSE_TRIGGER_PIN |
| 16 | PB6 | MISO | SPI 输入，经 R43 到 AFE SDO | D011_AFE_MISO_PIN |
| 17 | PB7 | MOSI | SPI 输出，经 R44 到 AFE SDI | D011_AFE_MOSI_PIN |
| 20 | PC0 | ALARM | 经 R51 接 AFE ALARM；CODE 配置为输入、低有效唤醒 | D011_AFE_ALARM_PIN |
| 21 | PC1 | RESET | 经 R41 接 AFE RESET；CODE 配置为输入，不能凭名称作为 MCU 复位输出 | D011_AFE_RESET_OUT_PIN |
| 22 | PC2 | SCI1-TX | UART TX，经 R11 到 U2 DI/A-TX | D011_SCI1_TX_PIN |
| 23 | PC3 | SCI1-RX | UART RX，经 R13 接 U2 RO/B-RX | D011_SCI1_RX_PIN |
| 24 | PC4 | DB-LED1 | LDB1 阴极侧；另一侧由 3V3 经 R26=10k 供电，低电平点亮（DERIVED） | D011_DEBUG_LED_PIN |
| 31 | PD2 | CS-M | SPI CS 输出，经 R49 到 AFE /CS | D011_AFE_CS_PIN |
| 32 | PD3 | CMNT-WK | U4 光耦输出经 R118=1k；通信唤醒输入 | D011_CMNT_WK_PIN |

U3 其他管脚：7=DVSS，8=VDD1V，9=VDD-IO，10=VDDDC-SW，11=VDDDC，12=VDD1V2，13=VDD-F，18=VDD3，19=VDDIO-AMS，25=RESETB，26=VANT，27=ANT，28=AVDD1V2，29=XC1，30=XC2，33=GND。
这是管脚索引，不是各电源轨的额定电压说明；电源连接须回看 U3 去耦/DC-DC/RF 区。

禁止直接沿用 D008 的 PC0/PC1 I2C、PB1 CHG-IN、PD4 RF 或 PC4 MCU-LDO。
D011 的 PC4 实际是 LED；D011 的 PD4 实际控制通信供电。

## 4. SPI、RESET、ALARM

| MCU 网络 | AFE 管脚/名称 | 串联电阻 |
|---|---|---|
| CS-M | 30 / CS | R49=100R |
| SCLK | 31 / SCK | R48=100R |
| MOSI | 32 / SDI | R44=100R |
| MISO | 33 / SDO | R43=100R |
| RESET | 34 / RESET | R41=100R |
| ALARM | 29 / ALARM | R51=100R |

R32..R37 为这些接口网络的 10k 上拉至 3V3；逐根对应以图中连线为准。
CODE：B85 SPI 组 `SH3673520_SPI_GROUP_B6_B7_D2_D7`，固定 500000 Hz，SPI_MODE3，硬件 SPI + 帧级 GPIO/CS 控制。
SH36735XX CV1.0A 要求 SCK 高/低电平时间均至少 500 ns，因此总线最高 1 MHz；500 kHz 留有 2 倍周期裕量。Mode 3、CRC、ACK 与帧格式由系列手册/参考代码约束。
必须给出 SH3673510 手册版本/章节和示波器或逻辑分析仪证据；本次仅确认物理连线，不重新认证既有协议常量。

## 5. 电芯通道、均衡与装配选项

- SCH：B0..B10 分别通过相应 RC0..RC10（1k）接入 VC0..VC10；CB0..CB10 标为 1u-50V。
- SCH：AFE VC11..VC20 在图中连成公共节点，并连接至 VC10 节点。固件不得把这些上部通道当作独立电芯。
- SCH：QB1..QB10 为 MMBT5551，RB1..RB10 为 100R；图中另有 510R 控制支路，构成逐节外部电路。
- TODO_VERIFY_HW：均衡控制机制、允许同时开启的通道、采样干扰、时间限制与均衡电流必须查手册并测量，不能只由晶体管数量判定。
- SCH：存在 BAT4/BAT8/BAT10 接口及 NC 选件。**有接口不表示当前 10S 固件已支持 4S/8S。**
- CODE：D011 有效电芯数为 10，均衡掩码限制为 0x03FF；报告剩余槽位清零。后续修改须保证 min/max、压差、SOC、保护与均衡都只使用有效通道。

## 6. 电流采样与单位

SCH：RS1..RS8 是跨接同一对节点的八只 2mR 电阻；AFE RS1/RS2 是管脚名称，与电阻位号 RS1/RS2 不同。
AFE 23 号 RS1、24 号 RS2 经 R86/R87（100R）及 C45/C46/C47（100n）连接采样网络。

DERIVED（八只均装配、各为 2mOhm）：

```text
Rsense = 2mOhm / 8 = 0.25mOhm = 250uOhm
50A 时采样压差 = I * R = 12.5mV
50A 时电阻网络总耗散 = I^2 * R = 0.625W
```

这些计算不证明单只封装功率、PCB 温升或 50A 长期运行能力。需核对 BOM、焊接、Kelvin 取样、热分布及校准。

CODE：CADC 换算得到有符号 mA；当前 BMS 适配把负电流作为充电、正电流作为放电，最终报告电流单位为 0.1A。
TODO_VERIFY_HW：用已知方向的小电流验证符号、零点及线性，同时回归 SOC 与 BLE/Modbus；不要仅凭网络名称认定方向。

## 7. 温度通道及明显待确认项

| AFE 管脚 | 网络 | 图纸元件 | 当前软件 | 待确认项 |
|---:|---|---|---|---|
| 25 / TS1 | TS1 | TTC1 外接点；RN6=10K-3435，C41=1n-50V | 电池温度 1 | NTC 装配/外接并联关系、放置位置与校准 |
| 26 / TS2 | TS2 | TTC1 外接点；RN5=10K-3435，C40=1n-50V | 电池温度 2 | 同上 |
| 27 / TS3 | TS3-NC / 加热 MOS 邻近 | **实际 10K NTC**；原理图 RN3=10M 为标注错误 | 当前仍不参与保护/加热控制 | 后续可作为加热 MOS 独立过温保护输入 |
| 28 / TS4 | TS4-MOS | **实际 10K NTC**；原理图 RN4=10M 为标注错误 | 10K NTC 曲线，作为充放电 MOS 温度 | 已由用户确认实际器件 |

D011 温度换算统一按 10K NTC。TS3 当前“未参与控制”是软件策略，不再是阻值不确定导致的禁用；若后续启用加热 MOS 独立温度保护，应给出独立阈值/恢复值并纳入加热状态机。

## 8. 主功率、加热、保护链

- SCH：AFE DSG（37）与 CHG（38）经分立器件网络形成 DO/CO 栅极驱动；DSGD（41）、CHGD（42）也进入外围电路。
- SCH：QD1/QD3/QD5/QD8 为一组并联功率管；QD2/QD4/QD6/QD9 为另一组，形成 B- 至 P- 的主功率通路。不可把某个 MCU GPIO 直接命名为主 CHG/DSG 栅极。
- SCH：HT-CHG/PB4 经 R100/Q17，HT-RF-EN/PB5 经 R75/Q11，连接 Q8/Q12/QD7/F1/HT-1 周围功率网络。
- 产品语义确认：PB4/HT-CHG 是可逆加热控制；PB5/HT-RF-EN 是加热回路保险丝熔断触发。当前固件对 PB5 只允许输出低电平；在加热 MOS 温度通道、失控判据、动作持续时间、F1 动作特性及不可逆锁存流程全部验证前，禁止任何自动拉高路径。
- SCH：R91=NC-1210、F1 周围存在短接/开窗装配备注。需记录生产 BOM 选项，不能在软件文档中默认“均已短接”。

## 9. 供电、通信与唤醒

### 9.1 供电

SCH：图左上包含 B4/B8/B10、D1/D2、Q1/Q3、R2/R7 NC 选件与 U1 3V3 电源网络。
其装配选项决定不同串数版本的取电方式，不能从 10S 参数反推 R2/R7 实装状态。

SCH：PD4/CMNT-EN 经 R16=330R 驱动 Q4，再控制 Q2，产生 C-3V3。Q2 输入是 3V3，输出为 C-3V3。
DERIVED：按图中 N-MOS 下拉与 PNP 高边拓扑，CMNT-EN 高电平开启该支路；需测 C-3V3 与启动时间确认。
此网是 U2 通信供电控制，不是 MCU 自保持电源。

### 9.2 RS485

- U2 DI=4，经 R11=100R 接 PC2；RO=1，经 R13=100R 接 PC3。
- U2 DE=3 与 /RE=2 共接 PA1/485-EN，经 R15=100R，R20=4.7k 下拉。
- CODE：PA1=0 接收、=1 发送，等待 UART 发送完成才释放方向；U2 真值表与时序需按器件资料核对。
- U2 A=14 经 R10=22R 接 485A；B=15 经 R14=22R 接 485B。
- 图上 R12=1k 跨 A/B；R6/R17=10k 偏置，D3/D6/D7 为保护器件。**不得自动把图中的 1k 解释成标准 120Ohm 终端。**
- UART1 4 针接口：1=GND，2=485A，3=485B，4=3V3。A-TX/B-RX 通过 R38/R40（NC-0805）作为可选连接；接口名 UART1 不表示默认 TTL UART 电平。
- GND 与 ISO-GND 是不同网络；外部连接与示波器接地须按隔离边界操作。

### 9.3 唤醒与按键

- PA0/DI1 接开关检测网络，SW1 两针为 SW1/GND；CODE 使用低有效。
- PB1/INT-WK-MCU 来自 Q5/Q6 等分立检测网络，经 R42/C34 接入 MCU。需要区分充电器、负载、按键与电源状态，不能把 PB1 高电平简单等同所有“允许加热/允许充电”条件。
- PD3/CMNT-WK 来自 U4，U4 输入通过 R116/D28 跨接 485B/485A。脉冲极性、持续时间、总线静态偏置下能否误唤醒需测量。
- ALARM/RESET 的唤醒极性和清除条件须核对 AFE 手册。配置了 GPIO wake 不等于已完成低功耗流程。
- 进入低功耗前应检查所有实际启用的唤醒源、当前有效电平及 UART/SPI/Flash/BLE 状态；无法证明可安全休眠时保持 RUN。

## 10. 固件阅读索引

以下源码均在 `tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/` 下：

| 文件 | 用途 |
|---|---|
| sh3673510_project_config.h | D011 GPIO、10S、250uOhm、SPI 与 NTC 索引 |
| bms_afe_backend.h / bms_afe.h | 活动 AFE 选择与应用 API 绑定 |
| sh3673520.c / .h / _reg.h | 既有共用传输、寄存器、采样及换算；名称不是 AFE 型号真值 |
| sh3673520_port.c / .h | B85 SPI 与时间基准 |
| sh3673510_control.c / .h | 初始化、保护写入、故障清除、均衡、MOS、GPIO、AFE sleep/wake |
| sh3673510_bms.c | BMS 报告、温度、分级保护、MOS 许可、加热、均衡 |
| conf.h | D11 产品选择与旧接口别名；别名不是硬件事实 |
| app.c | 旧业务、MOS 请求、温度/保护动作与 MCU 低功耗调用链 |
| modbus_uart.c / modbus_rtu.h | RS485 DMA/方向与诊断范围 |
| bms_state.* / bms_error.h | 通用报告、故障、历史和状态所有权 |

验证入口：`tests/sh3673520_contract_check.py`、`tests/sh3673510_d011_integration_check.py`、`bms_tools/bms.py`、`.github/workflows/bms-ci.yml`。

## 11. 后续 AI / 工程师维护规则

1. 先确认分支、原始 PDF 哈希和 BOM，再读本文件、完成度清单与相关源码。
2. 修改 GPIO/串数/Rsense/温度前，更新对应事实条目及证据；不要用已有代码反向“证明”原理图。
3. 原理图只证明连接。新增寄存器/CRC/保护量化/RESET/ALARM/睡眠语义时，补充精确 SH3673510 手册版本、页码和官方示例来源。
4. 若原图与 BOM 或软件冲突，保留两侧证据和 TODO_VERIFY_HW，先关闭证据缺口，不能静默任选一方。
5. 改动后更新状态清单、执行固定 TC32 门禁；硬件项须附板号、固件 SHA、参数、仪器及波形/测量结果。
6. 保留原始 PDF 原件；此 Markdown 未包含全部无源器件、几何连线和封装细节，不能用于替代网表/ERC/生产装配资料。
