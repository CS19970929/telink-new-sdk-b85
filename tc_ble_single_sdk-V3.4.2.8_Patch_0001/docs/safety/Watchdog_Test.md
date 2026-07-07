# Watchdog Test

## 测试目的

确认 WDT 已启用，Fail Safe 后 MCU 能通过 WDT 复位回到安全启动流程。

## 测试原理

当前运行期检查 `reg_wd_ctrl1 & FLD_WD_EN`。Fail Safe 中不再喂狗，等待现有 WDT 超时复位。

启动破坏性 WDT 复位测试由 `SAFETY_WATCHDOG_STARTUP_RESET_TEST_ENABLE` 控制，默认关闭。

## 测试步骤

1. 正常构建确认 `MODULE_WATCHDOG_ENABLE=1`。
2. 运行期检查 WDT 使能位。
3. 注入任一安全故障。
4. 观察危险输出关闭后 MCU 是否由 WDT 复位。

## 故障注入方法

```c
#define SAFETY_TEST_ENABLE 1
#define SAFETY_INJECT_WDT_FAULT 1
```

## PASS 条件

- 正常运行时 WDT 处于 enable。
- Fail Safe 后不喂狗。
- WDT 超时后 MCU 复位。

## FAIL 条件

- WDT 未启用。
- Fail Safe 后仍持续喂狗。
- WDT 不复位 MCU。
