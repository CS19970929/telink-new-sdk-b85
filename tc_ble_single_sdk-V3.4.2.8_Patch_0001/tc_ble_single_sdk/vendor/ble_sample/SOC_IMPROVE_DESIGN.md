# SOC改进详细设计方案

**日期**: 2026-03-19
**目标**: 在现有安时积分基础上，补齐第二、三层SOC策略，显著提升精度
**不做**: 卡尔曼滤波（当前硬件和数据基础不适合）

---

## 一、当前实现分析

### 已实现
| 功能 | 状态 | 问题 |
|------|------|------|
| 安时积分 | ✓ | 矩形积分法，精度低 |
| 满充校准 | ✓ | VCellMax>=4180触发 |
| 满放校准 | ✓ | VCellMin<=3100触发，延迟5秒 |
| 末端电压修正 | ✓ | 充放电末段强制增减SOC |
| SOH(循环次数) | ✓ | 仅基于循环次数，无温度/日历老化 |
| Flash存储 | ✓ | 变化即写，频繁 |

### 缺失（本次需补齐）
| 功能 | 影响 |
|------|------|
| OCV静置校准 | 静置后无法修正安时积分漂移 |
| 温度容量补偿 | 低温实际容量降低，SOC偏高 |
| 温度对积分的系数修正 | 高低温下容量不同，积分不准 |
| 内阻压降补偿 | 大电流时电压偏低，误判状态 |
| 静置检测 | 无法判断何时做OCV校准 |
| 自放电 | 长期静置SOC不降 |

---

## 二、改进方案（3阶段）

### 第一阶段：OCV校准 + 静置检测（效果最大，代码量适中）

#### 1.1 启用OCV-SOC查表

电池类型：三元锂（当前FD_BMS_TYPE=D11，10S）

三元锂 25°C OCV-SOC 参考表（单节）：

```
SOC(%)  OCV(mV)
  0     3000
  5     3200
 10     3350
 15     3420
 20     3480
 25     3530
 30     3570
 35     3610
 40     3650
 45     3690
 50     3730
 55     3770
 60     3810
 65     3860
 70     3910
 75     3970
 80     4030
 85     4080
 90     4120
 95     4160
100     4200
```

用 `GetEndValue()` 现有查表函数即可。

#### 1.2 静置检测状态机

```
IDLE ──(电流<阈值持续T1)──> PREPARE ──(继续静置T2)──> READY(可校准)
  ↑                                                      │
  └──(电流>阈值)─────────────────────────────────────────┘
```

参数：
- 静置电流阈值: 0.5A (u16Ichg<=5 && u16IDischg<=5)
- T1 准备时间: 30秒 (150次*200ms)
- T2 稳定时间: 5分钟 (1500次*200ms)
- OCV校准容差: 当前SOC与OCV查表SOC偏差>5%时校正

#### 1.3 OCV校准规则

```
if (静置状态==READY)
{
    soc_ocv = OCV_Table_Lookup(VCellMin);
    
    // 只允许向下修正（安全策略：宁可显示低，不可显示高）
    if (soc_ocv < SOC_Now && (SOC_Now - soc_ocv) > 5)
    {
        SOC_Now = soc_ocv;
    }
    // 向上修正需更严格（防止过充误判）
    else if (soc_ocv > SOC_Now && (soc_ocv - SOC_Now) > 3 && SOC_Now < 50)
    {
        SOC_Now = soc_ocv;
    }
}
```

**改动文件**: `SocEnhance.c`
**新增函数**: `soc_ocv_calibration()`, `soc_rest_detect()`
**修改函数**: `APP_SOC_IntEnhance_Ctrl()` 中增加调用

---

### 第二阶段：温度补偿（效果显著）

#### 2.1 温度-容量系数表

三元锂电池不同温度下可用容量折减系数：

