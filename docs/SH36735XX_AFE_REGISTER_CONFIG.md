# SH36735XX / D011 AFE 寄存器配置与保护逻辑

> 依据：`SH36735XX CV1.0A`。适用于 SH3673510 / SH3673514 / SH3673517 / SH3673520 的共用寄存器与 SPI 协议；不同型号主要区别为支持串数。本文描述 HS-D011 / SH3673510 当前固件配置。
>
> 代码单一事实源：
> - 寄存器地址、每一 bit/field、复位值：`sh3673520_reg.h`
> - D011 每一 bit 的产品配置：`sh3673510_project_config.h`
> - 寄存器写入/校验、保护阈值编码：`sh3673510_control.c`
> - MCU 软件保护/恢复/FET 策略：`sh3673510_bms.c`

## 1. 配置原则

1. 不依赖 AFE 上电复位值作为“隐藏配置”。D011 使用的静态寄存器均显式写入并回读校验。
2. 每一个静态 bit/field 都在 `sh3673510_project_config.h` 用独立宏命名，后续修改某一位不需要改 SPI 事务代码。
3. `0x49~0x54` 保护阈值不是固定常量，而是从 `g_tParam.protect` 运行参数编码后写入 AFE。
4. `SCONF6(0x45)` 最后写入：先把 OV/UV/OCD/SC/OCC/温度阈值配置正确，再开启硬件保护，避免短暂使用错误复位阈值。
5. AFE 硬件保护和 MCU 软件保护并行工作。硬件负责快速、独立切 MOS；软件负责三级策略、恢复回差、故障协调和系统级 fail-safe。

## 2. 当前静态寄存器完整配置

### 2.1 SCONF1 — 0x40 — 当前 0x00

手册 10.2.1。整个 8-bit 是模式命令，不应当作 8 个独立功能位。

| Bits | 名称 | 手册含义 | 当前值 |
|---|---|---|---|
| 7:0 | PIN[7:0] | `0x00` Normal；`0x55` IDLE；`0xAA` SLEEP；`0x33` Powerdown（Powerdown 还要求 PD_CTL 顺序条件） | `0x00` Normal |

### 2.2 SCONF2 — 0x41 — 当前 0x50

| Bit | 名称 | 手册含义 | 当前 |
|---:|---|---|---:|
| 7 | LTCLR | 1 时允许用 W0C 清 FLAG1/FLAG2 锁存标志；正常配置应为 0 | 0 |
| 6 | PD_EN | 低电芯自动进入 Powerdown 使能 | 1 |
| 5 | PD_CTL | MCU Powerdown 控制许可；与 SCONF1=0x33 必须连续执行 | 0 |
| 4 | PUMP_EN | Charge Pump | 1 |
| 3 | PDSG_CTL | PDSGMOS=0 时 MCU 强制控制预放电 | 0 |
| 2 | PDSGMOS | 预放电自动/强制模式选择 | 0 |
| 1 | DSGMOS | 放电 MOS 命令位 | 启动 0，运行时动态 |
| 0 | CHGMOS | 充电 MOS 命令位 | 启动 0，运行时动态 |

`PD_EN=1` 时，手册定义任一电芯低于 `VPD = VUV - 200mV` 且持续约 `tPD_UV=62.72s typ.`，AFE 可自主进入 Powerdown；Powerdown 仅保留充电器唤醒。

### 2.3 SCONF3 — 0x42 — 当前 0x44

| Bit(s) | 名称 | 当前 | 含义 |
|---|---|---:|---|
| 7 | Reserved | 0 | 保留 |
| 6 | CGR_WK | 1 | Charger wake 开启 |
| 5:4 | LD_WK[1:0] | `00` | Load wake 关闭 |
| 3:2 | CRLD_EN[1:0] | `01` | C+ 电压采集模式 |
| 1 | OWD_EN | 0 | 断线检测功能关闭 |
| 0 | OWD_TRG | 0 | 断线检测触发命令，静态配置保持 0 |

