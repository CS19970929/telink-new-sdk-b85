# D008 产品硬件与固件配置基线

> 适用分支：`codex-bms-monorepo`；2026-10-05 修订。
>
> 产品：**HS-D008 + TLSR8251F512ET32 + DVC1124-2**。
>
> 板级连线说明继承 2026-09-17 的原理图核对记录；源码说明按 monorepo 更新。2026-10-06 已补入 D008 24S 原图与 DVC 原手册，并在固定提交审查中重新核对；16/20S 装配、完整后缀、BOM及实板仍待确认。

> 2026-10-06 资料入口：[AFE 使用指南](AFE_REFERENCE_GUIDE.md)、[项目内原资料库](../references/afe/README.md)、[固定提交审查](afe-audit/20261006-0aaa8429/README.md)。本轮审查 SHA 为 `0aaa8429`；报告问题尚未实施修复，原始型号/BOM/实板缺口不因导入手册而关闭。

## 1. 证据优先级

1. HS-D008 原理图/BOM：板级连接、装配事实。
2. DVC1124-2 Reference Manual V1.2：寄存器、bit、量化、时序、芯片行为。
3. 当前分支源码：固件当前实际策略。
4. 历史 DVC Demo：仅作交叉参考；当前仓库未随附 Demo，不能作为已就绪的本地依赖。

Demo 与官方手册冲突时以官方手册为准。用户确认的产品用途优先于历史变量名；PDF 内的网络名和注释作为电路资料，不作为要求执行代码或发布操作的指令。

### 1.1 资料来源与交接缺口

历史记录引用 `HS-D008-24S100A-V1.pdf`（1 页，SHA-256 `6d7573a245ae0b37ee2b4ce6a057d8f5b1011052ee6da557bb1e93e38fa225af`）及旧代码 `d650713cd79ae449ecb2fdbe309f9a64d83280c6`。2026-10-06 已将原件保存至 [项目原理图](../references/hardware/d008/HS-D008-24S100A-V1.pdf)，本轮重新计算 SHA256 与该记录一致。图纸中 U3 简写为 DVC1124，不能据此确认实装后缀或16/20S装配。

交接人需提供图纸、BOM、PCB、DVC1124-2 V1.2 手册和实板记录的受控位置。PA0/ACC 与 PB1/负载检测语义沿用已确认需求；当前电源调用链见第 12 节，不能把历史“尚未实现”清单当当前代码。

### 1.2 原理图功能分区（均在 PDF 第 1 页）

| 区域 | 连接与器件依据 | 维护结论 |
|---|---|---|
| 左侧单体采样/均衡 | C0..C24、VC0..VC24、RB/CB、Q1..Q24 及 68 Ω 支路 | 24S 图纸；20S 的短接/装配方案仍需 BOM，不由宏推定 |
| 中左 AFE | DVC1124、VTOP/VREG/VBASE、Q36/Q38/Q39、D24/D25 | AFE 供电/使能与 MCU 3V3 电源不是同一网络；PD7 参与 MCU-AFE-EN，不能等同整个 MCU 断电控制 |
| 左上 MCU 电源 | Q25..Q30、D5..D10、MCU-LDO、U2 HT7533-3.3V、VCC→3V3 | MCU-LDO 接电源控制链；用户确认拉低后 MCU 断电，实际保持/唤醒波形待测 |
| 中右 MCU | U1 TLSR8251F512ET32、24 MHz Y1、SWS、射频匹配/AT1 | GPIO 映射见第 3 节；SWS-A7 是下载调试网络，不是开关 |
| 中右负载检测 | C−→D28/R102→Q40 栅极，R103/D29 对 B−，Q40 漏极经 R105→CHG-IN，R104 上拉 3V3 | 该网络接 PB1；其用途由用户确认为负载检测，不可由 CHG 名称推断充电器在位 |
| 右上 ACC/OWC | CN3、Q57/R152/R153/R154/R155、Q51..Q56、OWC-TX/RX；部分 R156..R158/R160 标 NC | ACC-MCU 是隔离于 MCU 侧的条件输入；OWC 有电平转换及 UART 接口；选装支路以实际 BOM 为准 |
| 下方功率回路 | RS1..RS10、QD1..QD6、QC1..QC6；GP6-DSG→Q47..Q50→DO，GP5-CHG→Q42..Q45→CO | 图纸确认 DVC GP5/GP6 驱动外部低边链；DVC 高边 CHG/DSG 引脚标未连接 |
| 下方加热/熔断 | PA1/MCC-EN-HT→Q34/Q33/Q35→QH1；PD4/MCC-EN-RF→Q32/Q31→D15/R74→F1 | 与射频 AT1 无关；熔断支路含 NC 标记，必须核对装配后再认定实物功能 |
| 温度 | GP1 经 R71 接 NTC2；GP4 接 NTC1；CN4 引出 GP2/GP3，R163/R164 各 3 MΩ | GP1/GP4 板载路径可见；GP2/GP3 外接探头型号、是否接入仍需线束/BOM；传感器实际热位置还需 PCB/实物 |

