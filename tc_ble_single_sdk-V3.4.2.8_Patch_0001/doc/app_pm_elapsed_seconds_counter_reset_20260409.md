# app_pm_take_elapsed_seconds 调用与低压计时清零说明

## 背景

`vendor/ble_sample/app.c` 中的 `blt_pm_proc()` 使用 `app_pm_take_elapsed_seconds()` 获取实际经过秒数，并用该秒数推进低功耗深睡计数。

这套逻辑替代了原先“主循环执行一次近似计 1 秒”的方式，避免主循环抖动、BLE suspend 或短时间阻塞导致 1 小时、24 小时、30 分钟等阈值计时偏慢。

## app_pm_take_elapsed_seconds 的作用

`app_pm_take_elapsed_seconds()` 内部读取 `pm_get_32k_tick()`：

1. 第一次调用只记录当前 32K tick，返回 `0`。
2. 后续调用用 `now_tick_32k - last_tick_32k` 计算经过 tick。
3. 将经过 tick 加上不足 1 秒的 `pending_tick_32k`。
4. 按 `APP_PM_TICKS_PER_SEC = 32000` 转换为经过秒数。
5. 不足 1 秒的余数继续保留到下一次调用。

因此它输出的是“自上次调用以来完整经过的秒数”，不是“调用次数”。

## 已发现的问题

`blt_pm_proc()` 原先使用 `if / else if` 按以下优先级累计不同深睡计数：

1. `u16VCellMin <= 2500`
2. `u16VCellMin <= 2800 && !u16Ichg` 或 `deepsleep_en`
3. `u16VCellMin <= 3000 && !u16Ichg`
4. `System_ErrFlag.u8ErrFlag_Com_AFE1 == 1`

旧逻辑只在所有分支都不满足时统一清零。这样会产生计时残留：

- 先在 `<=2500mV` 分支累计 59 分钟；
- 电压升到 `2700mV` 后进入另一个分支，`sleep_veryvlow_cnt` 不清零；
- 后续再短暂跌回 `<=2500mV`，可能很快触发 1 小时深睡。

这不是 `app_pm_take_elapsed_seconds()` 本身的问题，而是互斥分支之间没有清掉已经不再满足条件的计数。

## 本次修正

本次在每个互斥分支进入时，主动清零其它分支的计数：

- 进入 `<=2500mV` 分支时，清零 `sleep_vlow_cnt`、`sleep_vnormal_cnt`、`afe_comm_err_sleepcnt`。
- 进入 `<=2800mV && !u16Ichg` 或 `deepsleep_en` 分支时，清零 `sleep_veryvlow_cnt`、`sleep_vnormal_cnt`、`afe_comm_err_sleepcnt`。
- 进入 `<=3000mV && !u16Ichg` 分支时，清零 `sleep_veryvlow_cnt`、`sleep_vlow_cnt`、`afe_comm_err_sleepcnt`。
- 进入 AFE 通信异常分支时，清零 `sleep_veryvlow_cnt`、`sleep_vlow_cnt`、`sleep_vnormal_cnt`。
- 所有条件都不满足时，仍然统一清零所有计数。

同时保留当前工作区中 `deepsleep_en` 分支的 10 分钟起始计数意图，但改为只在 `sleep_vlow_cnt` 低于 10 分钟时补到 10 分钟，避免每次循环都重新赋值导致计数无法继续增长。

## 影响

修正后，各低压分支的深睡计时只代表当前连续满足该分支条件的时间，不会继承其它电压区间的历史残留。

这会让深睡触发更符合“对应电压条件连续满足指定时间”的语义；如果电压在阈值附近来回跳变，计时会随分支切换清零，需要连续满足当前分支条件才会触发对应深睡。

## 验证建议

建议用可控输入分别验证：

1. `u16VCellMin <= 2500` 连续累计到 1 小时触发深睡。
2. 从 `<=2500` 切到 `2501~2800` 后，再切回 `<=2500`，确认 `sleep_veryvlow_cnt` 从 0 重新累计。
3. 从 `<=2800` 切到 `2801~3000` 后，再切回 `<=2800`，确认 `sleep_vlow_cnt` 从 0 重新累计。
4. 从 `<=3000` 切到正常电压或充电状态后，再切回 `<=3000`，确认 `sleep_vnormal_cnt` 从 0 重新累计。
5. AFE 通信异常分支与低压分支互相切换时，确认不会继承旧分支的残留计数。
