# 低功耗纯软件补时方案说明

## 背景

当前项目在 `8251 / 512K` 配置下，低功耗与工厂运行时间统计存在两个问题：

1. `runtime` 依赖主循环里的“每秒任务 + 60 次凑 1 分钟”推进，主循环抖动、BLE suspend 或 deep sleep 后都会丢失实际经过时间。
2. `blt_pm_proc()` 里的 `1 小时 / 24 小时 / 30 分钟` 深睡阈值，原先也是按“主循环每秒执行一次”累加，严格说不是按真实经过时间推进。

本次实现选择方案 A：纯软件补时，不新增硬件，不增加 flash 写入频率，不通过周期性唤醒破坏低功耗。

## 设计原则

1. 统一使用低功耗 32k 时基做累计。
2. deep sleep 前只保存进入睡眠时的 `32k tick`，不写 flash。
3. 唤醒后读取当前 `32k tick`，用差值补偿 `runtime`。
4. 主循环中的计时改为“按经过时间推进”，而不是“执行一次算一秒”。

## 具体实现

### 1. runtime 改为基于 32k tick 累计

`runtime.c` 新增了以下能力：

- `Runtime_Poll()`
  - 每次主循环调用一次。
  - 读取 `pm_get_32k_tick()`。
  - 用当前 tick 与上次 tick 的差值累计分钟。
  - 超过 1 分钟时按实际经过分钟数补偿 `g_runtime_min`。

- `Runtime_PrepareForDeepSleep()`
  - 在进入 `DEEPSLEEP_MODE` 前调用。
  - 把当前 `32k tick` 写入 `DEEP_ANA_REG6~10`。
  - 不写 flash。

- `Runtime_CancelPendingDeepSleep()`
  - 仅在 `cpu_sleep_wakeup()` 没有真正进入 deep sleep、而是直接返回时清理上下文。
  - 防止下次上电误判为发生过一次 deep sleep。

- `Runtime_Init()`
  - 启动时先从 flash 恢复 runtime。
  - 再读取 `DEEP_ANA_REG6~10` 中保存的睡前 tick。
  - 如果校验通过，则以“当前 tick - 睡前 tick”的结果补偿 runtime。
  - 补偿完成后清空 deep sleep 上下文，避免重复补偿。

### 2. deep sleep 上下文保存位置

本次使用了 B85 deep sleep 保持寄存器：

- `DEEP_ANA_REG6`
- `DEEP_ANA_REG7`
- `DEEP_ANA_REG8`
- `DEEP_ANA_REG9`
- `DEEP_ANA_REG10`

用途如下：

- `REG6~REG9`：保存 32 位 `pm_get_32k_tick()`
- `REG10`：保存 1 字节校验值

这样可以在不落 flash 的前提下，跨 deep sleep 保留一次睡前时间戳。

### 3. 低功耗阈值改为按实际经过秒数推进

`app.c` 中的 `blt_pm_proc()` 原先用：

- `clock_time_exceed(..., 1s)`
- 然后 `++sleep_veryvlow_cnt / ++sleep_vlow_cnt / ++sleep_vnormal_cnt`

现在改成：

- 每次读取 `pm_get_32k_tick()`
- 累计真实经过的 `elapsed_sec`
- 各阈值计数器按 `elapsed_sec` 递增

这样：

- BLE suspend 下，不会因为主循环没刚好跑满 1 次/秒而少计时间
- 主循环偶发阻塞时，也不会丢掉整段时间

## 对低功耗的影响

本方案对低功耗影响很小，原因是：

1. 只增加了 `pm_get_32k_tick()` 和少量内存计算。
2. deep sleep 前后不写 flash。
3. 不引入周期性唤醒。
4. 不改变现有 BLE suspend / deep sleep 进入策略。

## 精度边界

本方案已经解决了“软件丢秒 / deep sleep 不补时”的问题，但精度仍然受当前低功耗时基约束：

- 当前工程的低功耗 32k 源仍是内部 `32k RC`
- 因此分钟级统计会明显优于原实现
- 但它仍不是外部 `32.768k crystal` 或独立 RTC 那种精度
- 由于使用的是 32 位 `pm_get_32k_tick()` 差值补偿，单次连续睡眠时长应控制在一个 tick 回绕周期内；按当前内部 `32k RC` 口径，工程上应认为单次 deep sleep 补时窗口不超过约 `37` 小时

换句话说：

- 本次修复的是“逻辑计时错误”
- 不是把内部 RC 变成高精度 RTC

## 适用结论

对当前项目，这个方案适合：

- 工厂模式运行时间统计
- deep sleep 前后运行分钟数补偿
- `1h / 24h / 30min` 这类低功耗阈值控制

如果后续目标变成“几天到几周 deep sleep 后仍要求更高时间精度”，则应再升级到：

1. 外部 `32.768k` 晶振
2. 或独立 RTC

## 本次修改文件

- `vendor/ble_sample/runtime.c`
- `vendor/ble_sample/runtime.h`
- `vendor/ble_sample/app.c`

## 验证建议

建议后续至少补做以下验证：

1. 工厂模式运行 5~10 分钟，核对 `runtime` 是否按分钟稳定递增。
2. 在工厂模式下进入一次 deep sleep，等待 2~5 分钟后唤醒，确认 `runtime` 被正确补偿。
3. 分别验证：
   - 充电中
   - 放电中
   - 蓝牙连接态
   - 蓝牙广播空闲态
4. 验证 `cpu_sleep_wakeup()` 未真正进入睡眠时，不会出现重复补时。
