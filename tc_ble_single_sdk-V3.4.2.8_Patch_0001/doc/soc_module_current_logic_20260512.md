# SOC 模块当前逻辑完整梳理

更新时间：2026-05-12

本文按当前工作区源码梳理 `vendor/ble_sample` 中的 SOC 逻辑。当前 SOC 不是单一安时积分函数，而是：

- AFE 电压/电流采样输入
- 200ms 安时积分主路径
- OCV/端点/低电区策略修正
- SOH 与循环次数统计
- KV 持久化
- `SocElement` 对外上报窗口

## 1. 相关文件

| 文件 | 职责 |
| --- | --- |
| `tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c` | SOC 主算法、积分、OCV/端点策略、SOH、输出同步 |
| `tc_ble_single_sdk/vendor/ble_sample/SocEnhance.h` | `SOC_CALCULATE_ELEMENT` 运行态结构和外部接口 |
| `tc_ble_single_sdk/vendor/ble_sample/app.c` | 启动恢复、200ms 调度、1s AFE 更新、KV 写回 |
| `tc_ble_single_sdk/vendor/ble_sample/sh367309_datadeal.c` | AFE 数据读取，生成单体电压、温度、充/放电电流 |
| `tc_ble_single_sdk/vendor/ble_sample/sci_upper.h` | `stCell_Info` 和 `SocElement` 对外结构 |
| `tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c/.h` | SOC/DSG/cycle 热区 KV 持久化 |
| `tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c` | 上位机写 SOC、写 cycle、实时窗口读 SOC/电流 |
| `tc_ble_single_sdk/vendor/ble_sample/sif_send.c` | 私有协议上报 SOC、电流、cycle |

## 2. 当前版型和默认参数

当前 `conf.h` 工作区配置为：

```c
#define FD_BMS_TYPE   C11_AND_C11pro
#define SeriesNum     (13)
#define CapacityFactory (104)
#define FAC_INIT_soc  (60)
```

其中 `CapacityFactory` 的配置单位是 `0.1Ah`，所以当前 `104` 代表 `10.4Ah`，不是 `104Ah`。

SOC KV 默认值定义在 `soc_kv_store.h`：

```c
SOC_PARAM_DEFAULT_SOC   = FAC_INIT_soc
SOC_PARAM_DEFAULT_DSG   = 60
SOC_PARAM_DEFAULT_CYCLE = 1
```

当前 `FW_UPGRADE_RESET_SOC_EPOCH = 0`，也就是本固件默认不触发一次性 SOC 重置。

## 3. 单位约定

| 字段/宏 | 当前单位 | 说明 |
| --- | --- | --- |
| `u16Ichg` | `A*10` | 充电电流，来自 `DataLoad_Current()` |
| `u16IDischg` | `A*10` | 放电电流，来自 `DataLoad_Current()` |
| `u16VCellMin/u16VCellMax` | `mV` | 最低/最高单体电压 |
| `u16VCellDelta` | `mV` | 单体压差 |
| `u32CapFactory/u32CapFull/u32CapNow` | `A*0.1s` | 内部容量单位，`CapacityFactory(0.1Ah) * 3600` |
| `u16CapacityNow/Full/Factory` | `Ah*100` | 对外上报容量单位 |
| `u8SOC_Now` | `%` | 内部 SOC，整数 `0~100` |
| `SocElement.u16Soc` | `%` | 对外显示/通信 SOC |

容量换算关系：

```text
内部满容量 = CapacityFactory(0.1Ah) * 3600 * SOH / 100
对外容量 = 内部容量 / 360
```

例如当前 `CapacityFactory=104`，即 `10.4Ah`，`SOH=100%`：

```text
u32CapFull = 104 * 3600 = 374400
1% 容量 = 3744
u16CapacityFull = 374400 / 360 = 1040  // Ah*100 = 10.40Ah
```

## 4. 启动链路

启动初始化在 `app.c` 中完成：

1. `App_AFEGet()` 先读取一帧 AFE 快照。
2. `soc_kv_store_init()` 初始化 SOC 热区 KV。
3. `soc_kv_store_get()` 读取 `soc/dsg/cycle`；没有有效记录时使用 `SOC_PARAM_DEFAULT_*`。
4. `soc_param_lib_init(&d)` 将 KV 数据装入 `SOC_Calculate_Element`。
5. `soc_sanitize_state()` 对 SOC、DSG、cycle 限幅，并按 SOC 重新计算 `u32CapNow`。
6. `SOC_Result_Pass()` 同步到 `g_stCellInfoReport.SocElement`。

