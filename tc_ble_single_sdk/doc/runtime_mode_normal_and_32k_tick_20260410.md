# runtime 进入 `MODE_NORMAL` 的完整逻辑说明

本文梳理 `vendor/ble_sample/runtime.*` 的状态机、`pm_get_32k_tick()` 的含义、时间是怎么累计的，以及 deep sleep 前后的补偿逻辑。

## 先说结论

- 当前工程里，`runtime` 进入 `MODE_NORMAL` 的阈值不是 7 天，而是 3 天。
- 代码实际使用的是 `FACTORY_TIME_LIMIT_MIN = 60 * 24 * 3`，也就是 `4320` 分钟。
- `runtime` 的时间不是用“秒计数器”直接累加，而是用 `pm_get_32k_tick()` 读到的 32k 时钟 tick 做差值，再换算成分钟。
- 进入 `MODE_NORMAL` 后，`runtime` 不会继续往上加，后续累加逻辑会直接退出。

## 相关代码位置

| 功能 | 位置 |
| --- | --- |
| `MODE_NORMAL` 阈值 | `tc_ble_single_sdk/vendor/ble_sample/runtime.h:6` |
| `runtime` 初始化、恢复、切模式 | `tc_ble_single_sdk/vendor/ble_sample/runtime.c:55-66`, `185-407` |
| 主循环调用 `Runtime_Poll()` | `tc_ble_single_sdk/vendor/ble_sample/app.c:1700-1704` |
| 睡前/唤醒补偿 | `tc_ble_single_sdk/vendor/ble_sample/app.c:131-146` |
| 读取 32k tick | `tc_ble_single_sdk/drivers/TC321X/clock.c:201-237` |
| `pm_get_32k_tick()` 官方说明 | `tc_ble_single_sdk/drivers/TC321X/lib/include/pm/pm.h:322-330`, `377-381` |

## 1. `MODE_NORMAL` 到底什么时候进入

`runtime` 相关模式只有两个：

- `MODE_FACTORY`
- `MODE_NORMAL`

### 1.1 启动后的默认状态

`runtime.c` 里静态变量 `g_mode` 初始写的是 `MODE_NORMAL`，但是这个只是编译期初值。  
真正启动时会先执行 `Runtime_Init()`，而 `Runtime_Init()` 一开始就调用 `runtime_set_initial_state()`，把模式重置成 `MODE_FACTORY`。

也就是说，**实际运行逻辑里，初始化后默认先进入工厂模式，而不是 normal**。

### 1.2 直接进入 `MODE_NORMAL` 的两个特殊情况

`Runtime_Init()` 里有两个特殊分支会让设备直接处于 `MODE_NORMAL`：

1. 运行时分区不存在，或者 `runtime_flash_base() == 0`
2. 从 flash 恢复出来的 `g_runtime_min >= FACTORY_TIME_LIMIT_MIN`

第一种情况属于“没有 runtime 分区/不启用 runtime 存储”的兜底逻辑。  
第二种情况表示累计运行时间已经达到阈值。

### 1.3 正常运行时什么时候切换

如果还在 `MODE_FACTORY`，那么主循环会持续调用 `Runtime_Poll()`：

- `app.c` 的 `main_loop()` 每轮都会调用一次 `Runtime_Poll()`
- `Runtime_Poll()` 读取当前 32k tick
- 计算和上一次 tick 的差值
- 把差值换算成分钟
- 累加到 `g_runtime_min`

当 `g_runtime_min` 达到 `4320` 分钟时：

- `runtime_apply_elapsed_minutes()` 会把 `g_runtime_min` 钳制到 `FACTORY_TIME_LIMIT_MIN`
- 然后调用 `runtime_finish_factory_mode()`
- `runtime_finish_factory_mode()` 把 `g_mode` 改成 `MODE_NORMAL`
- 同时会尝试把当前 runtime 写回 flash
- 最后调用 `enter_fac_mode(false)`

所以，**在当前代码里，runtime 进入 `MODE_NORMAL` 的真实条件就是“累计 runtime 达到 4320 分钟”**。

## 2. 32k tick 是什么

`pm_get_32k_tick()` 返回的是 **32k 时钟域的 tick 计数值**，不是秒，不是分钟。

从实现看，它做了两件事：

1. 从模拟寄存器 `0x63 ~ 0x60` 依次读出 4 个字节，拼成一个 32 位 tick 值
2. 连续读两次做稳定性判断，避免刚好踩在 tick 翻转边界时读到“半新半旧”的脏值

### 2.1 为什么要读两次

因为寄存器是按字节读的，可能出现这种情况：

- 第一次读到的低字节还是旧 tick
- 高字节已经是新 tick
- 结果拼出来的 32 位值就不是一个真实存在的 tick

所以实现里会比较两次读取结果：

- 如果两次结果差值小于 2，认为结果稳定，直接返回
- 还有一个特殊分支用来兼容低位翻转时的临界情况

### 2.2 `pm_get_32k_tick()` 读出来的数怎么理解

可以把它理解成：

- 32k 时钟每跳一次，tick 加 1
- tick 是一个 32 位无符号计数器
- 计数会回绕

按 32k 频率换算：

- `1 秒 = 32000 tick`
- `1 分钟 = 32000 * 60 = 1,920,000 tick`
- 32 位 tick 回绕周期约 `2^32 / 32000 ≈ 37.27 小时`

所以它适合做“前后差值”，不适合把一个绝对 tick 当真实时间戳长期保存。

## 3. `runtime` 的时间是怎么计算的

`runtime` 的核心思路不是“每秒加 1”，而是“每次看到新的 tick，就算它比上一次多了多少”。

