# ADC Test

## 测试目的

检测 BMS 采样数据中明显不合理的电压、电流和温度，降低传感链路异常导致危险输出继续打开的风险。

## 测试原理

`safety_adc_check.c` 基于 `g_stCellInfoReport` 做宽范围合理性检查：

- 单节电压最大值不超过 `SAFETY_CELL_VOLT_MAX_MV`。
- 单节电压最小值不低于 `SAFETY_CELL_VOLT_MIN_MV`。
- 充放电电流不超过 `SAFETY_CURRENT_MAX_A10`。
- 温度编码值不超过 `SAFETY_TEMP_RAW_MAX`。

如果 AFE 数据尚未形成有效帧，检查会跳过，避免上电初期误判。

## 测试步骤

1. 正常运行 BMS，确认不误触发。
2. 人为构造超范围电压/温度/电流数据。
3. 观察是否进入 Fail Safe。

## 故障注入方法

```c
#define SAFETY_TEST_ENABLE 1
#define SAFETY_INJECT_ADC_FAULT 1
```

## PASS 条件

- 正常采样范围内不触发 Fail Safe。
- 明显越界或故障注入时触发 Fail Safe。

## FAIL 条件

- 正常数据误触发。
- 明显越界后仍保持危险输出。
