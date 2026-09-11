# DVC1124-2 配置与通信接口

> 分支：`feature/dvc1124-config-registry`
>
> 芯片寄存器依据：`DVC1124-2 Reference Manual V1.2`
>
> 目标：让 AFE 的寄存器事实、板级默认、BMS 保护参数、BLE/UART 通信接口彼此分层；业务代码不再依赖难以理解的 `0x49`、`0x7F`、`0x28` 等 magic value。

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
        |               |
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

## 2. 芯片寄存器真值

`vendor/ble_sample/dvc1124_reg.h` 是 DVC1124-2 V1.2 的代码级寄存器真值。

规则：

- 禁止使用 C bit-field 映射硬件寄存器；
- 使用 address + mask + shift；
- 未命名 / reserved / read-only bit 不属于驱动写权限；
- mixed-access register 必须 read-modify-write；
- safety-critical write 必须 readback；
- self-clearing command（例如 COW/CAMZ）不能当 persistent configuration；
- Balance / FET state 属于 runtime control，不属于 persistent configuration。

## 3. 语义配置窗口

### 3.1 基本信息 `0x2800..0x2806`

| 地址 | 名称 | 单位/值 | 权限 |
|---:|---|---|---|
| `0x2800` | schema version | 当前 `1` | R |
| `0x2801` | DVC model | 22 / 24 | R |
| `0x2802` | chip version | raw CV | R |
| `0x2803` | I2C write address | 8-bit transfer address | R |
| `0x2804` | cell count | 4..24 | R/W* |
| `0x2805` | shunt low word | uOhm | R |
| `0x2806` | shunt high word | uOhm | R |

`cell count` 当前写入只修改 runtime driver config；持久化策略将在 AFE NVM transaction 阶段收口，因此量产上位机暂不应依赖该写入口。

### 3.2 CADC / VADC / charge-pump `0x2810..0x281B`

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

### 3.3 GP modes `0x2820..0x2825`

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

### 3.4 WDT / wake / body-diode / misc `0x2828..0x2835`

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

## 4. Protection requested / effective

### 4.1 Requested `0x2840..0x284D`

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

COV/CUV/OCD/OCC **不建立第二套保护参数**：它们映射现有 `g_tParam.protect`，继续使用现有 Flash KV 与 `AFE_PARAM_WRITE_Flag` 触发 AFE 重新量化配置。

当前 BMS 参数模型只有一组充/放过流 filter，因此 OC1 和 OC2 delay alias 最终仍映射同一 filter 字段。这是当前 BMS 参数模型的约束，不应在通信层伪造两套独立持久参数。

SCD 当前没有既有产品参数字段，`0x284C/0x284D` 仅作为调试阶段 runtime request；量产前必须完成硬件验证和持久化策略，不得默认开启。

### 4.2 Effective `0x2850..0x285D`

与 requested 一一对应，但值从**当前 DVC 寄存器实时反解**，只读。

用途：上位机必须能够区分：

```text
requested 15.0A
encoded OCD2T = 0
actual/effective 20.0A   (200uOhm shunt)
```

不能把硬件量化后的值伪装成 requested 值。

## 5. Raw register mirror `0x2900..0x2990`

映射：

```text
Modbus/BLE 0x2900 + DVC register offset
```

例如：

```text
0x295E -> DVC 0x5E OCD2
0x296D -> DVC 0x6D CP/COW/CMM/CVS
0x2977 -> DVC 0x77 I2C WDT
```

规则：

- `0x00..0x90` 全部允许读取，用于诊断；
- 写入口只拥有 V1.2 明确公开的 stable configuration bits；
- reserved / unnamed / read-only bits 必须保留；
- Alarm、CST、FET runtime control、Balance、COW、CAMZ 等不是 persistent raw config，不经该 raw 写入口修改；
- Raw write 是工厂/诊断能力，不应该成为普通用户参数页的首选接口；生产版本还需增加权限/模式门禁。

## 6. 当前尚未完成的事项

本轮先建立**寄存器真值 + 语义配置 + BLE/UART 共用入口**。以下项目在合入量产分支前继续完成：

1. AFE operating config 独立 Flash KV 持久化与 transaction commit；
2. raw/safety write 增加 Factory/Debug 权限门禁；
3. Modbus 写失败返回明确 exception/status，而不是单纯 echo；
4. `dvc1124.c` 内旧的局部 `DVC_REG_*` 别名迁移到 `dvc1124_reg.h` 唯一真值；
5. `register_catalog.json` 纳入该 schema，生成 Win/macOS/Android/iOS 客户端定义；
6. TC32 `bms.py ci` 编译、静态分析、MAP/size 对比；
7. HS-D008 实板验证 COV/CUV/OC、SCD、WDT、Body Diode、Sleep/Wake、GP mode。

在以上项目闭环前，本分支属于**开发分支，不直接作为量产 release**。
