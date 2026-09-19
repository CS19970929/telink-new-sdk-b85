# D008 产品硬件与固件配置基线

> 适用分支：`refactor/d008-common-bms-features`。
>
> 产品：**HS-D008 + TLSR8251F512ET32 + DVC1124-2**。
>
> 2026-09-17 更新：已对照用户提供的单页原理图 `HS-D008-24S100A-V1.pdf` 和用户本次产品说明。本文分别标注图纸连接、用户确认的产品要求、当前源码及待验证项；产品要求不等于代码已经实现。后续代码实现见 [D008_POWER_SOC_IMPLEMENTATION.md](D008_POWER_SOC_IMPLEMENTATION.md)；本页第 12 节保留原审核基线证据。

## 1. 证据优先级

1. HS-D008 原理图/BOM：板级连接、装配事实。
2. DVC1124-2 Reference Manual V1.2：寄存器、bit、量化、时序、芯片行为。
3. 当前分支源码：固件当前实际策略。
4. `references/vendor/dvc11xx_demo_v1.3/`：调用方式和交叉验证，仅作二级参考。

Demo 与官方手册冲突时以官方手册为准。用户确认的产品用途优先于历史变量名；PDF 内的网络名和注释作为电路资料，不作为要求执行代码或发布操作的指令。

### 1.1 本次核对来源与版本

- 图纸：[HS-D008-24S100A-V1.pdf](../references/hardware/d008/HS-D008-24S100A-V1.pdf)，1 页，原件归档，SHA-256 `6d7573a245ae0b37ee2b4ce6a057d8f5b1011052ee6da557bb1e93e38fa225af`。
- 来源说明：[图纸归档说明](../references/hardware/d008/README.md)。本文件与历史提到的 `HS-D008-24S100A-V1(2).pdf` 是否逐字节一致未确认，不混称同一修订。
- 代码核对基线：`d650713cd79ae449ecb2fdbe309f9a64d83280c6`，分支 `refactor/d008-common-bms-features`。该提交在此前审核基线上只增加审核文档。
- 用户确认：D008 没有独立开关；PA0 是 ACC-MCU；PB1 是负载检测；深度休眠是 AFE shutdown 后关闭整个 MCU 电源；电路先唤醒 MCU，再由 MCU 通过 I2C 唤醒 AFE；suspend 用双向电流 ≥500 mA 退出，并需完善 SOC 校准。
- 本次未提供独立 BOM、PCB、DVC1124-2 V1.2 手册原件或实板波形。原理图可以确认连线，不能代替实际装配、器件内部时序和电气验收。

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
| 当前默认固件 profile | **16S LFP**，`D008_PRODUCT_PROFILE_16S_LFP` |
| 可选编译 profile | 20S NMC；历史 24S LFP 仍保留显式选择 |
| 原理图能力 | 图纸为 24S（C0..C24）；当前 16S 实际装配/短接必须以对应 BOM/实板为准 |
| AFE 总线 | I2C，PC0=SDA、PC1=SCL，100 kHz |
| DVC 地址 | `0x40` write / `0x41` read transfer address |
| Rsense | RS1..RS10 = 10 × 2 mΩ 并联，全部装配约 200 µΩ |
| 板载 NTC | NTC1/NTC2 标注 `SNC103B13435F0603E` |

`conf.h` 仍有历史 D3PRO 产品参数依赖，因此历史容量、部分 OV/UV/OC/温度默认值不能仅凭当前源码视为 D008 已签核产品参数。

## 3. MCU IO 基线与代码核对

**结论：下面已命名 GPIO 的引脚号与本次图纸相符；原差异主要是业务语义和电源控制流程，当前实现已按本页约束接入（电气验收仍未完成）。D008 没有独立开关，不能继续从 `SW_PIN` 名字推导“关钥匙后关机”。**