图纸中的 `B−`、`BAT−`、`BS−`、`C−` 不可任意互换；采样电阻、负载检测及驱动的参考节点须按实际网络核对。

## 2. 产品身份

| 项目 | 当前配置/事实 |
|---|---|
| MCU | TLSR8251F512ET32 |
| AFE | DVC1124-2；源码默认 `DVC1124_MODEL_22` |
| 开发模式默认 profile | **16S LFP**；生产构建必须显式传入 `--d008-profile` |
| 可选编译 profile | `16s-lfp` / `20s-nmc` / `24s-lfp`，生产没有隐式默认值 |
| 原理图能力 | 图纸为 24S（C0..C24）；不能据此推定所选 16S/20S 固件对应的实际装配 |
| AFE 总线 | I2C，PC0=SDA、PC1=SCL，100 kHz |
| DVC 地址 | `0x40` write / `0x41` read transfer address |
| Rsense | RS1..RS10 = 10 × 2 mΩ 并联，全部装配约 200 µΩ |
| 板载 NTC | NTC1/NTC2 标注 `SNC103B13435F0603E` |

容量、OV/UV/OC/温度默认值仍需产品签核。当前入口为产品 `bms_product.h`、`d008_product_profile.h`、`bms_product.h` 及公共 `bms_parameters.h`。

## 3. MCU IO 基线与代码核对

**结论：下面已命名 GPIO 的引脚号与本次图纸相符；原差异主要是业务语义和电源控制流程，当前实现已按本页约束接入（电气验收仍未完成）。D008 没有独立开关，不能继续从 `SW_PIN` 名字推导“关钥匙后关机”。**

| MCU GPIO / U1 引脚 | 原理图网络 | 当前代码符号/用途 | 本次确认与维护要求 |
|---|---|---|---|
| PD7 / 2 | `MCU-AFE-EN` | `BMS_BOARD_AFE_ENABLE_PIN`，AFE init 拉高 | 参与 AFE 供电/接口使能；不是 MCU 总电源开关，也不是 DVC 独立 RESET 引脚 |
| PA0 / 3 | `ACC-MCU` | `BMS_BOARD_ACC_PIN`，低有效运行开关 | 用户最新授权独立ACC深睡眠，见第 12 节与 `bms/app/app_power.c` |
| PB1 / 6 | `CHG-IN` | `BMS_BOARD_LOAD_DETECT_PIN`，保留输入，无业务读取/PAD 唤醒 | 实际为负载检测。Q40 导通时输出低；不等于已验证所有负载场景的逻辑。暂不实现负载判定、去抖或策略，也不能据此证明正在充电 |
| PC4 / 24 | `MCU-LDO` | `BMS_BOARD_MCU_LDO_PIN`，启动保持高、关机事务最后拉低 | 已接入控制路径；实测保持时点、掉电与复电时序仍为 TODO_VERIFY_HW |
| PC0 / 20 | `SDA` | DVC I2C SDA | 经 R96=100 Ω；与图纸一致 |
| PC1 / 21 | `SCL` | DVC I2C SCL | 经 R95=100 Ω；与图纸一致；SDA/SCL 各有 4.7 kΩ 上拉至 MCU 3V3 |
| PC2 / 22 | `OWC-TX` | `BMS_BOARD_OWC_TX_PIN` | 图纸同时接 UART/单线转换路径；复用由 bus mux 管理 |
| PC3 / 23 | `OWC-RX` | `BMS_BOARD_OWC_RX_PIN` | 同上，不移植 D011 RS485/SPI 网络 |
| PA1 / 4 | `MCC-EN-HT` | `BMS_BOARD_HEATER_ENABLE_PIN`，启动低、加热时高 | 加热控制链，图纸连线对应；热控制参数仍须验证 |
| PD4 / 1 | `MCC-EN-RF` | `BMS_BOARD_HEATER_FUSE_PIN`，启动低、熔断请求高 | F1 相关支路；不能按 RF 名称理解成 BLE 射频供电 |
| PA7 / 5 | `SWS-A7` | SWS 下载/调试 | 保留 SDK 调试用途 |
| PB4 / 14、PB5 / 15、PB7 / 17、PD3 / 32 | `SOC25/50/75/100` | 对应 SOC LED 宏 | 网络映射一致；外接显示负载、极性仍按实物核对 |
| PB6 / 16 | `BLUE` | `LED_BLUE_PIN` | 经 R165=3.3 kΩ 接 LED1 至 B−；代码别名不改变原图网络名 |

