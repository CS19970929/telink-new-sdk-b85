# D013 产品硬件与固件配置基线

> 适用分支：`feature/sh3673510-d013-bms`。
>
> **重要证据限制：当前可访问的用户资料中没有找到 D013 专属原理图。** 因此本文把“当前源码事实”和“原理图已验证事实”严格分开。D013 的 GPIO 映射目前只能写成 **CODE（D011 派生）**，不能写成 D013 硬件事实。获得 D013 原理图前，禁止把 D011 引脚表直接视为 D013 已核对连接。

## 1. 证据优先级

1. D013 专属原理图/BOM：**当前缺失，待用户补充/重新定位**。
2. SH36735XX CV1.0A：AFE 寄存器、SPI、保护和状态机事实。
3. 当前 D013 分支源码：当前实际编译配置。
4. D011 原理图只能解释代码来源，不能证明 D013 板连接。

因此本文凡涉及 D013 板级 IO 均标记为“源码继承、未原理图确认”。

## 2. 当前源码产品身份

`sh3673510_project_config.h` 当前明确写入以下产品 profile：

| 项目 | 当前源码事实 | 硬件证据状态 |
|---|---|---|
| MCU | TLSR8251F512ET32 | 未取得 D013 原理图复核 |
| AFE | SH3673510 | 未取得 D013 原理图复核 |
| cell count | 4 | CODE |
| shunt | 100 µΩ | CODE；注释为 2 mV : 20 A -> 0.1 mΩ |
| SPI group | PB6 MISO / PB7 MOSI / PD7 SCLK / PD2 CS | CODE，沿用 D011 配置 |
| NTC nominal | 10K | CODE，沿用 D011 假设 |
| Modbus transport | direct UART，`MODBUS_RS485_ENABLE=0` | CODE |

### 2.1 当前最重要的身份技术债

D013 `conf.h` 仍保留 D011 身份：

- `FD_BMS_TYPE=D11`；
- `BMS_HARDWARE_VERDION_DEFAULT="D011"`；
- `BMS_SERIAL_NUMBER_DEFAULT="D011-UNSET"`；
- `DEV_NAME_STR="BT_D011"` / factory name 同样是 D011；
- `CapacityFactory=116`、`AFE_ODC1=300`、`AFE_ODC2=500` 仍是 D11 历史兼容值；
- `CS_Res=2`、`CS_Res_Num=20` 用于表达当前 2 mV : 20 A / 100 µΩ 兼容换算。

这些值是**当前源码事实**，不是 D013 产品已签核参数。文档整理不能把它们改写成 D013 正确产品值；后续应单独做 D013 identity/product parameter cleanup。

## 3. MCU IO：当前源码映射（未获 D013 原理图验证）

`sh3673510_project_config.h` 仍使用 `D011_*` 宏名并继承 D011 的引脚表。以下只表示 D013 分支当前会按这些 GPIO 编译：

| GPIO | 当前源码宏 | 当前源码用途 | D013 原理图状态 |
|---|---|---|---|
| PD4 | `D011_CMNT_EN_PIN` | communication enable | **未验证** |
| PD7 | `D011_AFE_SCLK_PIN` | AFE SPI SCLK | **未验证** |
| PA0 | `D011_SWITCH_PIN` | switch input | **未验证** |
| PA1 | `D011_RS485_EN_PIN` | RS485 direction | **未验证；且 D013 conf 当前 `MODBUS_RS485_ENABLE=0`** |
| PA7 | `D011_SWS_PIN` | SWS debug | **未验证** |
| PB1 | `D011_INT_WK_MCU_PIN` | interrupt/wake input | **未验证** |
| PB4 | `D011_HEATER_CHG_PIN` | heater control | **未验证** |
| PB5 | `D011_HEATER_FUSE_TRIGGER_PIN` | fuse trigger，safe level 0 | **未验证；不得在 D013 上假设存在同一不可逆硬件** |
| PB6 | `D011_AFE_MISO_PIN` | SPI MISO | **未验证** |
| PB7 | `D011_AFE_MOSI_PIN` | SPI MOSI | **未验证** |
| PC0 | `D011_AFE_ALARM_PIN` | AFE ALARM | **未验证** |
| PC1 | `D011_AFE_RESET_OUT_PIN` | AFE RESET network | **未验证** |
| PC2 | `D011_SCI1_TX_PIN` | UART TX | **未验证** |
| PC3 | `D011_SCI1_RX_PIN` | UART RX | **未验证** |
| PC4 | `D011_DEBUG_LED_PIN` | debug LED | **未验证** |
| PD3 | `D011_CMNT_WK_PIN` | communication wake | **未验证** |
| PD2 | `D011_AFE_CS_PIN` | SPI CS | **未验证** |