### 2.4 SCONF4 — 0x43 — 当前 0x6A

| Bit(s) | 名称 | 当前 | 含义 |
|---|---|---:|---|
| 7:5 | PDSGT[2:0] | `011` | 预放电开启时间 0.49s |
| 4:0 | CN[4:0] | `01010` | 10 串 |

SH3673510 支持 4~10S；SH3673520 同一字段可配置到 20S。

### 2.5 SCONF5 — 0x44 — 当前 0x3C

| Bit(s) | 名称 | 当前 | 含义 |
|---|---|---:|---|
| 7:6 | Reserved | `00` | 保留 |
| 5 | MOS_EN | 1 | 开启 AFE 的反向电流强制 MOS 逻辑；例如充过流后出现放电方向时可强制开充电 MOS，放过流后出现充电方向时可强制开放电 MOS |
| 4 | OCC_EN | 1 | 充电过流硬件保护使能 |
| 3 | CADC_EN | 1 | CADC 开启 |
| 2 | WDT_EN | 1 | AFE 看门狗开启 |
| 1:0 | WDT[1:0] | `00` | 最长档，约 32.34s |

有效 SPI 通讯会刷新 AFE 看门狗。手册规定 WDT 溢出后产生标志/ALARM，随后关闭 MOS；若 WDT_FLG 长时间未清，会进一步进入 Powerdown。

### 2.6 SCONF6 — 0x45 — 当前 0x3F

| Bit | 名称 | 当前 | 当前保护归属 |
|---:|---|---:|---|
| 7 | TS4_EN | 0 | TS4 充放电 MOS 温度由 MCU 软件独立保护 |
| 6 | TS3_EN | 0 | TS3 不参加 AFE 共用温度保护；MCU 用 TS3 做加热 MOS 独立可逆过温截止 |
| 5 | TS2_EN | 1 | 电池温度 2：AFE 硬件温度保护 + MCU 软件温度保护 |
| 4 | TS1_EN | 1 | 电池温度 1：AFE 硬件温度保护 + MCU 软件温度保护 |
| 3 | SC_EN | 1 | AFE 硬件短路保护 |
| 2 | OCD_EN | 1 | AFE 硬件放电过流保护 |
| 1 | UV_EN | 1 | AFE 硬件单体欠压保护 |
| 0 | OV_EN | 1 | AFE 硬件单体过压保护 |

注意：`TSn_EN=0` 只禁止该通道参与 AFE 温度保护，不禁止 VADC 温度采样。

### 2.7 SCONF7 — 0x46 — 当前 0x04

| Bit(s) | 名称 | 当前 | 含义 |
|---|---|---:|---|
| 7 | Reserved | 0 | 保留 |
| 6 | RLD | 0 | Load detect 上拉电流 60uA（1 为 500uA） |
| 5:4 | CADCT[1:0] | `00` | IDLE 模式 CADC 周期 4s；Normal 模式 CADC 固定 250ms |
| 3 | Reserved | 0 | 保留 |
| 2:0 | CDV[2:0] | `100` | 充/放状态检测阈值档，当前沿用手册复位档 742.5uV |

### 2.8 OWV/ALARMH — 0x47 — 当前 0x57

| Bit(s) | 名称 | 当前 | 含义 |
|---|---|---:|---|
| 7:4 | OWV[3:0] | `0101` | 断线检测阈值 960mV |
| 3 | LOADON_INT | 0 | Load-on ALARM 脉冲关闭 |
| 2 | LOADOFF_INT | 1 | Load-off ALARM 脉冲开启 |
| 1 | VADC_INT | 1 | VADC 完成 ALARM 脉冲开启 |
| 0 | CADC_INT | 1 | CADC 完成 ALARM 脉冲开启 |

### 2.9 ALARML — 0x48 — 当前 0xFF