历史“暂不写逻辑”约束已由最新ACC开关授权部分替代：ACC高电平进入独立深睡眠，PC4不拉低，低电平唤醒；PB1只按已授权的负载移除/电流保护恢复策略使用。当前已剥离错误的 key/charger 业务读取；正常产品请求为 CHG+DSG ON，由 guard/保护/输出授权仲裁。**PB1 不提供自动加热的充电源资格。D008 以可靠充电电流作为 charger-session 的进入事件，进入后因预热主动关闭 CHG 导致的零电流不会清除 session，可靠放电电流会立即退出 session 并关闭 Heater。**

## 4. DVC GP / FET 拓扑

当前 `bms_product.h`：

| DVC GP | 功能 | code |
|---|---|---:|
| GP1 | heater NTC | 1 |
| GP2 | battery NTC #1 | 1 |
| GP3 | battery NTC #2 | 1 |
| GP4 | power MOS NTC | 1 |
| GP5 | CHG low-side | 7 |
| GP6 | DSG low-side | 7 |

编码：

- `0x74 GP123_MODE = 0x49`
- `0x75 GP456_MODE = 0x7F`

D008 正常控制路径：

```text
TLSR8251
  -> I2C
  -> DVC R81 CHGC/DSGC
  -> DVC internal FET logic
  -> GP5/GP6 low-side output
  -> external CHG/DSG driver chain
```

本次图纸已确认 GP5-CHG→CO、GP6-DSG→DO 的外部驱动连线；未发现 GP5/GP6 或 MOS Gate 独立回读到 MCU 的线路。MCU 不直接 GPIO 控制 GP5/GP6。图纸连通不等于已测得栅极动作，仍保留 Physical Feedback unavailable/unknown。

`R81.CHGC/DSGC` 是命令模式；`R6.CHGF/DSGF` 是 DVC driver/output flag，不等同于 MOS 物理导通反馈。

## 5. 配置所有权：当前强制架构

D008 已取消 DVC operating-config Flash owner。**固定 DVC 配置全部来自编译期宏，每次 AFE reset/init 后重新下发。历史 operating-config KV 即使 Flash 中仍残留，也不会再读取或覆盖当前固件策略。**

### 5.1 编译期固定配置

唯一主要入口：

```text
bms/products/d008/bms_product.h
```

包括：

- DVC model/address
- physical cell count / Rsense
- GP1..GP6 mode
- high-side mask / low-side topology
- CADC / CC1 / VADC
- Charge Pump
- V3P3 / timed wake / interrupt mask
- DPC
- current-wake 固定策略
- Body-Diode / DBDM / CBDM
- DVC I2C watchdog
- I2C timeout close CHG/DSG
- fixed Core-OT policy

`d008_product_profile.h` 只负责 16S LFP / 20S NMC / 24S LFP 的装配串数和 chemistry/SOC identity，不再承载 DVC fail-safe 参数。历史 24S 选择记录不代表当前编译默认。当前开发默认 16S LFP，生产必须显式指定 profile 并按实物核对。

### 5.2 Flash 中保留的保护参数

仅保护参数保留运行时持久化：

1. MCU 软件保护：`g_bms_protection_params`
   - First / Second / Third
   - Recover
   - Filter
   - OV/UV/OC/温度等软件保护参数

