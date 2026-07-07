# Clock Test

## 测试目的

检测主时钟配置异常、时钟停止或明显不可用。

## 测试原理

`safety_clock_test.c` 当前检查：

- `clock_get_system_clk()` 是否等于 `SYS_CLK_TYPE`。
- 短循环后 `clock_time()` 是否前进。

## 测试步骤

1. `clock_init()` 后执行启动时钟检查。
2. 运行期 100 ms 安全任务中重复检查。
3. 观察时钟异常时是否进入 Fail Safe。

## 故障注入方法

```c
#define SAFETY_TEST_ENABLE 1
#define SAFETY_INJECT_CLOCK_FAULT 1
```

## PASS 条件

- 系统时钟配置匹配。
- `clock_time()` 正常递增。

## FAIL 条件

- 系统时钟配置不匹配。
- 系统计数不前进。
- 故障注入后未进入 Fail Safe。

## 后续增强

认证阶段应增加 32k 与系统主时钟的交叉频率测量，并给出允许误差窗口。
