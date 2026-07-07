# Fault Injection Test

## 测试目的

为认证和台架测试提供可重复的故障注入入口，验证各安全诊断能进入 Fail Safe。

## 测试原理

所有故障注入通过 `safety_config.h` 宏控制，默认关闭。打开后对应测试函数直接返回失败。

## 测试步骤

1. 选择单个故障注入宏。
2. clean build 固件。
3. 烧录并上电。
4. 观察是否进入 Fail Safe。
5. 记录故障类型、输出状态和 WDT 复位行为。

## 故障注入方法

示例：

```c
#define SAFETY_TEST_ENABLE 1
#define SAFETY_INJECT_ADC_FAULT 1
```

支持宏：

- `SAFETY_INJECT_CPU_FAULT`
- `SAFETY_INJECT_FLASH_FAULT`
- `SAFETY_INJECT_RAM_FAULT`
- `SAFETY_INJECT_CLOCK_FAULT`
- `SAFETY_INJECT_WDT_FAULT`
- `SAFETY_INJECT_ADC_FAULT`
- `SAFETY_INJECT_AFE_COMM_FAULT`

## PASS 条件

- 每个故障注入均能触发对应安全故障。
- MOS/危险输出关闭。
- WDT 复位路径可观测。

## FAIL 条件

- 故障注入后系统继续正常运行。
- 危险输出未关闭。
- 故障类型无法追踪。
