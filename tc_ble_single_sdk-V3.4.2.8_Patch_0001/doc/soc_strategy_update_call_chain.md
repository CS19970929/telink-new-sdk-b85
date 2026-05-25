# `soc_strategy_update` 调用链与 OCV/SOC 更新逻辑

更新时间：2026-05-25

## 1. 结论先行

`OCV` 不是每次循环都直接改 `SOC`，而是由 `APP_SOC_IntEnhance_Ctrl()` 驱动的策略层按条件更新。

当前核心口径：

- 外层入口每 `200ms` 执行一次。
- SOC 主体仍是电流积分，OCV/端点只做修正。
- 普通自动修正每次最多移动 `1%`。
- 放电到 `SOC_0_VAL = 3000mV` 是安全端点，持续约 2s 后直接同步 real/display SOC 到 0，用于保证早于 `2750mV` 过放保护点。
- SOH 仍只由 `bms_soh_from_cycle()` 按循环次数计算，不按容量误差或校准结果学习。
- 真正写回 Flash 的是 `soc_kv_store_update_and_log_if_changed()`，它只是“有变化才落盘”，不是 OCV 更新本身。

## 2. 调用链

### 2.1 主循环触发

文件：[`vendor/ble_sample/app.c`](../tc_ble_single_sdk/vendor/ble_sample/app.c)

- `main_loop()` 中：
  - `clock_time_exceed(test_task_tick, 1000 * 200)` 成立时，每 `200ms` 进入一次：
  - `APP_SOC_IntEnhance_Ctrl();`

### 2.2 SOC 策略入口

文件：[`vendor/ble_sample/SocEnhance.c`](../tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c)

`APP_SOC_IntEnhance_Ctrl()` 的执行顺序：

1. `SOC_State_Transfer()` / `SOC_Cont_AH_Int_CHG()` / `SOC_Cont_AH_Int_DSG()` 之一先做状态迁移或电流积分。
2. `soc_strategy_update()` 执行端点/OCV/低端修正。
3. `SOC_Result_Pass()` 把内部状态同步到 `g_stCellInfoReport.SocElement`。

### 2.3 策略更新顺序

`soc_strategy_update()` 内部依次调用：

1. `soc_update_discharge_sag_hold()`
2. `soc_apply_startup_ocv_correction()`
3. `soc_apply_terminal_sync()`
4. `soc_apply_discharge_terminal_tracking()`
5. `soc_apply_deferred_ocv_step()`
6. `soc_apply_discharge_ocv_tracking()`
7. `soc_apply_idle_ocv_tracking()`

顺序含义：

- 先更新放电压降/回弹 holdoff 状态，后续修正都能使用这个阻断条件。
- 启动端点、满电/空电端点优先级最高。
- 放电低压端点优先于一般 OCV 目标，保证低电末端可信。
- 静置 OCV 最后执行，只负责记录目标，不在静置时快速跳变。

## 3. 各策略条件

### 3.1 启动阶段一次性修正

函数：`soc_apply_startup_ocv_correction()`

触发条件：

- 只允许执行一次，靠 `g_soc_strategy_state.startup_checked` 防重复。
- 采样必须有效：`VCELLMIN/VCELLMAX` 在 OCV 有效窗口内，且 `VCELLMAX >= VCELLMIN`。
- 必须空闲：`u16Ichg == 0` 且 `u16IDischg == 0`。

当前动作：

- 只有 `ocv_soc == 0`、`VCELLMIN <= SOC_0_VAL`、当前 SOC 大于 0 时，才向 0 走一步。
- 该逻辑不是普通启动全量 OCV 校准，避免开机大跳变。

### 3.2 满电/静置空电端点同步

函数：`soc_apply_terminal_sync()`

满电同步：

- 条件：`VCELLMAX >= SOC_100_VAL = 4180mV` 且 `VCELLMIN >= SOC_FULL_SYNC_MIN_MV = 3980mV`。
- 不要求 `isCHG()`。
- 锁定：`SOC_FULL_LOCK_TICKS = 5 * 60`，约 60s。
- 步进：`SOC_FULL_SYNC_STEP_TICKS = 10`，约 2s/1%。

静置空电同步：

- 条件：空闲、`VCELLMIN <= SOC_0_VAL = 3000mV`、`VCELLMAX <= SOC_EMPTY_SYNC_MAX_MV = 3200mV`。
- 锁定：`SOC_EMPTY_LOCK_TICKS = 25`，约 5s。
- 步进：`SOC_EMPTY_SYNC_STEP_TICKS = 5`，约 1s/1%。

### 3.3 放电低压端点

函数：`soc_apply_discharge_terminal_tracking()`

端点表：

| 最低单体电压 | 目标 SOC | sag holdoff |
| ---: | ---: | --- |
| `3300mV` | 12% | 阻断 |
| `3200mV` | 6% | 阻断 |
| `3150mV` | 3% | 阻断 |
| `3050mV` | 1% | 不阻断 |
| `3000mV` | 0% | 不阻断 |

速度：

- 非 0% 端点使用 `soc_discharge_terminal_step_ticks()`，先按容量/电流动态计算，再按低端电压区间限幅。
- `3000mV -> 0%` 持续 `SOC_DSG_EMPTY_LOCK_TICKS = 10`，约 2s 后直接调用 `soc_apply_real_value(0u, 1u)`，同步 real/display SOC。

### 3.4 延迟 OCV 目标

函数：`soc_apply_deferred_ocv_step()`

目标来源：

- 主要来自 `soc_apply_idle_ocv_tracking()` latch 的 `deferred_ocv_target`。

方向约束：

