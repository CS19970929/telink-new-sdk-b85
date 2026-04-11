# `soc_strategy_update` 调用链与 OCV/SOC 更新逻辑

## 1. 结论先行

`OCV` 不是在每次循环都直接改 `SOC`，而是由 `APP_SOC_IntEnhance_Ctrl()` 驱动的策略层按条件更新。

- 外层入口每 `200ms` 执行一次。
- `OCV` 相关更新分为三类：
  - 启动阶段一次性修正
  - 满电 / 亏电终点同步
  - 空闲状态下的缓慢跟踪修正
- 真正写回闪存的是 `soc_kv_store_update_and_log_if_changed()`，它只是“有变化才落盘”，不是 `OCV` 更新本身。

## 2. 调用链

### 2.1 主循环触发

文件：[`vendor/ble_sample/app.c`](../tc_ble_single_sdk/vendor/ble_sample/app.c)

- `main_loop()` 中：
  - `clock_time_exceed(test_task_tick, 1000 * 200)` 成立时，每 `200ms` 进入一次：
  - `APP_SOC_IntEnhance_Ctrl();`

### 2.2 SOC 策略入口

文件：[`vendor/ble_sample/SocEnhance.c`](../tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c)

- `APP_SOC_IntEnhance_Ctrl()` 的执行顺序：
  1. `SOC_State_Transfer()` / `SOC_Cont_AH_Int_CHG()` / `SOC_Cont_AH_Int_DSG()` 之一
  2. `soc_strategy_update();`
  3. `SOC_Result_Pass();`

### 2.3 策略更新拆分

`soc_strategy_update()` 内部依次调用：

1. `soc_apply_startup_ocv_correction()`
2. `soc_apply_terminal_sync()`
3. `soc_apply_idle_ocv_tracking()`

## 3. 什么时候会用 OCV 更新 SOC

## 3.1 启动阶段一次性修正

函数：`soc_apply_startup_ocv_correction()`

触发条件：

- 只允许执行一次，靠 `g_soc_strategy_state.startup_checked` 防重复。
- 当前采样必须有效：
  - `VCELLMIN` 在 `2500mV ~ 4300mV`
  - `VCELLMAX` 不能小于 `VCELLMIN`
  - 单体压差 `u16VCellDelta <= 120mV`
- 必须空闲：
  - `u16Ichg == 0`
  - `u16IDischg == 0`

实际更新条件：

- `ocv_soc == 0` 且 `VCELLMIN <= SOC_0_VAL`，并且当前 `SOC > 3`
- 或 `ocv_soc == 100` 且 `VCELLMAX >= SOC_100_VAL`，并且当前 `SOC < 97`

动作：

- `soc_apply_real_value(ocv_soc, 1)`
- `soc_reset_integral_accumulator()`

这类修正只在“明显满电/空电端点”做硬校正，避免开机后 SOC 大跳变。

## 3.2 终点同步

函数：`soc_apply_terminal_sync()`

### 满电同步

触发条件：

- 当前是充电状态：`isCHG()`
- `VCELLMAX >= SOC_100_VAL`
- `VCELLMIN >= SOC_FULL_SYNC_MIN_MV`

锁定计数：

- 连续满足 `20` 次才生效
- 主循环周期是 `200ms`
- 所以满电同步约需要 `4s`

动作：

- 强制 `SOC = 100`
- 清空积分状态

### 亏电同步

触发条件：

- 当前空闲：`soc_idle_for_ocv()`
- `VCELLMIN <= SOC_0_VAL`
- `VCELLMAX <= SOC_EMPTY_SYNC_MAX_MV`

锁定计数：

- 连续满足 `25` 次才生效
- 主循环周期是 `200ms`
- 所以亏电同步约需要 `5s`

动作：

- 强制 `SOC = 0`
- 清空积分状态

## 3.3 空闲状态下的 OCV 跟踪

