# DVC1124-2 配置与通信接口

> 芯片寄存器依据：`DVC1124-2 Reference Manual V1.2`
>
> 目标：让 AFE 的寄存器事实、板级默认、BMS 保护参数、持久化和 BLE/UART 通信接口彼此分层；业务代码不再依赖难以理解的 `0x49`、`0x7F`、`0x28` 等 magic value。

## 1. 分层

```text
DVC1124-2 V1.2 Reference Manual
                |
                v
         dvc1124_reg.h
 address / bit / mask / shift / enum / reset
                |
                v
           dvc1124.h/.c
      CRC / I2C / RMW / readback
                |
        +-------+-------+
        |               |
        v               v
 semantic config     raw diagnostics
        |
        v
 dvc1124_config_store
 AFE-only flash KV / restore
        |
        +-------+-------+
                |
          Modbus register map
                |
          +-----+-----+
          |           |
          v           v
       UART        BLE SPP
```

BLE 与串口**不实现两套 AFE 参数逻辑**。当前 BLE SPP 与 UART 都进入同一个 `modbus_on_frame()`，因此共享下面的逻辑寄存器表。

## 2. 芯片寄存器真值与访问语义

`vendor/ble_sample/dvc1124_reg.h` 是 DVC1124-2 V1.2 的代码级寄存器真值；`dvc1124.h` 的 access helper 根据这些地址执行 destructive-read 门禁。

规则：

- 禁止使用 C bit-field 映射硬件寄存器；
- 使用 address + mask + shift；
- 未命名 / reserved / read-only bit 不属于驱动写权限；
- mixed-access register 必须 read-modify-write；
- safety-critical write 必须 readback；
- self-clearing command（例如 COW/CAMZ）不能当 persistent configuration；
- Balance / FET state 属于 runtime control，不属于 persistent configuration；
- RC/read-clear 寄存器不能作为普通 raw register 无副作用读取。

当前 V1.2 明确需要特殊处理的 destructive read：

```text
0x01 STATUS
  VADF / CC1F / CC2F = RC

0x76 CORE_OT
  COTF = RC
```

因此普通 raw diagnostics 不直接读取这两个地址。`0x76` 的阈值读写使用 `dvc1124_special.c` 专用接口，任何被该过程读取并清除的 COTF 都先保存到软件 sticky latch。

## 3. AFE 持久化

DVC1124 专属工作配置使用独立 `flash_kv32` journal：

```text
512K: 0x5F000..0x62FFF
4 x 4KB sectors
```

对应文件：

```text
vendor/ble_sample/dvc1124_config_store.h
vendor/ble_sample/dvc1124_config_store.c
```

保存范围包括：

- CADC / CC1 timing；
- charge pump / VADC；
- GP1..GP6 mode；
- V3P3 / I2C watchdog / timed wake / interrupt mask；
- current wake threshold；
- body-diode threshold；
- DSG pull-down strength；
- I2C timeout 对 CHG/DSG 的动作策略；
- DVC core over-temperature code；
- SCD threshold / delay。

COV/CUV/OCD/OCC **不重复存储**，继续由现有 `g_tParam.protect` / `cold_kv` 作为 requested source of truth。

AFE reset 后的流程为：

```text
DVC register reset
 -> apply existing board/protection configuration
 -> load DVC semantic config KV
 -> validate
 -> apply using masked RMW/readback
 -> normal sampling
```

配置 KV 无效或 schema 不匹配时使用当前项目 defaults；不能把未知旧数据直接解释为新配置。

## 4. 语义配置窗口

### 4.1 基本信息与无副作用诊断 `0x2800..0x2808`

| 地址 | 名称 | 单位/值 | 权限 |
|---:|---|---|---|
| `0x2800` | schema version | 当前 `1` | R |
| `0x2801` | DVC model | 22 / 24 | R |
| `0x2802` | chip version | raw CV | R |
| `0x2803` | I2C write address | 8-bit transfer address | R |
| `0x2804` | cell count | 4..24 | R |
| `0x2805` | shunt low word | uOhm | R |
| `0x2806` | shunt high word | uOhm | R |
| `0x2807` | cached STATUS | 最近一次正常采样读取到的 0x01 | R |
| `0x2808` | core OT event latched | 0/1，软件 sticky COTF | R |