### 3.1 主路径：`Runtime_Poll()`

`Runtime_Poll()` 的逻辑可以概括成下面这样：

```c
now_tick = pm_get_32k_tick();
delta_tick = now_tick - last_tick;
total_tick = pending_tick + delta_tick;
elapsed_min = total_tick / 1,920,000;
pending_tick = total_tick % 1,920,000;
runtime_min += elapsed_min;
```

这里有三个关键点：

1. `last_tick` 记录上一次采样的 32k tick
2. `pending_tick` 用来保存不足 1 分钟的剩余 tick
3. 只有凑够整分钟，`runtime_min` 才会增加

### 3.2 为什么要有 `pending_tick`

如果没有 `pending_tick`，比如每次只过了 30 秒就会被截断成 0 分钟，长期看会丢时间。

`pending_tick` 的作用就是把“还差一点才满 1 分钟”的那部分留到下一次继续累计。

举个例子：

- 这次累计到 `1,900,000 tick`
- 还差 `20,000 tick` 才满 1 分钟
- 这 `20,000 tick` 会放进 `pending_tick`
- 下一次再补一点，就可能凑出 1 分钟

这样不会因为轮询不是整分钟而丢失时间。

### 3.3 `Runtime_1MinTask()` 是什么

`runtime.c` 里还有一个 `Runtime_1MinTask()`，它的实现很简单：

- 直接把 `1` 分钟加到 runtime

但在当前工程里，我没有找到它的调用点。  
也就是说，**当前真正生效的主路径是 `Runtime_Poll()`，不是 `Runtime_1MinTask()`**。

## 4. deep sleep 时为什么还要额外处理

`Runtime_Poll()` 只适合“主循环持续运行”的场景。  
一旦进入 deep sleep，主循环停了，tick 采样也停了，所以必须额外补偿睡眠时间。

### 4.1 睡前做什么

`app_note_sleep_and_enter_deepsleep()` 里，在真正调用 `cpu_sleep_wakeup()` 前会先执行：

1. `Runtime_PrepareForDeepSleep()`
2. `cpu_sleep_wakeup(...)`
3. `Runtime_CancelPendingDeepSleep()`

`Runtime_PrepareForDeepSleep()` 的行为是：

- 如果当前已经是 `MODE_NORMAL`，就只清理睡眠 tick 记录，不再存储
- 如果还在 `MODE_FACTORY`，就读取一次 `pm_get_32k_tick()`
- 把这个 tick 写进模拟寄存器，作为“入睡时刻”
- 同时把 `g_runtime_last_tick_32k` 更新成这个值

### 4.2 醒来后怎么补偿

`Runtime_Init()` 在启动恢复时，会尝试读取睡前保存的 tick：

- 如果读到了，就计算 `now_tick - sleep_enter_tick`
- 再把这段差值按分钟补进 runtime

这样睡眠期间经过的时间不会丢。

### 4.3 为什么 `Runtime_CancelPendingDeepSleep()` 也要调用

如果 `cpu_sleep_wakeup()` 最终没有真正进入 PM，或者已经醒来返回了，那么必须把睡眠 tick 清掉，并重新把 `g_runtime_last_tick_32k` 设成当前 tick。

否则下一次 `Runtime_Poll()` 可能会把这段“假睡眠时间”也算进去。

## 5. flash 里的 runtime 是怎么存的

`runtime` 不是每次都整块重写，而是做成了顺序日志：

- 每条记录包含 `seq / runtime_min / flag / crc / commit`
- 启动时扫描整个 runtime 区域，取 `seq` 最大且校验通过的记录作为最新值
- 写入时按顺序追加
- 写新扇区前会先擦除扇区
- 记录写到一半时，`commit` 还没写进去，就会被扫描逻辑当成无效记录

这套设计的目的，是尽量避免掉电后把最后一次写坏，也避免每次都把整个区域整块重写。

### 5.1 保存周期

当前代码里 `RUNTIME_SAVE_INTERVAL_MIN = 1`，也就是 **每累计满 1 分钟就会尝试保存一次**。

因此，`runtime_min` 增加到新分钟时，通常会立刻落 flash。

## 6. 为什么代码注释和实际行为不一致

`runtime.h` 里的注释还写着“7天”，但当前宏值是：

```c
#define FACTORY_TIME_LIMIT_MIN   (60 * 24 * 3)
```

这对应的是 3 天，不是 7 天。

所以现在判断 `MODE_NORMAL` 时，**应该以代码为准**，不是以旧注释为准。

如果后面要统一文档和注释，建议把“7天”的旧描述一起修正掉，避免再次误判。

## 7. 用一句话概括整个流程

1. 上电后 `Runtime_Init()` 先把模式重置为 `MODE_FACTORY`
2. 从 flash 恢复历史 runtime
3. 如果已经到 4320 分钟，直接进入 `MODE_NORMAL`
4. 否则主循环通过 `Runtime_Poll()` 用 `pm_get_32k_tick()` 持续累计时间
5. deep sleep 前后用额外的 tick 存取把睡眠时间补回来
6. 一旦累计到阈值，就调用 `runtime_finish_factory_mode()`，切换到 `MODE_NORMAL`

## 8. 你如果只想记住最关键的点

- `MODE_NORMAL` 不是“运行一会儿就进”，而是 **累计 3 天 runtime 后进入**
- 这里的 runtime 是按 **32k tick -> 分钟** 换算出来的
- `pm_get_32k_tick()` 是一个 **硬件 32k 计数器读数**
- `pending_tick` 用来保存不满 1 分钟的剩余 tick
- deep sleep 的时间不会丢，会在醒来后补偿

