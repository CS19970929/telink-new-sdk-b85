# 低功耗与老化 runtime 计时口径说明

> 状态：2026-05-24 已更新。原“deep sleep 纯软件补时”方案已废弃，当前源码不再对老化 runtime 做 deep sleep 补偿。

## 当前结论

当前老化模式的计时口径是：

- 老化总时长仍是 `FACTORY_TIME_LIMIT_MIN = 60 * 24 * 3`，即 `4320min / 72h / 3 天`。
- 老化 runtime 只统计 BMS awake 且主循环正常运行时的时间。
- 老化模式允许进入 `DEEPSLEEP_MODE`。
- 老化模式不允许进入当前代码里的 `enter_rtc_mode()` / BLE suspend RTC 低功耗路径。
- deep sleep 唤醒后 MCU 会复位重启，Runtime 只从 Flash 记录恢复已保存的 `runtime_min`，不把 deep sleep 期间的时间补进老化 runtime。

## 源码实现

### 1. awake 时间累计

`Runtime_Poll()` 每轮主循环调用一次：

- 读取 `pm_get_32k_tick()`。
- 用当前 tick 和上次 tick 的差值累计 `g_runtime_pending_tick_32k`。
- 满 1 分钟后增加 `g_runtime_min`。
- 每增加 `RUNTIME_SAVE_INTERVAL_MIN`，当前为 1 分钟，写一次 runtime Flash 记录。

因此，正常运行时 runtime 是按 32k tick 推进，而不是按“主循环跑了多少次”硬凑。

### 2. deep sleep 不补偿

当前 `Runtime_PrepareForDeepSleep()` 不再保存 sleep enter tick，也不写 analog register。

进入 deep sleep 前，它只重新锚定当前 32k tick：

- 如果 `cpu_sleep_wakeup()` 没有真正进入 deep sleep 并返回，`Runtime_CancelPendingDeepSleep()` 会重新锚定 tick，避免下一轮 awake 差值异常。
- 如果已经进入 deep sleep，MCU 唤醒后复位，RAM 状态丢失；`Runtime_Init()` 重新从 Flash runtime record 恢复已保存的整分钟数。

也就是说：

- deep sleep 期间不计入老化。
- deep sleep 前不足 1 分钟的 pending tick 不跨复位保存。
- 不再存在 32-bit `pm_get_32k_tick()` 跨 deep sleep 回绕补偿问题。
- 不再占用 `DEEP_ANA_REG6~10` 或 `PM_ANA_REG_WD_CLR_BUF0~4` 保存 runtime sleep 上下文。

### 3. RTC mode 禁止

`blt_pm_proc()` 后半段在以下条件下会禁用 BLE suspend 并调用 `quit_rtc_mode()`：

- 充电 pin active
- bus mux 非 `BUS_STATE_OWC_IDLE`
- 有放电电流
- `Runtime_GetMode() == MODE_FACTORY`
- OTA working

因此只要 Runtime 处于 `MODE_FACTORY`，就不会进入 `enter_rtc_mode()`。

注意：如果只是通过 `enter_fac_mode(true)` 打开 MOS，但没有让 `Runtime` 进入 `MODE_FACTORY`，则不会触发这条 RTC mode 阻断条件。这也是 `0x1102 = 0x0003` 当前只打开 MOS、不重置 Runtime 工厂状态的风险点。

## 与旧方案的差异

旧方案曾计划：

- deep sleep 前保存 32k tick 到 analog register。
- 唤醒后用 `now_tick - sleep_enter_tick` 补偿 runtime。

当前已明确不采用该方案，原因是当前产品口径要求“只统计正常运行状态下的老化时间”。deep sleep 本身不属于正常运行时间，不应计入 3 天老化。

## 精度边界

当前实现的计时精度边界：

- awake 状态下按 32k tick 换算分钟，分钟级计时比按主循环次数计时稳定。
- runtime 每 1 分钟保存一次 Flash，因此掉电或 deep sleep 前小于 1 分钟的残余运行时间不会保存。
- 低功耗 32k 源仍是内部 `32k RC`，不是高精度 RTC；3 天累计仍会受 RC 精度影响。

## 验证建议

建议后续验证：

1. 工厂模式 awake 运行 5~10 分钟，确认 `Runtime_Get_runtime()` 按分钟递增并落盘。
2. 工厂模式运行若干分钟后进入 deep sleep，等待一段时间再唤醒，确认 runtime 只恢复到睡前已保存分钟数，不增加睡眠时间。
3. 工厂模式下确认不会执行 `enter_rtc_mode()`，ADC/RTC mode 相关 IO 应保持退出 RTC mode 状态。
4. 写 `0x1102 = 0x0003` 的上位机路径需要单独验证：它当前只调用 `enter_fac_mode(true)`，不是完整 Runtime 工厂模式入口。