`0x2807`/`0x2808` 的目的就是避免上位机为了诊断直接读取 RC 寄存器并无声清除硬件状态。

`cell count` 属于产品 profile/板型身份，不能与普通 AFE tuning 参数混为一类；当前不作为量产普通可写参数。

### 4.2 CADC / VADC / charge-pump `0x2810..0x281B`

| 地址 | 名称 | 通信值 |
|---:|---|---|
| `0x2810` | high-side FET mask | 0/1 |
| `0x2811` | CADC work enable | 0/1 |
| `0x2812` | sleep current-wake engine enable | 0/1 |
| `0x2813` | CC1 work time | code 0..3 = 0.5/1/2/4ms |
| `0x2814` | CC1 sleep wake time | code 0..3 = 4/8/16/32ms |
| `0x2815` | charge-pump voltage | 0(off), 6..12V |
| `0x2816` | cell measurement mask CMM | 0/1 |
| `0x2817` | cell signed mode CVS | 0/1 |
| `0x2818` | VADC enable | 0/1 |
| `0x2819` | VADC sync with CC2 | 0/1 |
| `0x281A` | VADC period | 1/2/4/8 CC2 cycles |
| `0x281B` | VADC conversion time | 790/1540/3030/6020 us |

### 4.3 GP modes `0x2820..0x2825`

通信值直接使用 V1.2 mode code。

| 地址 | GP | 合法值 |
|---:|---|---|
| `0x2820` | GP1 | 0=OFF, 1=NTC, 2=analog, 3=CON |
| `0x2821` | GP2 | 0=OFF, 1=NTC, 2=analog, 6=INT, 7=low-side PDSG |
| `0x2822` | GP3 | 0=OFF, 1=NTC, 2=analog, 6=INT, 7=low-side PCHG |
| `0x2823` | GP4 | 0=OFF, 1=NTC, 2=analog, 3=DON |
| `0x2824` | GP5 | 0=OFF, 1=NTC, 2=analog, 6=INT, 7=low-side CHG |
| `0x2825` | GP6 | 0=OFF, 1=NTC, 2=analog, 6=INT, 7=low-side DSG |

`3/4/5` 对 GP2/3/5/6 在 V1.2 中为 N/A，语义接口拒绝写入。

### 4.4 WDT / wake / body-diode / misc `0x2828..0x2835`

| 地址 | 名称 | 通信值 |
|---:|---|---|
| `0x2828` | V3P3 sleep enable | 0/1 |
| `0x2829` | V3P3 work enable | 0/1 |
| `0x282A` | V3P3 restart after I2C timeout | 0/1 |
| `0x282B` | DVC I2C watchdog | 0/4/8/16/32 s |
| `0x282C` | timed wake | 0, 10..60, 120..600 s，必须为手册离散值 |
| `0x282D` | interrupt mask | bit mask |
| `0x2830` | current-wake threshold | uV，0=off，其他为 10uV step |
| `0x2831` | body-diode threshold | uV，0=off，其他为 40uV step |
| `0x2832` | DSG pull-down strength DPC | 0..30 |
| `0x2833` | I2C timeout closes CHG | 0/1 |
| `0x2834` | I2C timeout closes DSG | 0/1 |
| `0x2835` | core OT threshold | 0.1°C，0=off |

这些项目中有多项属于产品安全策略。接口存在不等于已经得到量产参数结论；未完成硬件验证的功能继续保持 fail-safe 默认 disabled。

## 5. Protection requested / effective

### 5.1 Requested `0x2840..0x284D`