### 3.1 结论

在 D013 原理图缺失的当前状态下：

- 不能声称上述 GPIO 与 D013 PCB 一致；
- 不能声称 PB5 是 D013 的保险丝熔断输出；
- 不能声称 TS3/TS4 在 D013 上分别靠近 heater MOS / power MOS；
- 不能声称 D013 存在 D011 的隔离 RS485、C-3V3、CMNT-WK 电路；
- 任何 D013 实板测试前应先补 D013 原理图并逐网核对。

## 4. SH3673510 AFE：当前源码静态配置

AFE 寄存器模型来自 SH36735XX CV1.0A；当前 D013 与 D011 共用 `sh3673520_reg.h` 和 `sh3673510_control.c`。这在芯片系列层面有手册依据；板级配置仍需 D013 原理图确认。

解析当前 `sh3673510_project_config.h`：

| 寄存器 | D013 当前静态值 | 关键字段 |
|---|---:|---|
| SCONF1 0x40 | `0x00` | Normal |
| SCONF2 0x41 | `0x50` | PD_EN=1、PUMP_EN=1；PDSGMOS/DSGMOS/CHGMOS boot=0 |
| SCONF3 0x42 | `0x44` | CGR_WK=1、LD_WK=OFF、CRLD_EN=CPLUS、OWD_EN/TRG=0 |
| SCONF4 0x43 | `0x64` | PDSGT code3=490 ms，CN=4 |
| SCONF5 0x44 | 初始 `0x3C` | MOS_EN=1、OCC_EN=1、CADC_EN=1、WDT_EN=1、WDT code0 |
| SCONF7 0x46 | `0x04` | RLD=0、CADCT=4S、CDV=4 |
| OWV/ALARMH 0x47 | `0x57` | OWV code5、LOADOFF/VADC/CADC interrupt=1 |
| ALARML 0x48 | `0xFF` | WK/WDT/OWD/TEMP/OCC/OCD/UV/OV interrupts enabled |

D013 与 D011 的关键静态 AFE 差异在当前源码中主要是：

- `CN=4`（D011 为 10）；
- `Rsense=100 µΩ`（D011 为 250 µΩ）；
- 通信模式在 `conf.h` 是 direct UART，而不是 RS485。

其余大量宏仍名为 `SH3673510_D011_*`，这是源码命名技术债，不是产品身份。

## 5. 运行时硬件保护配置

SCONF6 和 SCONF5/OCC 的最终 enable 状态来自独立 AFE hardware profile：

- COV -> OV_EN；
- CUV -> UV_EN；
- OCD1/OCD2 -> OCD_EN；
- SC -> SC_EN；
- TEMP -> TS1_EN + TS2_EN；
- OCC1 -> SCONF5 OCC_EN。

因此文档不把 SCONF6 写成永远固定 `0x3F`。若当前 persisted `enable_mask` 全开到 COV/CUV/OCD/SC/TEMP，则运行时会得到相应 0x3F 组合；实际设备必须以 profile + readback 为准。

## 6. SH3673510 硬件保护量化（当前代码）

D013 与 D011 使用同一量化实现，但因为 Rsense 不同，**同一个寄存器 sense-voltage code 对应的电流不同**：