| MCU GPIO / U1 引脚 | 原理图网络 | 当前代码符号/用途 | 本次确认与维护要求 |
|---|---|---|---|
| PD7 / 2 | `MCU-AFE-EN` | `AFE1_PRO_EN_PIN`，AFE init 拉高 | 参与 AFE 供电/接口使能；不是 MCU 总电源开关，也不是 DVC 独立 RESET 引脚 |
| PA0 / 3 | `ACC-MCU` | `ACC_MCU_PIN`，低有效运行开关 | 用户最新授权独立ACC深睡眠，详见 D008_ACC_SLEEP.md |
| PB1 / 6 | `CHG-IN` | `CHG_IN_PIN`，保留输入，无业务读取/PAD 唤醒 | 实际为负载检测。Q40 导通时输出低；不等于已验证所有负载场景的逻辑。暂不实现负载判定、去抖或策略，也不能据此证明正在充电 |
| PC4 / 24 | `MCU-LDO` | `MCU_LDO_PIN`，启动保持高、关机事务最后拉低 | 已接入控制路径；实测保持时点、掉电与复电时序仍为 TODO_VERIFY_HW |
| PC0 / 20 | `SDA` | DVC I2C SDA | 经 R96=100 Ω；与图纸一致 |
| PC1 / 21 | `SCL` | DVC I2C SCL | 经 R95=100 Ω；与图纸一致；SDA/SCL 各有 4.7 kΩ 上拉至 MCU 3V3 |
| PC2 / 22 | `OWC-TX` | `OWC_TX_PIN` | 图纸同时接 UART/单线转换路径；复用由 bus mux 管理 |
| PC3 / 23 | `OWC-RX` | `OWC_RX_PIN` | 同上，不移植 D011 RS485/SPI 网络 |
| PA1 / 4 | `MCC-EN-HT` | `HEATER_EN_PIN`，启动低、加热时高 | 加热控制链，图纸连线对应；热控制参数仍须验证 |
| PD4 / 1 | `MCC-EN-RF` | `RF_EN_PIN`，启动低、熔断请求高 | F1 相关支路；不能按 RF 名称理解成 BLE 射频供电 |
| PA7 / 5 | `SWS-A7` | SWS 下载/调试 | 保留 SDK 调试用途 |
| PB4 / 14、PB5 / 15、PB7 / 17、PD3 / 32 | `SOC25/50/75/100` | 对应 SOC LED 宏 | 网络映射一致；外接显示负载、极性仍按实物核对 |
| PB6 / 16 | `BLUE` | `LED_BLUE_PIN` | 经 R165=3.3 kΩ 接 LED1 至 B−；代码别名不改变原图网络名 |

历史“暂不写逻辑”约束已由最新ACC开关授权部分替代：ACC高电平进入独立深睡眠，PC4不拉低，低电平唤醒；PB1只按已授权的负载移除/电流保护恢复策略使用。当前已剥离错误的 key/charger 业务读取；正常产品请求为 CHG+DSG ON，由 guard/保护/输出授权仲裁。**PB1 不提供自动加热的充电源资格。D008 以可靠充电电流作为 charger-session 的进入事件，进入后因预热主动关闭 CHG 导致的零电流不会清除 session，可靠放电电流会立即退出 session 并关闭 Heater。**

## 4. DVC GP / FET 拓扑

当前 `dvc1124_project_config.h`：

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
dvc1124_project_config.h
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

`d008_product_profile.h` 只负责 16S LFP / 20S NMC / 24S LFP 的装配串数和 chemistry/SOC identity，不再承载 DVC fail-safe 参数。当前分支默认选择 16S LFP；24S 图纸事实与当前默认装配必须明确区分。

### 5.2 Flash 中保留的保护参数

仅保护参数保留运行时持久化：

1. MCU 软件保护：`g_tParam.protect`
   - First / Second / Third
   - Recover
   - Filter
   - OV/UV/OC/温度等软件保护参数

软件保护记录在启动时必须通过完整参数校验。校验失败时保留原 Flash
内容用于诊断，但 `bms_protection_params_valid()` 保持 false，公共 AFE
输出门禁同时禁止 CHG/DSG；只有完整有效参数成功持久化后才解除该门禁。
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
g_tParam.protect
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

## 11. 实板验证重点

固定配置或 FET 策略修改后至少验证：

