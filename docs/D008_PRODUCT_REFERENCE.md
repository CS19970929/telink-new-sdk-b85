# D008 产品硬件与固件配置基线

> 适用分支：`refactor/d008-common-bms-features`。
>
> 产品：**HS-D008 + TLSR8251F512ET32 + DVC1124-2**。
>
> 本文只记录当前源码、D008 原理图/BOM、DVC1124-2 Reference Manual V1.2 能支持的结论。资料不足的项目保持未确认，不用“常见 BMS 做法”补齐。

## 1. 证据优先级

1. HS-D008 原理图/BOM：板级连接、装配事实。
2. DVC1124-2 Reference Manual V1.2：寄存器、bit、量化、时序、芯片行为。
3. 当前分支源码：固件当前实际策略。
4. `references/vendor/dvc11xx_demo_v1.3/`：调用方式和交叉验证，仅作二级参考。

Demo 与官方手册冲突时以官方手册为准。

## 2. 产品身份

| 项目 | 当前配置/事实 |
|---|---|
| MCU | TLSR8251F512ET32 |
| AFE | DVC1124-2；源码默认 `DVC1124_MODEL_22` |
| 默认装配 | 24S LFP |
| 可选装配 | 20S NMC，由 `D008_PRODUCT_PROFILE` 编译选择 |
| AFE 总线 | I2C，PC0=SDA、PC1=SCL，100 kHz |
| DVC 地址 | `0x40` write / `0x41` read transfer address |
| Rsense | RS1..RS10 = 10 × 2 mΩ 并联，全部装配约 200 µΩ |
| 板载 NTC | NTC1/NTC2 标注 `SNC103B13435F0603E` |

`conf.h` 仍有历史 D3PRO 产品参数依赖，因此历史容量、部分 OV/UV/OC/温度默认值不能仅凭当前源码视为 D008 已签核产品参数。

## 3. MCU IO 基线

| MCU GPIO | D008 网络 | 当前用途 |
|---|---|---|
| PD7 | `MCU-AFE-EN` | DVC reset/enable |
| PA0 | `ACC-MCU` | key，低有效 |
| PB1 | `CHG-IN` | charger detect，低有效；同时用于低电平唤醒 |
| PC0 | `SDA` | DVC I2C SDA |
| PC1 | `SCL` | DVC I2C SCL |
| PC2 | `OWC-TX` | One-wire/UART 业务网络 |
| PC3 | `OWC-RX` | One-wire/UART 业务网络 |
| PA7 | `SWS-A7` | Telink SWS 下载/调试 |
| PB4/PB5/PB7/PD3 | SOC25/50/75/100 | SOC LED |
| PB6 | `LED_BLUE` | 蓝色 LED |

GP2/GP3 是否在所有 BOM 版本都实际装外部 NTC 仍需按具体 BOM 确认。

## 4. DVC GP / FET 拓扑

当前 `dvc1124_project_config.h`：

| DVC GP | 功能 | code |
|---|---|---:|
| GP1 | NTC / MOS NTC | 1 |
| GP2 | NTC | 1 |
| GP3 | NTC | 1 |
| GP4 | NTC / Battery NTC | 1 |
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

MCU 不直接 GPIO 控制 GP5/GP6。

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

`d008_product_profile.h` 只负责 24S LFP / 20S NMC 的装配串数和 chemistry/SOC identity，不再承载 DVC fail-safe 参数。

### 5.2 Flash 中保留的保护参数

仅保护参数保留运行时持久化：

1. MCU 软件保护：`g_tParam.protect`
   - First / Second / Third
   - Recover
   - Filter
   - OV/UV/OC/温度等软件保护参数
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