软件保护记录在启动时必须通过完整参数校验。校验失败时保留原 Flash
内容用于诊断，但 `bms_protection_params_valid()` 保持 false，公共 AFE
输出门禁同时禁止 CHG/DSG；完整有效参数及所有启动存储域验证成功后才具备解除条件；普通 SaveParam 不绕过启动失败。
2. AFE 硬件保护：`bms_afe_hw_profile_t`
   - COV / CUV
   - OCD1 / OCD2
   - OCC1 / OCC2
   - SCD
   - delay / recover / enable mask

SOC、容量、事件日志等仍由各自独立存储模块管理，但不属于 DVC operating-config。

### 5.3 通信接口

- `0x2800` DVC semantic window：固定配置只读诊断。
- `0x2900` DVC raw mirror：只读诊断。
- 固定配置写入返回 READ_ONLY / Modbus exception。
- AFE HW protection 修改必须走 `bms_afe_hw_profile` 的完整原子事务。
- 软件保护修改继续走软件参数接口。

## 6. 当前固定 DVC 工作配置

| 项目 | 当前值 | 说明 |
|---|---:|---|
| High-side FET mask | 1 | D008 使用 GP5/GP6 low-side |
| CADC work | 1 | work enable |
| Current-wake engine | 0 | 当前关闭 |
| CC1 work time | 4 ms | code 3 |
| CC1 sleep wake | 32 ms | code 3 |
| Charge Pump | 10 V | CPVS=5 |
| Cell signed mode | 0 | unsigned cell voltage |
| VADC | enable | 使能 |
| VADC sync CC2 | enable | 使能 |
| VADC period | every 1 CC2 | code 0 |
| VADC time | 1.54 ms | code 1 |
| V3P3 sleep/work | 1 / 1 | enable / enable |
| V3P3 timeout restart | 0 | disabled |
| Timed wake | OFF | fixed |
| Interrupt mask | `0xFF` | 当前不消费 DVC interrupt |
| DSG pull-down DPC | 16 | reset-equivalent named policy |
| Core OT fixed policy | 0 | disabled |
| Current wake threshold | 0 µV | disabled |
| Body diode threshold | **80 µV** | BDPT=2；200 µΩ 下名义约 0.4 A |
| I2C watchdog | **4 s** | DVC hardware watchdog |
| timeout close CHG | **1** | WDT timeout 允许关闭 CHG |
| timeout close DSG | **1** | WDT timeout 允许关闭 DSG |

Body-Diode 80 µV 来自当前 D008 common-port 固件策略；仍需继续结合实板电流、噪声、MOS 行为验证其最终量产裕量。

## 7. Common-port CHG/DSG 策略

正常工作不按当前方向选择单个 MOS：

```text
Normal / charge / discharge:
CHG = ON
DSG = ON
```

发生单侧保护：

```text
charge-side protection only:
CHG = AUTO_DIODE (10b)
DSG = ON (11b)

discharge-side protection only:
CHG = ON (11b)
DSG = AUTO_DIODE (10b)
```

DVC 根据 body-diode/reverse-current 条件自主重新开启受保护侧 driver，MCU 不轮询电流方向强制开 MOS。

软件必须一次性写最终 R81 模式；禁止：

```text
hard OFF -> AUTO_DIODE
```

这种两阶段切换，也禁止每 200 ms 重复写同一个 AUTO_DIODE 模式，否则会破坏 DVC 自主续流状态。

共同故障、通信 inhibit、sleep、open-wire、output disable 等仍保持 CHG+DSG hard OFF 语义。

## 8. I2C watchdog 与 fail-safe

必须区分两套 timeout：

### MCU transaction timeout

```text
DVC1124_I2C_CMD_TIMEOUT_US = 5000 µs
```

它只是 TLSR8251 等待一次 I2C BUSY/transaction 的上限，不负责 DVC 自主关 MOS。

### DVC hardware I2C watchdog

```text
DVC1124_I2C_WATCHDOG_SECONDS  = 4
DVC1124_I2C_TIMEOUT_CLOSE_CHG = 1
DVC1124_I2C_TIMEOUT_CLOSE_DSG = 1
```

真实 I2C dead-bus 后，MCU 再通过 I2C 下发 `FETS(0,0)` 只能 best-effort，因为总线已经不可用。最终 fail-safe 必须依靠失联前已配置好的 DVC hardware watchdog。