这里先取 AFE 快照的目的，是让启动阶段的 OCV 合理性判断具备电压、电流上下文，避免完全无采样时直接输出历史 SOC。

## 5. 主循环调度

主循环中有三条和 SOC 相关的节拍：

```text
每 200ms:
  APP_SOC_IntEnhance_Ctrl()
  charger_detect_and_keyLogi_200ms()

每 1s:
  App_AFEGet()
  app_adc_multi_sample()
  app_event_log_1s_task()

每个 main_loop:
  soc_kv_store_update_and_log_if_changed(u8SOC_Now, u8DSG_SOC_Int, u32Cycle_times)
```

注意：SOC 每 200ms 积分一次，但电压/电流快照目前主要每 1s 更新一次。因此 200ms 积分使用的是最近一次 AFE 电流值；电流突变的真实响应上限仍受 AFE 刷新周期影响。

## 6. 电流输入链路

`App_AFEGet()` 成功读取 SH367309 RAM 后调用：

```text
UpdateVoltageFromBqMaximo()
DataLoad_CellVolt()
DataLoad_CellVoltMaxMinFind()
DataLoad_Temperature()
DataLoad_TemperatureMaxMinFind()
DataLoad_Current()
```

`DataLoad_Current()` 根据 AFE 电流符号区分充电和放电：

- 正向：计算 `u32_ChgCur_mA`，清零 `u32_DsgCur_mA`
- 反向：计算 `u32_DsgCur_mA`，清零 `u32_ChgCur_mA`
- 根据 `SYSKDEFAULT/SYSBDEFAULT` 做电流校准
- 写入 `g_stCellInfoReport.u16Ichg/u16IDischg`
- 小于等于 `2` 的电流值会清零，也就是硬件输入层仍有约 `0.2A` 的死区

当前 C11/C11pro 放电电流换算有特殊分支：

```c
g_stCellInfoReport.u16IDischg = (UINT16)((u32_DsgCur_mA >> 10) / 10 / 12);
```

SOC 模块不再额外过滤或延时，只要最终 `u16Ichg/u16IDischg` 非零，就进入对应方向积分。

## 7. SOC 运行态结构

核心运行态是 `SOC_Calculate_Element`：

| 字段 | 作用 |
| --- | --- |
| `u8SOC_Now` | 当前内部 SOC，`0~100` |
| `u32CapNow` | 当前剩余容量，内部单位 `A*0.1s` |
| `u32CapFull` | 当前满容量，已叠加 SOH |
| `u32CapFactory` | 版型额定容量，`CapacityFactory * 3600`，其中 `CapacityFactory` 单位为 `0.1Ah` |
| `u32CapChange` | 当前方向下未跨越显示 1% 的积分残量/调试量 |
| `u8CHG_AHCalcu_Flag` | 充电积分标志，当前作为过程状态使用 |
| `u8DSG_AHCalcu_Flag` | 放电积分标志，当前作为过程状态使用 |
| `u8DSG_SOC_Int` | 放电 SOC 累计量，用于折算循环次数 |
| `u32Cycle_times` | 循环次数，最大钳到 `65535` |
| `soh` | 由 cycle 映射出的 SOH |
| `u32CapFull_Cal_As` | 充电累计量，当前只在充电积分中增加，未参与主链路决策 |

`SOC_Cali_Flag` 仍保留 `TRANSFER/CHG/DSG` 三态，但当前 `APP_SOC_IntEnhance_Ctrl()` 已经直接按实时电流选择分支，不再依赖状态机延时进入。

## 8. 200ms 安时积分主路径

入口：

```c
APP_SOC_IntEnhance_Ctrl()
```

当前执行顺序：

1. 如果 `u16Ichg > 0`，执行 `SOC_Cont_AH_Int_CHG()`。
2. 否则如果 `u16IDischg > 0`，执行 `SOC_Cont_AH_Int_DSG()`。
3. 否则执行 `SOC_State_Transfer()`，清理积分方向与余数。
4. 执行 `soc_strategy_update()` 做 OCV/端点/低电区修正。
5. 执行 `SOC_Result_Pass()` 写回对外结构。

如果充电和放电电流同时非零，当前代码优先走充电分支。这种情况正常硬件路径不应出现，若出现应优先检查电流采样和方向判定。

### 8.1 积分公式