| Bit | 名称 | 当前 | 含义 |
|---:|---|---:|---|
| 7 | WK_INT | 1 | Wake 事件 ALARM |
| 6 | WDT_INT | 1 | WDT 事件 ALARM |
| 5 | OWD_INT | 1 | 断线检测事件 ALARM |
| 4 | TEMP_INT | 1 | 温度保护事件 ALARM |
| 3 | OCC_INT | 1 | 充电过流事件 ALARM |
| 2 | OCD_INT | 1 | OCD1/OCD2/SC 事件 ALARM |
| 1 | UV_INT | 1 | 欠压事件 ALARM |
| 0 | OV_INT | 1 | 过压事件 ALARM |

ALARM 只是中断/通知输出，真正硬件切 MOS 不依赖 MCU 是否及时处理中断。

## 3. 动态保护寄存器 0x49~0x54

这些寄存器由当前持久化参数 `g_tParam.protect` 计算并写入，因此“当前值”应理解为运行参数的 AFE 量化结果，而不是永远固定的十六进制值。

| 地址 | 寄存器 | 每一 bit/field | 当前参数映射 |
|---|---|---|---|
| 0x49 | OVT/OVH | b7 Reserved；b6:4 OVT；b3:2 Reserved；b1:0 OV[9:8] | 单体 OV 第三级 + OV filter |
| 0x4A | OVL | b7:0 OV[7:0] | 单体 OV 第三级，5mV/LSB |
| 0x4B | UVT/UVH | b7 Reserved；b6:4 UVT；b3:2 Reserved；b1:0 UV[9:8] | 单体 UV 第三级 + UV filter |
| 0x4C | UVL | b7:0 UV[7:0] | 单体 UV 第三级，5mV/LSB |
| 0x4D | OCD1V/OCD1T | b7 Reserved；b6:4 OCD1T；b3:0 OCD1V | 放电过流第一级；5mV/档 |
| 0x4E | OCD2V/OCD2T | b7:4 OCD2T；b3:0 OCD2V | 放电过流第二级；10mV/档，延时 25ms/档 |
| 0x4F | SCV/SCT | b7:6 Reserved；b5:4 SCV；b3:0 SCT | 产品固定短路备份：SCV=2×OCD2，SCT=256us |
| 0x50 | OCCV/OCCT | b7:5 OCCT；b4:0 OCCV | 充电过流第一级；1.375mV/档 |
| 0x51 | OTC | b7:0 OTC | 充电高温第三级 |
| 0x52 | OTD | b7:0 OTD | 放电高温第三级 |
| 0x53 | UTC | b7:0 UTC | 充电低温第三级 |
| 0x54 | UTD | b7:0 UTD | 放电低温第三级 |

温度寄存器编码按手册第 7.13.2.3 的 NTC 阈值公式转换。D011 实装 NTC 为 10K。

### 3.1 当前默认参数量化后的硬件备份值

> 若 Flash 中已有客户参数，则以客户参数重新计算，下面只是当前默认参数的结果。

| 保护 | 软件默认 | AFE 实际备份 |
|---|---:|---:|
| 单体 OV | 3750mV，第三级；软件 1s filter | 3750mV；AFE 延时量化为约 2.03s |
| 单体 UV | 3000mV，第三级；软件 10s filter | 3000mV；AFE 延时量化为约 10.01s |
| 放电 OC1 | 软件一级 10A | Rsense=250uOhm，AFE 最低 5mV，因此实际约 20A；延时约 140ms |
| 放电 OC2 | 软件二级 15A | AFE 最低 10mV，因此实际约 40A；延时约 100ms |
| 短路 | - | 2×OCD2 = 20mV，约 80A；256us |
| 充电 OC | 软件一级 10A | 2.75mV，约 11A；延时约 140ms |
| 充电高温 | 55℃ 第三级 | 约 55℃；AFE 温度保护延时固定约 2.94s typ. |
| 充电低温 | 0℃ 第三级 | 约 0℃；AFE 温度保护延时固定约 2.94s typ. |
| 放电高温 | 60℃ 第三级 | 约 60℃；AFE 温度保护延时固定约 2.94s typ. |
| 放电低温 | -20℃ 第三级 | 约 -20℃；AFE 温度保护延时固定约 2.94s typ. |