1. 上电后读回固定配置寄存器，确认没有旧 Flash 覆盖。
2. CHG protection -> 放电：CHG AUTO_DIODE 自动恢复后持续导通，不得出现 200 ms 周期关断脉冲。
3. DSG protection -> 充电：同理验证 DSG。
4. I2C 正常时确认 R77/R53/R54/R66 符合当前 compile-time policy。
5. 真正停止 MCU↔DVC I2C 通信，约 4 s 后验证 CHG/DSG driver 均被 DVC 自主关闭。
6. `SW/HW = 1/0、0/1、0/0、1/1` 均需 TC32 clean build；production 最终使用 `1/1`。
7. 任何 safety change 继续通过 source-order、Host contracts、firmware check、MAP、verify、cppcheck；这些不能替代实板测试。

## 12. 2026-09-17 原始代码差异清单（历史固定 SHA）

下表描述 d650713 基线，保留证据而不覆盖历史。当前实现状态以第 13 节及实现说明为准，表中的“当前/后续”均指当时审核时点。

| 编号 | 当前代码证据 | 与确认产品定义的差异 | 后续最小工作边界 |
|---|---|---|---|
| IO-01 | [conf.h:21](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/conf.h#L21)；[app.c:66](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L66)；[app.c:258](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L258)；[app.c:556](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L556) | _DI_SWITCH_SYS_ONOFF 已定义；PA0 被当成 key，参与 MOS 请求和无 key/charger 3 s 后深睡眠 | 按 ACC 输入重新梳理旧依赖；暂不实现 ACC 开关策略，不能只改宏名就视为完成 |
| IO-02 | [bms_board.c:33](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_board.c#L33)；[dvc1124_feature_backend.c:68](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124_feature_backend.c#L68)；[bms_features.c:40](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_features.c#L40)；[app.c:668](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L668) | PB1 被当成 charger，影响 MOS、加热资格、suspend 禁止和 PAD 唤醒 | 先剥离错误充电器语义；负载检测新逻辑保持待实现，不能将 PB1 低直接当充电 |
| PM-01 | [conf.h:218](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/conf.h#L218)；[app.c:145](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L145)；[dvc1124.c:1244](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124.c#L1244) | MCU_LDO_PIN 无业务引用；现路径为 AFE ENTER_SLEEP + cpu_sleep_wakeup(DEEPSLEEP_MODE)，不是 AFE shutdown + MCU 断电 | 建立受控关机事务，shutdown 成功后最后拉低 PC4；失败分支不得无条件断电 |
| PM-02 | [dvc1124_config_store.c:45](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124_config_store.c#L45)；[dvc1124_config_store.c:56](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124_config_store.c#L56)；[dvc1124_config_store.c:238](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124_config_store.c#L238)；[bms_afe_guard.c:350](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_afe_guard.c#L350) | 已有 PD7 使能、I2C 唤醒脉冲、reset/reinit 和 guard 台架 shutdown/wake；尚未接入产品 PC4 断电流程 | 复用驱动生命周期；MCU 断电后从冷启动恢复，不假定 RAM 中 test_shutdown_hold 或 Requested 保留 |
| PM-03 | [app.c:643](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L643)；[app.c:668](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L668)；[dvc1124.c:1308](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124.c#L1308) | suspend 退出仍是 PB1 低、OWC 忙、任意非零放电报告、OTA；没有双向 ≥500 mA 判定 | 用有效且新鲜的电流判断两方向，保持通信/OTA/故障的独立约束；不从 PB1 推定方向 |
| SOC-01 | [SocEnhance.c:13](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L13)；[SocEnhance.c:657](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L657)；[SocEnhance.c:722](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L722)；[app.c:990](https://github.com/CS19970929/telink-new-sdk-b85/blob/d650713cd79ae449ecb2fdbe309f9a64d83280c6/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L990) | SOC 使用固定 200 ms 调用计数；电压范围检查不代表 AFE 样本新鲜；当前无 suspend 专属资格 | 按 SOC.md 第 10 节实现时间、样本资格和断电边界；不能把 sleep 标志当静置证据 |

### 12.1 suspend 与 MCU 断电是不同状态

| 状态 | MCU / AFE | 退出或恢复 | SOC 边界 |
|---|---|---|---|
| Active | MCU 执行业务，AFE 提供有效测量 | 满足后续定义的静置/通信条件才允许 suspend | 正常积分、保护与端点处理 |
| Suspend | MCU 仍供电，使用 SDK suspend；AFE 保持能提供测量的配置 | 任一方向电流 ≥500 mA 退出；通信/OTA/故障按已有安全边界处理 | 只有合格样本和可靠时间才允许积分/静置资格累计 |
| Shutdown prepare | MCU 仍供电，完成必要持久化及输出控制，然后 AFE shutdown | 失败则留在受控故障/恢复路径 | 在失去测量与电源前完成所需 State 提交 |
| MCU power off | AFE 已 shutdown，PC4 低关闭 MCU 电源，MCU 无执行能力 | 外部硬件电路恢复 MCU 供电 | 不能读取电流、计时、校准或继续写 Flash |
| Cold boot / AFE recovery | 电路先唤醒 MCU，MCU 再走 I2C 唤醒和 AFE 初始化 | 固定配置、硬件保护、有效样本资格和输出门控都满足后才能进入正常业务 | 恢复已保存状态，重新累计静置资格；不推算未知断电时长 |

SDK 的短暂定时唤醒用于重新采样，不等于产品已退出 suspend 状态。周期、最坏响应时间、进入延时和迟滞尚未由用户给定，保持待确认；本轮不擅自增加阈值。

### 12.2 深度休眠目标时序（用户确认，尚未接入产品逻辑）

```text
满足已确认关机条件（不得继续以不存在的独立开关推导）
 -> 结束/阻止冲突的 OTA、参数事务与 Flash 操作
 -> 必要 State/Event 保存成功，冻结新的业务输出请求
 -> 受控关闭加热/均衡与 CHG/DSG，核对关闭结果
 -> 通过 guard 管理的 AFE shutdown 命令
 -> 停止所有会重新唤醒 AFE 的 I2C/诊断/重试
 -> MCU_LDO_PIN / PC4 拉低（最后一步）
 -> MCU 整体断电
 -> 外部硬件电路恢复 MCU 电源
 -> 冷启动与电源保持，安全 GPIO 初值
 -> PD7 接口使能，PC0/PC1 I2C 唤醒 AFE
 -> reset/init，重新应用固定配置及持久化 AFE hardware profile
 -> 配置一致且新样本/保护资格满足后，恢复允许的业务输出
```

本图没有规定新的关机延时/电压阈值。既有协议请求及保护结果仍参与最终输出仲裁；ACC/PB1 暂不增加业务含义。

- 当前 `DVC1124_CST_ENTER_SHUTDOWN` 与 `ENTER_SLEEP` 是不同命令。已有 backend 在 shutdown 后不读回 STATUS，避免重新通信；不能强行加“shutdown 后读回成功”验收。
- 当前 I2C 唤醒实现释放 SCL、拉低 SDA 1000 µs，再释放 SDA；代码注释记载 SCL 比 SDA 高至少 2 V、超过 50 µs。这里仅记录现有实现，手册原件未在本次提供，幅值、时序及 PD7 配合仍需官方资料与实测确认。
- PC4 拉低后不能再安排依赖 MCU 执行的 Flash/I2C/日志步骤。PC4 开机保持时点、掉电是否被调试器/串口/I2C 反向供电、外部电路的具体唤醒条件与最短脉宽保持 `TODO_VERIFY_HW`。
- 本版图纸可见独立 MCU 电源控制与负载相关电路，但“哪些外部动作在所有状态都能恢复供电”不能仅由网络名判定；不得把 PB1 PAD 唤醒等同于整机断电唤醒。

### 12.3 ≥500 mA 退出 suspend 的判定边界

- “500 mA 以上”按包含边界记录：有效测量 `current_ma >= 500`（源码正方向为放电）或 `current_ma <= -500`（负方向为充电）；实板校准仍需确认正负方向。
- 优先使用带有效性/新鲜度的 mA 测量。公共 `bms_afe_aux_measurements_t` 现传递精确 `current_ma` 与 `sample_tick_32k`；应用必须经 guard 读取，不直接依赖私有寄存器。
- 当前 `u16Ichg/u16IDischg` 单位 0.1 A，先截断再比较不能验证 499/500/501 mA 的精确边界，更不能把采样失败时被清零的电流当静置。
- 201..499 mA 即使尚不要求退出 suspend，也不能自动进入 OCV 静置校准。SOC 积分死区与 suspend 退出门槛分别维护，详见 [SOC.md](SOC.md)。
- 当前 AFE current-wake engine 关闭，interrupt mask 为 0xFF；本要求不等于授权直接打开 AFE current-wake 或改变固定配置。须先用真实调度确认周期采样/唤醒能达到响应预算，并同时满足 4 s I2C watchdog。
- 无效、陈旧、读写故障样本不能保持“已确认静置”资格；后续设计应转入受控采样/故障处理。不能为了省电关闭保护或掩盖通信故障。

## 13. 文档交付与后续验证

图纸归档提交之后，用户授权继续修改代码。当前已实现 IO 语义纠正、AFE shutdown→PC4 断电、周期采样下双向 500 mA suspend 门槛及有效时间 SOC；详见 [实现与验证说明](D008_POWER_SOC_IMPLEMENTATION.md)。协议、保护参数、Flash 布局和 source order 未变；没有新增 ACC/负载业务逻辑。软件路径完成不等于实板电气流程已验证。

[前次全模块审核](D008_FULL_MODULE_AUDIT_2026-09-17.md)保留其固定 SHA 的代码证据；本次图纸补足了部分连接证据，修正历史 charger/key 用途，但不自动关闭原审核的软件缺陷或实板未决项。实测统一记录在 [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md)，SOC 算法要求统一记录在 [SOC.md](SOC.md)，避免产生第二套 IO 真源。

## 2026-09-17 后续授权：ACC 独立休眠

本页此前“不新增ACC逻辑”属于历史基线，已由用户新要求替代。PA0低电平运行；高电平稳定200ms后完成保存、AFE shutdown，再以PA0低电平为PAD唤醒源进入MCU DEEPSLEEP_MODE。PC4保持高，与既有PC4断电路径分开。实现/失败处理见 [D008_ACC_SLEEP.md](D008_ACC_SLEEP.md)，实板仍待验证。

## 2026-09-17 电流保护恢复更新

用户最新授权替代此前“PB1 暂不实现负载检测”和“SCD 不自动清除”的限制：PB1 低=负载在、高=负载移除，仅在有效 DSGF=0 时判定；软件三级/硬件放电过流及短路在负载移除或可靠充电后恢复，充电过流等待 30 s。保留单侧 AUTO_DIODE 续流；运行期间锁存不因 AFE reinit 丢失，MCU 复位按原启动策略。见 [恢复实现](D008_CURRENT_RECOVERY.md)。实板仍为 TODO_VERIFY_HW。

## 软件温度保护开关更新

`DVC1124_SW_PROTECT_ENABLE` 仅控制软件电压、电流及压差；新增默认开启的 `DVC1124_SW_TEMP_PROTECT_ENABLE` 独立控制电池温度、MOS温度及必需NTC失效保护，不能用HW开关替代外部温度保护。旧SW/HW四组合说明按此更新；详情见 [实现说明](D008_PROTECTION_GROUPS.md)。

## 14. 2026-09-19 Heater / Balance 产品策略

### 14.1 低温充电加热闭环

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

### 14.2 均衡独立参数

Balance 不再复用软件压差保护参数。Config schema 4 独立保存：

- `balance_enable`
- `balance_start_mv`：可调均衡起始电压；
- `balance_start_delta_mv`：默认 50 mV；
- `balance_stop_delta_mv`：默认 30 mV，必须小于 start delta。

当前默认 16S LFP 的 `balance_start_mv` 为 3400 mV，仅作为当前固件业务默认值，量产仍需结合电芯、均衡电流、热测试签核。

### 14.3 均衡数据可信门禁

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

### 14.4 DVC COW 断线诊断

DVC COW 开启后约 1 s 内 100 uA 下拉有效，因此诊断采样必须发生在 COW 仍为 1 的窗口内。当前实现约 200 ms 后使用新的 AFE snapshot 捕获诊断电压并主动清 COW；使用官方流程中的确定性判据：诊断窗口内使用中的 cell 输入为 0 mV 时标记对应 open bit，不另外猜测非零阈值。

### 14.5 Balance 与 COV

Cell OVP 不被一刀切作为 Balance hard-block：在电压数据可信、温度/AFE/OpenWire 条件正常且 charge session 有效时，CHG 可因高单体停充，同时继续对高单体被动泄放，形成 `停充 -> 均衡 -> COV recover -> 继续充电` 的恢复闭环。CUV、总压异常、过流、温度故障、短路等仍阻止均衡。