当前周期固定按 `200ms` 计算：

```text
sum = current(A*10) * 200 + remainder
delta = sum / 1000
remainder = sum % 1000
```

`delta` 的单位就是内部容量单位 `A*0.1s`。余数保留在 `g_soc_integral_ms_remainder`，用于避免小电流长期被整数除法吃掉。

方向切换时，`soc_integral_select_dir()` 会清理：

- `g_soc_integral_ms_remainder`
- `u32CapChange`

这样可以避免充电剩余积分直接带到放电方向，或反过来。

### 8.2 充电积分

`SOC_Cont_AH_Int_CHG()` 当前逻辑：

1. `isCHG()` 判断 `u16Ichg > 0`。
2. 按 200ms 公式得到 `delta`。
3. `u32CapNow += delta`，但不超过 `u32CapFull`。
4. 根据 `u32CapNow/u32CapFull` 计算新的 SOC。
5. 充电方向使用向下取整，只有真正跨过下一个百分比边界才显示增加。
6. 到达满容量后清理 `u32CapChange`。
7. `u32CapFull_Cal_As += delta`，但当前没有用于容量学习。

充电向下取整是用户体验策略：避免刚插充电器或小电流波动就立刻跳 1%。

### 8.3 放电积分

`SOC_Cont_AH_Int_DSG()` 当前逻辑：

1. `isDSG()` 判断 `u16IDischg > 0`。
2. 按 200ms 公式得到 `delta`。
3. `u32CapNow -= delta`，不足时钳到 `0`。
4. 根据 `u32CapNow/u32CapFull` 计算新的 SOC。
5. 放电方向使用向上取整，只有真正跌破当前百分比边界才显示降低。
6. 每次 SOC 下降时，将下降的百分比累加到 `u8DSG_SOC_Int`。
7. `u8DSG_SOC_Int >= 80` 时减去 80，并让 `u32Cycle_times++`。

放电向上取整是用户体验策略：避免刚起步、刚有负载就因为整数边界立即掉 1%。

## 9. SOH 和循环次数

SOH 由 `bms_soh_from_cycle()` 计算，不做真实容量学习：

| Cycle 区间 | SOH |
| --- | --- |
| `0~80` | `100%` |
| `81~500` | 从 `100%` 线性下降到约 `90%` |
| `501~799` | 从 `90%` 线性下降到约 `80%` |
| `>=800` | `80%` |

`soc_recalc_full_capacity()` 每次根据 cycle 映射 SOH，并更新：

```text
u32CapFull = u32CapFactory * soh / 100
```

当前 cycle 的来源主要是放电 SOC 累计量：放电显示 SOC 累计下降 `80%` 记为 1 次 cycle。

## 10. 策略层修正顺序

`soc_strategy_update()` 每 200ms 跟随 SOC 主入口执行一次，顺序固定：

1. `soc_apply_startup_ocv_correction()`
2. `soc_apply_terminal_sync()`
3. `soc_apply_discharge_terminal_tracking()`
4. `soc_apply_discharge_ocv_tracking()`
5. `soc_apply_idle_ocv_tracking()`

前四个策略只要命中并完成一次调整，就立即返回，不再叠加后续策略。这样可以限制单个 200ms 周期内的 SOC 变化幅度。

### 10.1 OCV 样本有效性

OCV 类策略统一使用 `soc_ocv_sample_valid()`：

- `VCELLMIN` 在 `2000~4000mV`
- `VCELLMAX >= VCELLMIN`
- `VCELLMAX <= 4000mV`
- `u16VCellDelta <= 200mV`

OCV SOC 估算采用线性映射：

```text
weighted_mv = (VCELLMIN * 3 + VCELLMAX) / 4
SOC_0_VAL   = 3000mV
SOC_100_VAL = 4180mV
```

当前没有按电芯体系、温度、静置时长建立 OCV 查表，所以 OCV 只作为保守修正，不作为主算法。

### 10.2 启动 OCV 纠偏

启动纠偏只执行一次，条件：

- OCV 样本有效
- 当前无充放电电流
- `startup_checked == 0`

当前只做一个保守动作：

- 若 OCV 估算为 `0%`
- 且 `VCELLMIN <= SOC_0_VAL`
- 且当前 SOC 大于 `0`
- 则每次只向 `0%` 方向下降 `1%`

中段 SOC 不会在启动时按 OCV 大幅拉回，目的是保证 e-bike 仪表重启后的显示连续性。