## 4. BALANCE / FLAG / STATUS 每一 bit

### 4.1 BALANCEH/M/L — 0x55~0x57

- 0x55 BALANCEH: b7:4 Reserved，b3:0 = CB20..CB17
- 0x56 BALANCEM: b7:0 = CB16..CB9
- 0x57 BALANCEL: b7:0 = CB8..CB1

D011 只使用 CB1..CB10。Normal 模式连续均衡约 30.38s 后 AFE 会自动清零 BALANCE，固件因此对非零 mask 周期刷新。

### 4.2 FLAG1 — 0x58

| Bit | 名称 | 说明 |
|---:|---|---|
| 7 | RST1_FLG | 系统复位标志 |
| 6 | WK_FLG | 唤醒标志 |
| 5 | OCC_FLG | 充电过流 |
| 4 | SC_FLG | 短路 |
| 3 | OCD2_FLG | 放电过流 2 |
| 2 | OCD1_FLG | 放电过流 1 |
| 1 | UV_FLG | 单体欠压 |
| 0 | OV_FLG | 单体过压 |

FLAG1 为锁存标志。清除流程必须先置 `SCONF2.LTCLR=1`，再以 W0C 写 FLAG，最后恢复 LTCLR。

### 4.3 FLAG2 — 0x59

| Bit | 名称 | 说明 |
|---:|---|---|
| 7 | OTD_FLG | 放电高温 |
| 6 | UTD_FLG | 放电低温 |
| 5 | OTC_FLG | 充电高温 |
| 4 | UTC_FLG | 充电低温 |
| 3 | RST2_FLG | LDO2/SPI 相关复位 |
| 2 | WDT_FLG | 看门狗 |
| 1 | VADC_FLG | VADC 完成；读 FLAG2 自动清 |
| 0 | CADC_FLG | CADC 完成；读 FLAG2 自动清 |

### 4.4 FLAG3 — 0x5A（只读）

- b7:2 Reserved
- b1 OWD_IND：奇/偶断线检测结果索引
- b0 OWD_FLG：断线检测转换状态

### 4.5 BSTATUS1 — 0x5B（只读）

- b7 Reserved
- b6 E2P_ERR
- b5 HDSG_FET
- b4 HCHG_FET
- b3 Reserved
- b2 PDSG_FET
- b1 DSG_FET
- b0 CHG_FET

### 4.6 BSTATUS2 — 0x5C（只读）

- b7 CHGING
- b6 DSGING
- b5 SLEEP
- b4 IDLE
- b3 BAL
- b2 Reserved
- b1 LOADON
- b0 LOADOFF

## 5. 当前保护不是“全部由 AFE 硬件完成”

当前架构是 **AFE 硬件保护 + MCU 软件保护双层并行**。