| 项目 | 芯片/代码量化 |
|---|---|
| OV/UV | 5 mV/code；delay 使用手册离散档位 |
| OCD1 | (code+1)×5 mV sense；delay使用 8 档表 |
| OCD2 | (code+1)×10 mV sense；delay=(code+1)×25 ms |
| OCC | (code+1)×1.375 mV sense；delay使用 8 档表 |
| SC | effective OCD2 × {2,3,4,6}；delay={2,4,8,16,32,64,128,256} µs |
| HW Temp | 10K NTC table -> divider code |

当前 D013 `Rsense=100 µΩ` 来自源码 profile，因此 effective current 计算必须使用 100 µΩ；不得继续套 D011 的 250 µΩ 电流范围/限制。

## 7. WDT / Powerdown / Sleep

- 当前静态配置 `WDT_EN=1`，WDT code0；SH36735XX CV1.0A 对应约 32.34 s。
- 手册说明 WDT overflow 后还有 FLAG2/WDT_FLG 清除窗口，条件满足才进入 Powerdown；不能简化成“32 s 必然断电”。
- SLEEP `SCONF1=0xAA` 会关闭 CADC、WDT、保护、FET、charge pump、balance；唤醒受 CGR_WK/LD_WK 配置影响。
- D013 当前代码配置 `CGR_WK=1`、`LD_WK=OFF`。

这些是 AFE 芯片/源码事实；D013 外部 charger/load 网络是否与 D011 相同仍需原理图确认。

## 8. 软件保护与 AFE 硬件保护

- 软件保护统一使用 `g_tParam.protect`：First / Second / Third / Recover / Filter。
- AFE 硬件保护使用独立 `bms_afe_hw_profile_t`，没有三级概念。
- 两套参数独立持久化、独立修改；普通软件参数写入不得改硬件 profile。
- AFE 参数使用 requested/effective 分离、完整 35-word 原子写、固件 validate/persist/apply/readback/rollback；rollback 失败报告 `CONFIG_INCONSISTENT`。

## 9. 当前 D013 必须优先关闭的文档/硬件缺口

1. **补 D013 原理图/BOM**，逐一确认 MCU GPIO、SPI、RESET、ALARM、UART、开关、加热、唤醒、LED。
2. 明确 D013 实际 AFE 型号和封装，确认就是 SH3673510。
3. 用硬件资料确认 4S cell wiring、未使用通道处理方式。
4. 确认 100 µΩ shunt 的实际物料/并联结构、Kelvin 取样、方向和功率。
5. 确认 TS1..TS4 的实际 NTC 数量、阻值和物理位置；当前 10K/TS3 heater/TS4 MOS 是 D011 派生代码事实，不是 D013 原理图事实。
6. 移除/重命名 `D011_*` 宏和 D011 产品身份残留前，先用 D013 原理图确定正确名称，不能只做文本替换。
7. 签核 D013 容量、OV/UV、OC、SC、温度、SOC profile/端点；当前 D11 历史默认不能作为 D013 量产值。

## 10. 当前权威源码入口

- `vendor/ble_sample/conf.h`：当前 D013 编译身份/通信模式，同时暴露 D011 身份残留。
- `vendor/ble_sample/sh3673510_project_config.h`：4S/100µΩ 和当前继承 IO/AFE 静态配置。
- `vendor/ble_sample/sh3673520_reg.h`：SH36735xx CV1.0A 寄存器/协议真值。
- `vendor/ble_sample/sh3673510_control.c`：硬件保护量化、静态配置、FET/温度处理。
- `vendor/ble_sample/sh3673510_bms.c`：BMS适配、保护恢复、AFE communication fail-safe。
- `vendor/ble_sample/bms_sw_protection.*`：软件三级保护。
- `vendor/ble_sample/bms_afe_hw_profile.*`：独立 AFE HW profile。

获得 D013 原理图之前，本文不会把任何 D011 原理图事实复制成 D013 硬件事实。