### 10.3 满充/空电端点同步

满充同步条件：

- 处于充电态
- `VCELLMAX >= SOC_100_VAL`
- `VCELLMIN >= SOC_100_VAL - 200mV`
- 连续满足 `SOC_FULL_LOCK_TICKS = 20` 次，即约 `4s`
- 之后每 `SOC_TERMINAL_SYNC_STEP_TICKS = 5` 次，即约 `1s`，向 `100%` 增加 `1%`

空电同步条件：

- 当前无充放电电流
- `VCELLMIN <= SOC_0_VAL`
- `VCELLMAX <= SOC_0_VAL + 200mV`
- 连续满足 `SOC_EMPTY_LOCK_TICKS = 25` 次，即约 `5s`
- 之后每约 `1s` 向 `0%` 下降 `1%`

端点同步会调用 `soc_apply_step_towards_when_due()`，每次只调整 `1%`，并清理积分残量。

### 10.4 放电低端上限跟踪

放电过程中，低电区按最低单体电压给 SOC 设置上限：

| 条件 | 目标 SOC 上限 |
| --- | --- |
| `VCELLMIN <= 3000mV` | `0%` |
| `VCELLMIN <= 3050mV` | `1%` |
| `VCELLMIN <= 3150mV` | `3%` |
| `VCELLMIN <= 3200mV` | `6%` |
| `VCELLMIN <= 3300mV` | `12%` |

生效前提：

- 处于放电态
- `VCELLMAX >= VCELLMIN`
- `VCELLMIN >= 2000mV`
- 当前 SOC 高于目标上限

目标为 `0%` 时，还需要 `SOC_DSG_EMPTY_LOCK_TICKS = 10` 次确认，即约 `2s`。之后按 `SOC_DSG_TERMINAL_STEP_TICKS = 5`，约每 `1s` 下降 `1%`。

这个策略不是替代容量积分，而是在低电压端防止仪表仍显示明显偏高 SOC。

### 10.5 放电 OCV 对比修正

放电 OCV 跟踪只允许向下修正，不允许放电过程中向上修正。按放电电流分档：

| 放电电流 `u16IDischg` | 分档 | 差值阈值 | 稳定时间 | 调整间隔 |
| --- | --- | --- | --- | --- |
| `<=20` | low | `1%` | `8` 次，约 `1.6s` | `100` 次，约 `20s` |
| `<=50` | mid | `5%` | `12` 次，约 `2.4s` | `100` 次，约 `20s` |
| `<=100` | high | `8%` | `20` 次，约 `4s` | `100` 次，约 `20s` |
| `>100` | very high | `12%` | `30` 次，约 `6s` | `100` 次，约 `20s` |

修正条件：

- 处于放电态
- OCV 样本有效
- OCV SOC 小于当前 SOC
- 差值达到当前分档阈值

每次只下降 `1%`，并清理积分残量。这样避免大电流压降下 OCV 误判导致 SOC 快速下跳。

### 10.6 静置 OCV 跟踪

静置 OCV 条件：

- 当前无充放电电流
- OCV 样本有效

时间参数：

- `SOC_OCV_IDLE_STABLE_TICKS = 5 * 60 = 300` 次，按 200ms 约 `60s`
- `SOC_OCV_IDLE_ADJUST_TICKS = 5 * 60 = 300` 次，按 200ms 约 `60s`
- `SOC_OCV_RUNTIME_DIFF_THRESHOLD = 3%`

当前静置 OCV 只允许向下修正：

- 如果 OCV SOC 高于或等于当前 SOC，不动作
- 如果当前 SOC 高于 OCV SOC 且差值达到 `3%`，每次下降 `1%`

这个策略用于长时间静置后慢慢修正高估 SOC，同时避免静置后突然大跳。

## 11. 输出链路

`SOC_Result_Pass()` 每 200ms 运行一次，把内部状态同步到：

```c
g_stCellInfoReport.SocElement.u16Soc
g_stCellInfoReport.SocElement.u16Soh
g_stCellInfoReport.SocElement.u16CapacityNow
g_stCellInfoReport.SocElement.u16CapacityFull
g_stCellInfoReport.SocElement.u16CapacityFactory
g_stCellInfoReport.SocElement.u16Cycle_times
```

外部使用点：