| 保护项 | AFE 硬件 | MCU 软件 | 说明 |
|---|---|---|---|
| 单体 OV | 是 | 是，三级 | 硬件用第三级作为独立备份；软件第三级也会关充电 MOS |
| 单体 UV | 是 | 是，三级 | 硬件用第三级作为独立备份；软件第三级也会关放电 MOS |
| Pack 总压 OV/UV | 否 | 是，三级 | SH36735xx 没有对应独立 Pack 总压保护寄存器 |
| 充电过流 | 是 OCC | 是，三级 | 硬件 OCC 取软件一级做快速备份；软件第三级关充电 MOS |
| 放电过流 | 是 OCD1/OCD2 | 是，三级 | OCD1/OCD2 分别取软件一/二级；软件第三级关放电 MOS |
| 短路 | 是 | 是，锁存/恢复监督 | AFE 256us 级切断；MCU 不重新判定短路阈值，只监督解除 |
| 电池充/放高低温 TS1/TS2 | 是 | 是，三级 | AFE 用第三级；软件提供三级告警/保护和恢复回差 |
| MOS 温度 TS4 | 否（TS4_EN=0） | 是，三级 | 阈值与电池温度不同，所以不让 AFE 共用 OTC/OTD/UTC/UTD 去保护 TS4 |
| 加热 MOS 温度 TS3 | 否 | 是，可逆截止 | TS3=10K；当前复用 MOS Third/Rcv 作加热 MOS 过温/恢复阈值，只关 PB4，加热保险丝 PB5 永不自动触发 |
| NTC 断线/温度无效 | 否 | 是 | TEMP_BREAK，fail-safe 关相关输出 |
| 单体压差 | 否 | 是，三级 | 当前只形成软件故障/历史；还被均衡逻辑复用了一级阈值 |
| SOC 低 | 否 | 参数存在，但当前 SH3673510 adapter 未执行 | 需要单独接入 SOC 状态机/输出策略 |
| SPI/AFE 通讯故障 | 否 | 是 | 立即 output inhibit、关 heater、balance、CHG/DSG FET |
| AFE 内部过温 | 是 | 状态间接感知 | AFE 可自主进入 Powerdown |
| WDT | 是 | MCU负责通讯/清标志 | AFE 自主执行后果 |
| 低电芯 Powerdown | 是，PD_EN=1 | MCU不参与阈值判断 | Vcell < VUV-200mV，约 62.72s 后 AFE自主 Powerdown |

## 6. MCU 软件保护逻辑

采样周期为 200ms。每个软件保护都有三级状态，每一级使用自己的 trip threshold，但共用该保护项的 recovery threshold 和 filter 时间。

### 6.1 触发

`filter_update()` 的逻辑：

- 高方向故障（OV/OC/OT/压差）：`value >= trip`
- 低方向故障（UV/UT）：`value <= trip`
- 连续达到 `ceil(filter_ms / 200ms)` 个采样后进入 active
- 违反条件中断时，未触发前计数会缓慢回退，而不是一次清零

### 6.2 恢复

已 active 后必须进入恢复区并连续满足同样的 filter 时间：

- 高方向故障：`value <= recovery`
- 低方向故障：`value >= recovery`

满足后才清软件 fault。

### 6.3 哪一级真正关 MOS

当前只用 **第三级软件 fault** 直接做输出禁止：

充电 MOS 阻断：
- 单体 OV 第三级
- Pack OV 第三级
- 充电 OC 第三级
- 充电 OT/UT 第三级
- MOS OT 第三级
- 温度采样失效
- 加热状态 active

放电 MOS 阻断：
- 单体 UV 第三级
- Pack UV 第三级
- 放电 OC 第三级
- 放电 OT/UT 第三级
- MOS OT 第三级
- 短路锁存
- 温度采样失效

第一/第二级主要用于分级 fault/status/history；当前并不直接关 MOS。

## 7. AFE 硬件保护的恢复并不是“电压回到恢复阈值自动恢复”

这是当前设计最重要的一点。

SH36735xx 对 OV/UV/OCD/OCC/SC/外部温度保护的手册恢复条件，核心是：**MCU 清对应 FLAG**（LTCLR + W0C）。AFE 没有类似 BMS 软件参数中的独立 `OV_Rcv/UV_Rcv/OCP_Rcv/T_Rcv` 寄存器。

因此当前协调策略是：

1. AFE 先用硬件阈值快速切 MOS、置 FLAG。
2. 活动的 AFE FLAG 立即进入 MCU 的方向性 FET gate；即使软件 Third 尚未 active，也不会主动请求同方向 MOS 重开。
3. MCU 对每一个硬件 FLAG 建立独立 recovery counter，直接检查物理量是否进入 `*_Rcv` 安全区并连续满足对应 filter 时间。
4. OV/UV/OCD1/OCD2/OCC 除了满足软件恢复阈值，还必须越过 `sh3673510_control_get_protection_actual()` 给出的 **AFE 实际量化阈值**，避免客户配置的 recovery 值高于硬件 trip 值时形成 clear→retrip 循环。
5. TS1/TS2 的 OTC/OTD/UTC/UTD 使用电池温度恢复阈值和 filter；TS3/TS4 不参与这些 AFE TEMP FLAG 的恢复判断。
6. 物理恢复条件稳定后，MCU 才执行 LTCLR + W0C 清对应 AFE FLAG。
7. FLAG 清除后仍要经过 output enabled、有效采样、通讯健康、软件 Third fault、钥匙等 FET gate；由于本周期读取到的硬件 FLAG 仍然有效，至少到下一次有效状态采样才可能重新放开。