当前 production `HW_PROTECT_ENABLE=1` 下关键寄存器预期：

- R77 watchdog bits `[2:0] = 100b`（4 s）；在当前 V3P3 policy 下超时前通常读到约 `0xC4`，状态位可能影响完整字节显示。
- R53 `DWM=0`，允许 I2C WDT 关闭 DSG；结合当前 mask policy 通常为 `0x50`。
- R54 `CWM=0`，允许 I2C WDT 关闭 CHG；结合当前 mask policy 通常为 `0x78`。
- R66 `BDPT=2`，即 80 µV。

R53/R54 mask 语义：**0 = 允许该来源动作；1 = 屏蔽该来源。**

### 8.1 受控 Shutdown/Wake 台架接口

禁止直接从产品代码调用 DVC 私有 Shutdown/Wake 原语。D008 仅公开公共
guard 管理的 `bms_afe_test_enter_shutdown()` / `bms_afe_test_wake()`：

1. Shutdown 前清均衡、CHG/DSG hard-off，并进入无 I2C 的 hold 状态；
2. Wake 走完整 AFE init，重新应用固定配置和 AFE HW profile；
3. 旧 snapshot/通信资格作废，连续 3 帧新有效采样后才允许恢复请求；
4. Requested、R81 command、R6 driver flag 与物理 Gate 反馈仍分别记录。

## 9. 保护编译隔离

| SW | HW | 行为 |
|---:|---:|---|
| 1 | 1 | production：软件保护 + DVC HW protection + 固定 fail-safe |
| 1 | 0 | 软件保护台架；DVC autonomous protection/fail-safe 主动关闭 |
| 0 | 1 | DVC HW protection 台架；软件保护状态清除，固定 fail-safe 保留 |
| 0 | 0 | 测量/通信调试；DVC autonomous protection/fail-safe 主动关闭 |

`HW=0` 时会主动：

- disable COV/CUV/OC/SCD application
- I2C WDT = OFF
- current wake = OFF
- Body-Diode = OFF
- Core-OT = OFF
- R53/R54 autonomous-close mask = `0xFF`

因此不能用 `HW=0` 验证 I2C watchdog 关 MOS。

Requested AFE Hardware Profile 仍保存在 Flash；重新用 `HW=1` 构建后继续使用原 requested profile。

## 10. AFE Hardware Protection 参数

软件保护与 AFE hardware protection 是两套独立参数：

```text
g_bms_protection_params
    = software First/Second/Third/Recover/Filter

bms_afe_hw_profile_t
    = DVC COV/CUV/OCD/OCC/SCD hardware profile
```

修改一套不得副作用重写另一套。

AFE profile 的 requested/effective 必须分开展示；DVC 量化后的值不能伪装成用户输入值。

主要量化事实：

| Protection | DVC 量化 |
|---|---|
| COV/CUV | 12-bit threshold；delay 200..8000 ms 离散 |
| OC1 | threshold code × 0.25 mV；delay=(code+1)×8 ms |
| OC2 | threshold=(code+1)×4 mV；delay=(code+1)×4 ms |
| SCD | threshold=code×10 mV；delay=code×7.81 µs |

物理电流阈值按 sense voltage / 200 µΩ Rsense 换算。

2026-10-08 产品确认三个 D008 装配 profile 默认开启 SCD：**200 A / 请求延时 256 µs**。依据 DVC1124-2 Reference Manual V1.2 第 20 页，40 mV 对应 `SCDT=4`，现有驱动将延时向下量化为 `SCDD=32`，名义硬件延时 249.92 µs，整数 effective 读回 250 µs；保持原量化规则和恢复逻辑。