```
温度(°C)    系数(千分比)
  -20        600    (60%)
  -10        700
    0        800
   10        900
   25       1000    (100%)
   35       1020
   45       1030
   55       1020
   60       1000
```

实际容量 = 标称容量 × 温度系数 / 1000

#### 2.2 积分系数修正

安时积分时，实际积分的电量应考虑温度折减：

```c
// 当前（无温度补偿）
u32CapChange += I * dt;    // 直接累加

// 改进（温度补偿）
u16 temp_c = (g_stCellInfoReport.u16TempMin - 400) / 10;  // 实际温度
u16 cap_factor = get_temp_capacity_factor(temp_c);         // 千分比
u32CapChange += I * dt * cap_factor / 1000;               // 温度修正后累加
```

#### 2.3 满充/满放阈值温度修正

```
当前: SOC_100_VAL = 4180 (固定)
改进: SOC_100_VAL = get_soc100_voltage(temp_c)  // 低温时降低满充电压

当前: SOC_0_VAL = 3100 (固定)  
改进: SOC_0_VAL = get_soc0_voltage(temp_c)      // 低温时提高满放电压
```

**改动文件**: `SocEnhance.c`, `SocEnhance.h`
**新增数据**: 温度-容量系数表, 温度-电压修正表

---

### 第三阶段：内阻补偿 + SOH改进 + 自放电

#### 3.1 内阻压降补偿

核心：从采样电压中剥离内阻压降，得到真实OCV用于判断

```
V_real = V_sample - I * R_internal
```

内阻R随SOC和温度变化，需要一张二维查找表：

```
SOC(%)/T(°C)   -10    0    25    45
   10          180   140   100    85   (mΩ/串)
   30          150   120    90    75
   50          140   110    85    70
   70          145   115    88    72
   90          170   135    98    82
```

对于当前项目（10S，采样电阻2mΩ）：
```c
// 放电时：V_real = V_sample + I * R_total
// 充电时：V_real = V_sample - I * R_total
// R_total = R_per_cell * SeriesNum
```

用途：
1. OCV校准时用内阻补偿后的电压查表
2. 电压判断保护阈值时补偿大电流影响

**注意**: 内阻表需要实际电池标定，初期可用经验值。

#### 3.2 SOH改进（日历老化+温度）

当前SOH只看循环次数，实际老化是循环+日历+温度的综合结果。

```c
// 日历老化：每年约2-3%容量损失（25°C存储）
// 温度加速因子：每升高10°C，老化速率约翻倍

u32 calendar_loss_per_year = 25;  // 千分比/年 @25°C
u32 temp_factor = pow(2, (temp_c - 25) / 10);  // 温度加速因子
u32 calendar_loss = calendar_loss_per_year * temp_factor * days / 365;

// 最终SOH
soh = 100 - cycle_loss - calendar_loss/1000;
```

需要增加一个 `u32RuntimeDays` 计数器（Runtime模块已有基础）。

#### 3.3 自放电补偿

三元锂电池自放电率约 2-5%/月 @25°C，温度越高越快。

```c
// 每天静置时扣除自放电
// 温度越高自放电越快
if (is_resting)
{
    u32 self_discharge_per_day = 3;  // 千分比/天 @25°C
    u32 sd_factor = get_temp_sd_factor(temp_c);
    u32CapNow -= u32CapFull * self_discharge_per_day * sd_factor / (1000 * 86400);
}
```

---

## 三、代码架构设计

### 3.1 新增文件

不需要新增文件，在 `SocEnhance.c` 中扩展。

### 3.2 新增数据结构