| 地址 | 参数 | 单位 |
|---:|---|---|
| `0x2840` | COV threshold | mV |
| `0x2841` | COV delay | ms |
| `0x2842` | CUV threshold | mV |
| `0x2843` | CUV delay | ms |
| `0x2844` | OCD1 current | 0.1A |
| `0x2845` | OCD1 delay | ms |
| `0x2846` | OCC1 current | 0.1A |
| `0x2847` | OCC1 delay | ms |
| `0x2848` | OCD2 current | 0.1A |
| `0x2849` | OCD2 delay | ms |
| `0x284A` | OCC2 current | 0.1A |
| `0x284B` | OCC2 delay | ms |
| `0x284C` | SCD sense threshold | mV |
| `0x284D` | SCD delay | us |

COV/CUV/OCD/OCC **不建立第二套保护参数**：它们映射现有 `g_tParam.protect`。单字段写入先把 requested 值同步应用到 AFE 并读回校验，再写 cold KV；apply 或 persist 失败时恢复内存旧值并尝试重新下发，通信返回 device failure。因此成功应答代表 live AFE、内存请求值和持久化值一致。

当前 BMS 参数模型只有一组充/放过流 filter，因此 OC1 和 OC2 delay alias 最终仍映射同一 filter 字段。这是当前 BMS 参数模型的约束，不应在通信层伪造两套独立持久参数。

SCD 没有历史 `g_tParam` 字段，因此由 `dvc1124_config_store` 独立保存；量产前仍必须通过硬件测试确定 threshold/delay，默认保持 disabled。

### 5.2 Effective `0x2850..0x285D`

与 requested 一一对应，但值从**当前 DVC 寄存器实时反解**，只读。

用途：上位机必须能够区分：

```text
requested 15.0A
encoded OCD2T = 0
actual/effective 20.0A   (200uOhm shunt)
```

不能把硬件量化后的值伪装成 requested 值。

## 6. Raw register mirror `0x2900..0x2990`

映射：

```text
Modbus/BLE 0x2900 + DVC register offset
```

普通无副作用寄存器仍可直接诊断，例如：

```text
0x295E -> DVC 0x5E OCD2
0x296D -> DVC 0x6D CP/COW/CMM/CVS
0x2977 -> DVC 0x77 I2C WDT
```

但以下两个地址**禁止通过普通 raw read 读取**：

```text
0x2901 -> DVC 0x01 STATUS（包含 RC flags）
0x2976 -> DVC 0x76 CORE_OT（COTF 为 RC）
```

替代接口：

```text
0x2807 -> cached STATUS
0x2808 -> sticky COTF event
```

Raw 规则：

- 地址空间仍覆盖 DVC `0x00..0x90`，但 RC/destructive register 的普通 raw read 返回 forbidden；
- raw write 仅在 Factory 模式开放；
- raw write 仍通过 semantic candidate + validation + apply/persist，不允许直接绕开配置模型；
- reserved / unnamed / read-only bits 不属于普通配置写权限；
- Alarm、CST、FET runtime control、Balance、COW、CAMZ 等不属于 persistent raw config；
- Raw register 是工厂/诊断能力，不是普通产品参数 API。

## 7. 当前开发状态

已完成：

1. V1.2 寄存器真值集中到 `dvc1124_reg.h`；
2. `dvc1124.c` / `dvc1124_bms.c` 的重复寄存器地址和 Alarm bit 已完成首轮清理；
3. board defaults 改成语义化配置，不再要求人工解释 `0x49/0x7F`；
4. UART + BLE 共享 `0x2800/0x2900` AFE 窗口；
5. requested/effective 保护值分离；
6. 独立 DVC AFE `flash_kv32` 区与 reset 后恢复路径；
7. SCD、WDT、GP、Body Diode 等 DVC 专属配置已有独立持久化数据模型；
8. 字段写入已禁止超位宽值被 mask 后静默截断；
9. 0x01/0x76 已进入 destructive-read 策略；
10. Core OT 阈值访问使用专用接口并保留软件 sticky COTF。

下一步按 `docs/DVC1124_DEVELOPMENT_TASKS.md` 补齐 R52/R53/R54、0x6A..0x6C 和完整 catalog，并完成多字段原子事务、固定 TC32 编译和实板验证。当前分支仍不能作为量产 release。