因此硬件 FLAG 恢复已经和软件 Third fault **解耦**：AFE 负责快速独立触发，MCU 负责按实际物理恢复窗口和硬件量化阈值受控解除。

## 8. 短路恢复是特殊路径

短路不会按普通 OCP recovery 电流阈值直接恢复：

1. AFE SC 触发，SC_FLG=1，硬件立即关放电 MOS。
2. MCU 设置 `s_short_latched=1`。
3. 必须检测 `BSTATUS2.LOADOFF=1` 连续 10 个 200ms 周期，即约 2s，证明负载已经移除。
4. MCU 才执行 LTCLR/W0C 清 SC_FLG。
5. 后续再次读取确认 `SC_FLG=0` 且 `LOADOFF=1`，才解除软件短路锁存并允许重新开放电。

因此短路恢复不依赖“电流已经接近 0A”这种可能误判的条件。

## 9. 通讯故障恢复

SPI CRC/协议/timeout 等错误时：

- `s_output_inhibit=1`
- heater OFF
- balance OFF
- charge/discharge FET OFF
- 连续通信失败达到阈值后重新初始化 AFE

通信恢复后不是立即放开输出，而是要求连续 3 个有效采样快照（当前 200ms 周期，约 600ms）后才清 `output_inhibit`。若检测到 `RST1_FLG` 或 `RST2_FLG`，则先禁止输出、重新执行完整 AFE 初始化/静态寄存器/保护阈值配置并清复位标志，再重新累计这 3 个有效快照；不会仅清 RST 标志后继续使用可能已经回到复位值的 RAM 配置。

## 10. 后续修改某一 bit 的正确方法

静态功能位只改：

`vendor/ble_sample/sh3673510_project_config.h`

例如关闭 AFE 自主低压 Powerdown：

```c
#define SH3673510_D011_PD_EN 0u
```

例如打开 TS4 硬件温度保护（只有在 TS4 与 TS1/TS2 共用同一套 OTC/OTD/UTC/UTD 阈值确实符合产品需求时才应该这样做）：

```c
#define SH3673510_D011_TS4_HW_PROTECT_EN 1u
```

寄存器位定义、mask、复位值只在：

`vendor/ble_sample/sh3673520_reg.h`

修改保护数值不改 bit profile，而是改/写 `g_tParam.protect` 参数，由 `sh3673510_control_apply_protection()` 重新量化到 AFE 寄存器。

## 11. 上板验证要求

软件配置正确不等于硬件保护已经验证。至少需要逻辑分析仪/示波器和故障注入确认：

- 上电后 0x40~0x59 实际读回值
- OV/UV/OCD1/OCD2/OCC/SC 的实际 trip 值和延时
- TS1/TS2 高低温 trip
- FLAG 锁存、LTCLR/W0C 清除时序
- MOS_EN 反向电流强制开 MOS 行为
- WDT 溢出和 Powerdown
- PD_EN 低电芯 Powerdown 与充电唤醒
- SC 的 LOADOFF 2s 受控恢复
- OV/UV/OCD1/OCD2/OCC 在 recovery 条件未满足时 FLAG 不得被清；特别验证软件 Third 未触发但 AFE 已触发的区间
- RST1/RST2 注入后必须先完整重配 AFE，且重新获得 3 个有效快照前 MOS 不得恢复
- TS3 断线/高温必须关闭 PB4 加热；整个测试过程中 PB5 加热保险丝触发脚保持安全低电平

以上验证完成后，才能把“代码/手册一致”提升为“实板保护行为已确认”。