- target 高于 real SOC：只允许 `isCHG()` 时上调。
- target 低于 real SOC：只允许 `isDSG()` 时下调。

速度：

- 充电上调：`SOC_CHG_DEFERRED_OCV_STEP_TICKS = 20s/1%`。
- 放电下调：`soc_discharge_gap_correction_step_ticks(current_soc, target_soc)` 动态计算。

放电动态公式：

```text
自然 1% 时间(s) = 36 * CapacityFactory / IDSG
```

之后按误差档乘以 `4/3/2`，并限制在 `10s/1% ~ 180s/1%`。

### 3.5 放电 OCV 跟踪

函数：`soc_apply_discharge_ocv_tracking()`

进入条件：

- 必须 `isDSG()`。
- OCV 样本必须有效。
- `soc_discharge_sag_hold_active()` 为 false。

电流分档：

- `IDSG <= 20`：低电流，diff 阈值 1%。
- `IDSG <= 50`：中电流，diff 阈值 5%。
- `IDSG <= 100`：高电流，diff 阈值 8%。
- `IDSG > 100`：超高电流，diff 阈值 12%。

动作：

- 只允许向下修正。
- 稳定计数达到分档要求后，再按 `soc_discharge_gap_correction_step_ticks()` 的容量/电流自适应速度每次下修 1%。

### 3.6 静置 OCV 目标记录

函数：`soc_apply_idle_ocv_tracking()`

进入条件：

- 必须空闲：`u16Ichg == 0` 且 `u16IDischg == 0`。
- OCV 样本有效。
- 单体压差不超过 `SOC_OCV_IDLE_CELL_DELTA_MAX_MV = 100mV`。
- weighted cell voltage 相邻变化不超过 `SOC_OCV_IDLE_SLOPE_MAX_MV = 8mV`。
- 至少稳定 `SOC_OCV_IDLE_MIN_STABLE_TICKS = 5 * 30`，约 30s。
- `idle_ocv_confidence >= SOC_OCV_CONFIDENCE_TARGET = 80`。

动作：

- OCV 与 real SOC 差值小于 `SOC_OCV_RUNTIME_DIFF_THRESHOLD = 3%` 时清掉 deferred target。
- 差值达到阈值后只记录 `deferred_ocv_target`，不在静置时快速跳变。
- 已有 target 时，`SOC_OCV_IDLE_TARGET_REFRESH_TICKS = 5 * 60`，约 60s 才刷新一次目标。

静置下修例外：

- 只有 `real_soc - deferred_ocv_target >= SOC_IDLE_STATIC_DOWN_DIFF_THRESHOLD = 10%` 时，才允许静置下修。
- 下修速度是 `SOC_LONG_REST_DOWN_STEP_TICKS = 5 * 60 * 30`，约 30min/1%。

## 4. OCV 如何换算 SOC

函数链：

- `soc_estimate_ocv_percent()`
- `soc_estimate_percent_from_cell_mv()`

计算方式：

1. 使用加权单体电压：
   - `weighted_mv = (VCELLMIN * 3 + VCELLMAX) / 4`
2. 查保守 OCV 分段表：
   - `3000mV = 0%`
   - `3200mV = 5%`
   - `3300mV = 10%`
   - `3400mV = 15%`
   - `3500mV = 25%`
   - `3600mV = 35%`
   - `3650mV = 45%`
   - `3700mV = 55%`
   - `3750mV = 65%`
   - `3800mV = 75%`
   - `3900mV = 85%`
   - `4000mV = 92%`
   - `4100mV = 97%`
   - `4180mV = 100%`

注意：当前 `SOC_OCV_VALID_MAX_MV = 4000mV`，所以高 SOC 静置 OCV 主要仍依赖满电端点同步，是否放开高压 OCV 需要实测确认。

## 5. 写回 Flash

文件：[`vendor/ble_sample/app.c`](../tc_ble_single_sdk/vendor/ble_sample/app.c)

主循环末尾每次都调用：

```c
soc_kv_store_update_and_log_if_changed(
    SOC_Calculate_Element.u8SOC_Now,
    SOC_Calculate_Element.u8DSG_SOC_Int,
    SOC_Calculate_Element.u32Cycle_times);
```

文件：[`vendor/ble_sample/soc_kv_store.c`](../tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c)

- 只有当 `soc / dsg / cycle` 任一变化时才会写入。
- 如果值没变，则直接返回，不做 Flash 写操作。
- display SOC 不写入 KV。

## 6. 时间尺度总结

| 场景 | 时间尺度 |
| --- | --- |
| 主策略循环 | `200ms/次` |
| 满电同步锁定 | 约 `60s` |
| 满电上调 | 约 `2s/1%` |
| 静置空电同步锁定 | 约 `5s` |
| 静置空电下调 | 约 `1s/1%` |
| 放电 3000mV 安全归零 | 约 `2s` 后直接同步 0 |
| 静置 OCV 最小稳定 | 约 `30s` + confidence |
| 静置 OCV target 刷新 | 约 `60s` |
| 静置大误差下修 | 约 `30min/1%` |
| 放电 OCV/延迟目标下修 | 按 `36 * CapacityFactory / IDSG` 自适应，限幅 `10s/1% ~ 180s/1%` |

## 7. 关键文件

- [`vendor/ble_sample/app.c`](../tc_ble_single_sdk/vendor/ble_sample/app.c)
- [`vendor/ble_sample/SocEnhance.c`](../tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c)
- [`vendor/ble_sample/SocEnhance.h`](../tc_ble_single_sdk/vendor/ble_sample/SocEnhance.h)
- [`vendor/ble_sample/soc_kv_store.c`](../tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c)