```c
// OCV-SOC查表数据
struct OCV_SOC_TABLE {
    const uint16_t *voltage;  // 电压数组 (mV)
    const uint8_t  *soc;      // 对应SOC数组 (%)
    uint16_t size;
};

// 静置检测状态
enum REST_STATE {
    REST_IDLE = 0,     // 非静置
    REST_PREPARE,      // 准备中（电流刚降到阈值以下）
    REST_READY,        // 静置稳定，可校准
};

struct SOC_REST_DETECT {
    enum REST_STATE state;
    uint16_t prepare_cnt;   // 准备计数器
    uint16_t ready_cnt;     // 稳定计数器
    uint8_t  large_curr_flag; // 大电流后需更长静置时间
};

// 温度补偿参数
struct TEMP_COMP {
    int8_t   temp_c;        // 当前温度 (°C)
    uint16_t cap_factor;    // 容量系数 (千分比)
    uint16_t ocv100_mv;     // 该温度下的满充电压
    uint16_t ocv0_mv;       // 该温度下的满放电压
};
```

### 3.3 新增函数清单

| 函数 | 说明 | 复杂度 |
|------|------|--------|
| `soc_ocv_table_init()` | 初始化OCV表 | 低 |
| `soc_rest_detect()` | 静置检测状态机 | 中 |
| `soc_ocv_calibration()` | OCV校准主逻辑 | 中 |
| `soc_temp_get_capacity_factor()` | 温度→容量系数 | 低 |
| `soc_temp_get_voltage()` | 温度→满充/满放电压 | 低 |
| `soc_internal_r_get()` | SOC+温度→内阻 | 低 |
| `soc_voltage_compensate()` | 内阻补偿电压 | 低 |

### 3.4 修改函数清单

| 函数 | 修改内容 |
|------|----------|
| `APP_SOC_IntEnhance_Ctrl()` | 增加 `soc_ocv_calibration()` 调用 |
| `SOC_Cont_AH_Int_CHG()` | 积分时乘以温度容量系数 |
| `SOC_Cont_AH_Int_DSG()` | 积分时乘以温度容量系数 |
| `soc_cali()` | 满充/满放电压阈值改为温度修正 |
| `CORRECTION_TERMINAL_CV()` | 末端修正加入内阻补偿 |
| `soc_param_lib_init()` | 上电初始化用OCV估算初始SOC |

---

## 四、实施优先级与工作量

| 阶段 | 功能 | 代码量 | 效果 | 建议 |
|------|------|--------|------|------|
| P0 | OCV查表+静置检测 | ~150行 | ★★★★★ | 必做 |
| P1 | 温度容量系数 | ~80行 | ★★★★ | 必做 |
| P2 | 满充/满放温度修正 | ~40行 | ★★★ | 推荐 |
| P3 | 内阻补偿 | ~60行+标定表 | ★★★ | 推荐（需实测标定） |
| P4 | SOH日历老化 | ~50行 | ★★ | 可选 |
| P5 | 自放电补偿 | ~30行 | ★★ | 可选 |

---

## 五、风险与注意事项

1. **OCV表准确性**: 上表为参考值，需根据实际电池型号标定。不同厂家三元锂OCV曲线差异可达50-100mV。

2. **静置检测时间**: 大电流放电后，电压恢复需要时间（极化效应），需等待足够久才能用OCV查表。
   - 大电流(>5A): 等待30分钟
   - 中电流(1-5A): 等待10分钟
   - 小电流(<1A): 等待3分钟

3. **温度测量**: AFE的温度采样精度和位置影响补偿效果。NTC探头贴在电芯上比贴在PCB上更准。

4. **Flash写入频率**: 增加OCV校准后可能触发额外SOC存储，需关注Flash寿命。

5. **测试验证**: 建议在 -10°C、0°C、25°C、45°C 四个温度点分别做充放电曲线验证SOC精度。

---

## 六、确认事项

请确认以下信息后开始实施：

1. 是否只做 P0+P1（OCV+温度补偿），还是全部阶段？
2. 三元锂OCV-SOC表是否需要基于实际电池标定？还是先用参考值？
3. 内阻值是否已有标定数据？还是先用经验值？
4. 是否需要保留旧代码(#if 0)作为参考？还是直接替换？