函数：`soc_apply_idle_ocv_tracking()`

这是最接近“持续用 OCV 修正 SOC”的逻辑。

### 进入条件

- 当前必须空闲：
  - `u16Ichg == 0`
  - `u16IDischg == 0`
- 采样必须有效，和启动阶段一致

### 稳定等待

- 先累计 `idle_stable_ticks`
- 达到 `SOC_OCV_IDLE_STABLE_TICKS = 5 * 60 * 30 = 9000` 次后才开始调整
- 因为每次 `200ms` 调一次，所以等价于：
  - `9000 * 200ms = 1800000ms = 30分钟`

### 调整节奏

- 稳定后每累计 `SOC_OCV_IDLE_ADJUST_TICKS = 5 * 60 = 300` 次才允许调整一次
- 等价于：
  - `300 * 200ms = 60000ms = 60秒`

### 调整规则

- 先算 `ocv_soc`
- 当前 SOC 和 OCV SOC 的差值小于 `8%` 时不动
- 差值达到 `8%` 或更大时：
  - 每次只朝 OCV 方向走 `1%`
  - 不会一次跳到目标值

动作：

- `soc_apply_real_value(next_soc, 1)`
- `soc_reset_integral_accumulator()`

## 4. OCV 是怎么换算成 SOC 的

函数链：

- `soc_estimate_ocv_percent()`
- `soc_estimate_percent_from_cell_mv()`

计算方式：

1. 用 `Vmin` 和 `Vmax` 做加权平均：
   - `weighted_mv = (VCELLMIN * 3 + VCELLMAX) / 4`
2. 在线性区间内映射到 `0% ~ 100%`
3. 使用边界：
   - `SOC_0_VAL = 3000mV`
   - `SOC_100_VAL = 4180mV`

因此它不是查表法，而是一个简单的线性估算。

## 5. 相关入口和数据落地

### 启动初始化

文件：[`vendor/ble_sample/app.c`](../tc_ble_single_sdk/vendor/ble_sample/app.c)

- 启动时先调用 `App_AFEGet()`
- 然后：
  - `soc_kv_store_init()`
  - `soc_kv_data_t d = soc_kv_store_get();`
  - `soc_param_lib_init(&d);`

说明：

- 启动 SOC / DSG / Cycle 会从 KV 恢复
- 随后 `soc_strategy_update()` 才开始按电压和电流做修正

### 写回闪存

文件：[`vendor/ble_sample/app.c`](../vendor/ble_sample/app.c)

- 主循环末尾每次都调用：
  - `soc_kv_store_update_and_log_if_changed(SOC_Calculate_Element.u8SOC_Now, SOC_Calculate_Element.u8DSG_SOC_Int, SOC_Calculate_Element.u32Cycle_times);`

文件：[`vendor/ble_sample/soc_kv_store.c`](../tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c)

- 只有当 `soc / dsg / cycle` 任一变化时才会写入
- 如果值没变，则直接返回，不做 flash 写操作

## 6. 你关心的“多久更新一次”总结

按当前代码，`OCV` 参与 `SOC` 修正的时间尺度是：

- 主策略循环：`200ms/次`
- 启动硬修正：首次满足条件时立即发生，且只执行一次
- 满电终点同步：约 `4秒`
- 亏电终点同步：约 `5秒`
- 空闲 OCV 跟踪：
  - 先静置约 `30分钟`
  - 之后每 `60秒` 评估一次
  - 每次最多只调 `1%`

## 7. 关键文件

- [`vendor/ble_sample/app.c`](../tc_ble_single_sdk/vendor/ble_sample/app.c)
- [`vendor/ble_sample/SocEnhance.c`](../tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c)
- [`vendor/ble_sample/SocEnhance.h`](../tc_ble_single_sdk/vendor/ble_sample/SocEnhance.h)
- [`vendor/ble_sample/soc_kv_store.c`](../tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c)