- Modbus 实时窗口读取 `BMS_REALTIME_REG_SOC_ADDR`
- 旧布局直接读取 `g_stCellInfoReport.SocElement`
- SIF 公有/私有上报中 SOC 会按协议转换，例如 `soc * 2`
- 上位机工具读取和显示最终都以 `SocElement.u16Soc` 为准

目前没有独立的“真实 SOC”和“显示 SOC”双层模型，内部 `u8SOC_Now` 与对外 `u16Soc` 是同一个整数百分比。

## 12. 上位机写入路径

`modbus_rtu.c` 中：

- 写 `0x1005`：调用 `set_soc_param(val, 1, 1)`
- 写 `0x2319`：更新 `u32Cycle_times = val`，再调用 `set_soc_param(get_soc_real(), 1, 1)`

`set_soc_param()` 会：

1. 设置内部 SOC。
2. 重算满容量与当前容量。
3. 清理积分残量。
4. 如果要求同步显示，则写 `SocElement.u16Soc`。

写入 SOC 后，主循环后续会通过 `soc_kv_store_update_and_log_if_changed()` 将新值写入 KV。

## 13. 持久化逻辑

SOC 热区保存 3 个值：

- `soc`
- `dsg`
- `cycle`

启动读取：

```text
soc_kv_store_get()
  从默认值开始
  如果 KV 中有 SOC/DSG/CYCLE，就逐项覆盖
```

运行写回：

```text
soc_kv_store_update_and_log_if_changed(soc, dsg, cycle)
  如果 KV 未初始化，直接返回
  读取当前 KV 缓存值
  三个值都相同则不写
  任意值变化则 write_all 三个值
```

由于当前 SOC 仍然是 `1%` 级变化，KV 写入频率主要由 SOC 变化、DSG 累计变化和 cycle 变化决定，不会按 200ms 高频写入。

## 14. 当前用户体验策略总结

当前 SOC 适合 e-bike 仪表的体验取向：

- 行驶/充电时，容量积分是主算法。
- SOC 显示只用整数 `1%` 步进，避免小数抖动。
- 充电不提前上跳，放电不提前下跳。
- 满电、空电、低端电压区允许按 1% 慢速收敛。
- 放电 OCV 只向下修正，且大电流更保守。
- 静置 OCV 只向下慢修正，避免重启或静置后大跳。
- 启动阶段只在明显空电时向下纠偏，不在中段按 OCV 强拉。

## 15. 当前风险和注意点

1. AFE 采样是 1s 级，SOC 积分是 200ms 级。200ms 积分能修正时间基准，但不能突破电流采样刷新率。
2. C11/C11pro 放电电流换算有特殊 `/10/12` 缩放，建议用实测电流确认 `u16IDischg` 与真实电流的一致性。
3. OCV 仍是 `3000mV~4180mV` 线性映射，不区分电芯体系、温度和静置时长。
4. `u32CapFull_Cal_As` 当前只累计充电量，未用于容量学习或 SOH 修正。
5. 当前没有单独显示 SOC 缓冲层。如果后续要做更细腻的仪表体验，建议引入内部真实 SOC 与显示 SOC 分层。
6. `SOC_Cali_Flag` 仍保留，但当前主入口已直接按电流分支；后续若清理代码，需要同步检查调试工具和旧文档。

## 16. 建议验证矩阵

| 场景 | 验证目标 |
| --- | --- |
| 20A 恒流放电 | `CapacityFactory=104` 即 `10.4Ah`，约 `18.72s` 下降 `1%` |
| 10A 恒流充电 | `CapacityFactory=104` 即 `10.4Ah`，约 `37.44s` 上升 `1%` |
| 充放电快速切换 | 方向切换后余数清理，SOC 不反向异常跳动 |
| 小电流持续运行 | 余数累计后仍能推动 SOC 变化 |
| 满充端点 | 电压满足后按约 `1%/s` 慢速收敛到 `100%` |
| 低电放电 | 进入低端电压区后 SOC 不再明显高估 |
| 静置高估 | 静置约 60s 后才开始按约 `1%/min` 向下慢修正 |
| 写 `0x1005` | 上位机写 SOC 后 RAM 和 KV 后续保持一致 |
| 断电重启 | 从 KV 恢复 SOC/DSG/cycle，启动阶段不发生中段大跳 |

## 17. 一句话结论

当前 SOC 模块以 200ms 安时积分为主，配合端点同步、放电低端上限、放电 OCV 对比和静置 OCV 慢修正；整体策略偏向 e-bike 仪表的连续性和稳定性，而不是实验室级高精度 OCV 估算。