D008 的 `BMS_UPDATE_AFE_REVISION=2u`，OTA 后对同 schema、相同产品/串数的旧编号设备恢复**整组 AFE 硬件保护默认值**，包括原自定义 COV/CUV/OCD/OCC；其他参数组及其他产品编号不变。启动成功后应读到 requested `0x284C=40`、`0x284D=256`，硬件读回 `0x285C=40`、`0x285D=250`。重复启动/旧固件回退规则和验证边界见 [OTA 参数更新](OTA_PARAMETERS.md#d008-开启短路保护2026-10-08)；此配置确认不代替实板动作与恢复验收。

### 10.1 COV / CUV 恢复采样修正（2026-10-09）

DVC1124-2 DS V1.1 原 PDF 第 13 页规定 CC2 周期为 256 ms；当前 VADC 配置与每个 CC2 同步，而应用层约 200 ms 轮询。原恢复入口位于 `sample_pending` 返回之后，使用电流样本时间戳检查 400 ms 连续性，正常等待转换的一轮叠加调度延迟就可能反复清零资格。历史 D008 日志中，最低单体持续高于 3278 mV、实际恢复门限 3100 mV 时，三级欠压仍持续存在。

现保持原有简单计数与恢复参数：每次有效 AFE 轮询先维护连续性，只有新鲜、正常的电压转换才推进 COV/CUV 恢复计数，不再等待新的电流样本。有效缓存不增加计数；无效采样、诊断电压、配置读取失败或超过原有轮询连续性限制仍清零。配置 1000 ms 对应 5 次合格电压转换，恢复时间约一秒，受转换与轮询相位影响，不承诺精确毫秒。RM V1.2 原 PDF 第 6 页规定 ALARM 写 0 清除，现有选择性清除及回读确认保持。

本次不改保护门限、ADC 周期、通信恢复三次样本资格、watchdog 静默、MOS 仲裁、协议或存储布局。已有恢复回归补入正常等待轮次与调度抖动、缓存不推进、无效/诊断采样清零场景。按本机约定，本次完成源码与差异审查，尚未执行 host 回归、目标编译或 OTA。修复覆盖已确认的软件机制；首次 CUV 触发来源及历史事件是否另有 I/O 清除失败/重触发仍待实板证据。

## 11. 实板验证重点

固定配置或 FET 策略修改后至少验证：

1. 上电后读回固定配置寄存器，确认没有旧 Flash 覆盖。
2. CHG protection -> 放电：CHG AUTO_DIODE 自动恢复后持续导通，不得出现 200 ms 周期关断脉冲。
3. DSG protection -> 充电：同理验证 DSG。
4. I2C 正常时确认 R77/R53/R54/R66 符合当前 compile-time policy。
5. 真正停止 MCU↔DVC I2C 通信，约 4 s 后验证 CHG/DSG driver 均被 DVC 自主关闭。
6. `SW/HW = 1/0、0/1、0/0、1/1` 均需 TC32 clean build；production 最终使用 `1/1`。
7. 任何 safety change 继续通过 source-order、Host contracts、firmware check、MAP、verify、cppcheck；这些不能替代实板测试。

## 12. 当前电源路径与保护开关

`bms/app/app_power.c` 的 DVC 分支有四条不同路径：

- 普通 suspend 由 `app_power_process()` 结合通信、采样及可靠充/放电电流判断；双向绝对值达到 200 mA 即禁止，允许时仍保持 200 ms 应用采样唤醒。实际功耗和保护响应需实测验证。
- `app_enter_power_off()`：检查 OTA/Flash/mux 等门禁，保存 State/事件，AFE shutdown 成功后才拉低 PC4/MCU_LDO，切断 MCU 电源。
- `app_enter_acc_sleep()`：PA0 高稳定 200 ms 后，检查通信/连接并保存状态；AFE shutdown 成功后保持 PC4 高，以 PA0 低电平 PAD 唤醒进入 deep sleep。与 PC4 断电路径不同。
- `app_enter_protective_sleep()`：低压或 AFE 异常计时到期后强制 deep sleep，保持 PC4 高；通信、OTA、保存或 AFE 失败不阻止，规则见 [保护性深睡](LOW_POWER_POLICY.md)。

普通 ACC/显式关机在保存或 shutdown 失败时保留请求并退避；保护性深睡不使用这些门禁。唤醒走重新初始化。物理供电、ACC 电平、负载检测与唤醒时序仍需实板闭环，见 [硬件验收](HARDWARE_VALIDATION.md)。

`DVC1124_SW_PROTECT_ENABLE` 控制软件电压/电流/压差，`DVC1124_SW_TEMP_PROTECT_ENABLE` 独立控制温度和必需 NTC 失效保护；第 9 节 SW/HW 表必须连同温度开关理解。生产全部保护开关按门禁开启；非生产组合只作受控验证。

## 13. Heater / Balance 当前策略

### 13.1 低温充电加热闭环

D008 当前产品策略使用 DVC 电流方向作为充电会话入口，而不是 PB1：

1. 普通低温静置时不因为温度本身提前 hard-off CHG；D008 软件温度保护的新故障本来就要求存在对应方向电流。
2. 检测到可靠充电电流后锁存 charge session。
3. 若低于 Heater start 或充电低温保护已进入 Third，则 Heater 先进入 `ARMING`。
4. `ARMING` 只禁止充电方向，不立即给加热膜上电；DVC common-port 将该方向映射为 `CHG=AUTO_DIODE, DSG=ON`。
5. 后续新鲜采样确认 `Ichg=0` 后才进入 `ACTIVE` 并拉高 PA1。
6. Heater Active 期间 `Ichg=0` 是预期行为，不得据此判定充电器移除。
7. 出现可靠放电电流时立即退出 charge session、关闭 Heater；DVC AUTO_DIODE 负责保留合法放电方向的续流恢复。
8. 达到 Heater stop 且充电低温故障已恢复后退出 Active，恢复普通充电仲裁。
9. AFE/NTC/参数/Heater 回路异常、OpenWire confirmed、严重高温/短路等安全条件失败时 Heater fail-safe OFF；低温保护本身不得成为 Heater hard fault。

仅靠电流无法区分“CHG 已主动关闭且充电器仍连接”和“充电器已拔出且系统完全空载”。因此拔充电器空载时 Heater 物理供电路径、是否可能由电池反供、以及必要的第二在位证据仍为 `TODO_VERIFY_HW`。

### 13.2 均衡独立参数

Balance 不再复用软件压差保护参数。CFG2 格式的 user payload（当前内部 journal schema 3） 独立保存：

- `balance_enable`
- `balance_start_mv`：可调均衡起始电压；
- `balance_start_delta_mv`：默认 50 mV；
- `balance_stop_delta_mv`：默认 30 mV，必须小于 start delta。

当前 LFP profile 的 `balance_start_mv` 为 3400 mV，仅作为当前固件业务默认值，量产仍需结合电芯、均衡电流、热测试签核。

### 13.3 均衡数据可信门禁

在计算 balance mask 前必须先确认单体数据可信：

- AFE snapshot / cell count 有效；
- 每节单体落在测量合理范围；
- 软件重算的 min/max/delta 与发布值一致；
- 单节相邻采样不存在超过 sanity limit 的异常跳变；
- 总压差没有进入明显异常/疑似断线区；
- 连续稳定样本达到确认时间；
- OpenWire active / suspected / confirmed 均禁止均衡；
- Heater ARMING / ACTIVE 均禁止均衡。

任何可信度失败先请求 Balance OFF，并置 `openwire_suspected`；条件允许时优先执行 DVC COW 断线诊断，确认无断线后重新累计可信样本才能恢复均衡。

### 13.4 DVC COW 断线诊断

DVC COW 开启后约 1 s 内 100 uA 下拉有效，因此诊断采样必须发生在 COW 仍为 1 的窗口内。当前实现约 200 ms 后使用新的 AFE snapshot 捕获诊断电压并主动清 COW；当前项目以诊断窗口内使用中的 cell 输入为 0 mV 标记对应 open bit。这是项目判据；DVC DS V1.1 p15 / RM V1.2 p22 没有规定它是断线的充分或必要条件，需厂家澄清及实板验证。采样完成资格问题见固定提交审查 DVC-02，不能把 snapshot generation 增加自动解释为新 ADC 完成。

本轮修复排除COW开始前的VADF，只在有效激励内获得新VADC事件后发布诊断。启动零点及正常电流分别检查CC2完成事件；原厂尚未规定多字节快照原子性和CAMZ完成上限，实板采样相位仍待验收。新写入边界、物理恢复和完整证据见 [修复记录](AFE_AUDIT_REMEDIATION.md)。

### 13.5 Balance 与 COV

Cell OVP 不被一刀切作为 Balance hard-block：在电压数据可信、温度/AFE/OpenWire 条件正常且 charge session 有效时，CHG 可因高单体停充，同时继续对高单体被动泄放，形成 `停充 -> 均衡 -> COV recover -> 继续充电` 的恢复闭环。CUV、总压异常、过流、温度故障、短路等仍阻止均衡。
