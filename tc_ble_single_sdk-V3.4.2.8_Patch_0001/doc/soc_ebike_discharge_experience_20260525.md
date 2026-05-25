# eBike 放电骑行 SOC 体验优化说明

日期：2026-05-25

## 1. 目标

本次优化面向 eBike 骑行放电体验，优先级按下面顺序处理：

1. 用户体验优先：骑行中不因为电机负载压降造成 SOC 疯狂掉格。
2. 准确性必须守住：整个生命周期内 SOC 误差不能靠低端一次性大幅修正来兜底。
3. 过放保护前必须显示 0%：当前 AFE 过放保护点是 `2750mV`，SOC 显示 0% 锚点保持 `SOC_0_VAL = 3000mV`，给保护点预留约 `250mV` 余量。
4. 不改 SOH 逻辑：SOH 继续只按循环次数计算，不做容量学习，不用本次放电修正反推 SOH。
5. 不改 deep sleep、老化、KV 布局、BLE/Modbus/SIF 协议。

源码真相源：

- `tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c`
- `tc_ble_single_sdk/vendor/ble_sample/tests_flash_quick_check.py`

## 2. 理论依据

骑行放电时端电压不是纯 OCV：

```text
端电压 = OCV(SOC, 温度, 老化) - I * R - 极化电压
```

如果只按固定时间修正，会有两个问题：

- 大电流起步、爬坡时，端电压被负载拉低，固定时间太短会误判成真实低 SOC。
- 容量不同的版型，1% 容量对应的真实时间不同，固定 `2s/1%` 会让大容量车型掉格过快，也会让小容量车型节奏不自然。

本次放电修正改为容量和电流自适应：

```text
CapacityFactory 单位 = Ah * 10
IDSG 单位 = A * 10
自然放电 1% 时间(s) = 36 * CapacityFactory / IDSG
```

例子：

| 版型容量 | 电流 | 自然 1% 时间 |
| ---: | ---: | ---: |
| `CapacityFactory = 87`，8.7Ah | 10A | `36 * 87 / 100 = 31.3s` |
| `CapacityFactory = 116`，11.6Ah | 10A | `41.8s` |
| `CapacityFactory = 180`，18Ah | 10A | `64.8s` |

因此所有放电过程的 OCV/低端修正都不能写死某个车型容量，必须从 `conf.h` 的 `CapacityFactory` 取值。

## 3. 本次实现

### 3.1 SOH 保持循环次数模型

保持不变的链路：

```c
SOC_Calculate_Element.soh = bms_soh_from_cycle(soc_cycle_to_u16(SOC_Calculate_Element.u32Cycle_times));
SOC_Calculate_Element.u32CapFull = (SOC_Calculate_Element.u32CapFactory * SOC_Calculate_Element.soh) / 100u;
```

含义：

- `cycle` 仍按 100% 等效放电累计。
- `bms_soh_from_cycle()` 的分段退化规则不变。
- `CapacityFactory` 仍是版型额定容量，不被骑行修正逻辑动态改写。
- 本次新增的容量自适应只用于“1% 修正应该多快”，不改变 SOH 计算来源。

### 3.2 放电 OCV/延迟目标改为容量自适应

新增核心参数：

| 参数 | 当前值 | 作用 |
| --- | ---: | --- |
| `SOC_TICKS_PER_SECOND` | `5` | 200ms 调度换算，`5 ticks = 1s` |
| `SOC_DSG_CORR_STEP_MIN_TICKS` | `5 * 10` | 正常放电修正最快 `10s/1%` |
| `SOC_DSG_CORR_STEP_MAX_TICKS` | `5 * 180` | 正常放电修正最慢 `180s/1%` |
| `SOC_DSG_CORR_LARGE_GAP_PERCENT` | `10%` | 误差大时加快修正 |
| `SOC_DSG_CORR_MID_GAP_PERCENT` | `6%` | 中等误差档 |
| `SOC_DSG_CORR_SMALL_GAP_MUL` | `4` | 小误差按自然速度的 1/4 介入 |
| `SOC_DSG_CORR_MID_GAP_MUL` | `3` | 中误差按自然速度的 1/3 介入 |
| `SOC_DSG_CORR_LARGE_GAP_MUL` | `2` | 大误差按自然速度的 1/2 介入 |

实际逻辑：

- 静置 OCV 只建立 `deferred_ocv_target`。
- 目标高于真实 SOC：只在充电时消化，当前 `SOC_CHG_DEFERRED_OCV_STEP_TICKS = 20s/1%`。
- 目标低于真实 SOC：只在放电时消化，速度由 `CapacityFactory` 和 `IDSG` 动态计算。
- 放电 OCV 持续跟踪也使用同一套动态速度，不再固定 `20s/1%`。

这样可以让 8.7Ah、11.6Ah、18Ah 等不同版型都按自身容量自然掉格，不写死某个产品。

### 3.3 静置 OCV 少校准

静置策略调整为“少动，只记录，必要时慢修”：

- 静置且 OCV 样本有效。
- 单体压差不超过 `SOC_OCV_IDLE_CELL_DELTA_MAX_MV = 100mV`。
- weighted cell voltage 相邻采样变化不超过 `SOC_OCV_IDLE_SLOPE_MAX_MV = 8mV`。
- 至少稳定 `SOC_OCV_IDLE_MIN_STABLE_TICKS = 5 * 30`，约 30s。
- `idle_ocv_confidence` 达到 `SOC_OCV_CONFIDENCE_TARGET = 80` 后才允许 latch OCV target。
- 已有 target 时，`SOC_OCV_IDLE_TARGET_REFRESH_TICKS = 5 * 60`，约 60s 才刷新一次目标。

长静置下修也加了门槛：

- 只有 `real_soc - deferred_ocv_target >= SOC_IDLE_STATIC_DOWN_DIFF_THRESHOLD = 10%` 时，才允许静置慢速下修。
- 下修速度仍是 `SOC_LONG_REST_DOWN_STEP_TICKS = 30min/1%`。

这满足“静置时尽量少校准，误差很大才修正”的体验目标。

### 3.4 放电 sag/rebound holdoff

大电流压降阻断仍保留，并改为电流强度 + 回弹稳定：

| 放电电流 | holdoff |
| --- | ---: |
| `IDSG > 50` 且 `<= 100` | `SOC_DSG_SAG_HOLDOFF_HIGH_TICKS = 60s` |
| `IDSG > 100` | `SOC_DSG_SAG_HOLDOFF_VHIGH_TICKS = 90s` |

释放前还看回弹稳定：

- weighted cell voltage 相邻变化不超过 `SOC_DSG_REBOUND_SLOPE_MAX_MV = 12mV`。
- 连续稳定 `SOC_DSG_REBOUND_STABLE_TICKS = 10s`。
- 未稳定时，holdoff 会继续保持至少 10s。

结果：

- 起步、加速、爬坡时不把瞬态压降直接当成真实低 SOC。
- 松油门后电压仍在回弹时不急着修正。
- 回弹稳定后再允许放电 OCV 和非关键低端规则介入。

### 3.5 低电末端安全收敛

低端电压表仍保留，但速度改为动态限制：

| 最低单体电压 | 目标 SOC | 说明 |
| ---: | ---: | --- |
| `3300mV` | `12%` | 进入低端上限，仍偏体验保护 |
| `3200mV` | `6%` | 中低端上限 |
| `3150mV` | `3%` | 低端上限 |
| `3050mV` | `1%` | 关键低端，允许绕过 sag holdoff |
| `3000mV` | `0%` | 安全端点，锁定约 2s 后直接同步 real/display SOC 到 0 |

关键变化：

- `3300/3200/3150mV` 这些非关键低端仍受 sag/rebound holdoff 阻断。
- `3050mV -> 1%` 不再被 sag holdoff 阻断，避免临近低端仍显示过高。
- `3000mV -> 0%` 是安全端点，不做 1% 慢慢掉，而是 `SOC_DSG_EMPTY_LOCK_TICKS = 2s` 后直接同步到 0。

这样做的原因：

- AFE 过放保护点是 `2750mV`。
- `3000mV` 到 `2750mV` 只有 `250mV` 余量。
- 如果真实 SOC 误差很大，继续坚持 1% 慢速下降，可能出现保护前还没显示到 0 的风险。

安全端点直接同步 0 是唯一例外；正常中高 SOC 区间仍坚持慢速、小步、容量自适应修正。

## 4. 调参建议

### 4.1 首选不动的参数

| 参数 | 建议 |
| --- | --- |
| `CapacityFactory` | 只能按版型真实额定容量配置，不要拿它调体验 |
| `bms_soh_from_cycle()` | 继续按 cycle 计算 SOH，本次不建议改 |
| `SOC_0_VAL` | 当前 3000mV 是显示 0% 锚点，低于 AFE 2750mV 保护点前置，不建议随意降低 |
| AFE `CUV = 2750mV` | 这是保护点，不是体验参数 |

### 4.2 如果用户反馈骑行掉格太快

优先顺序：

1. 增大 `SOC_DSG_SAG_HOLDOFF_HIGH_TICKS` / `SOC_DSG_SAG_HOLDOFF_VHIGH_TICKS`。
2. 降低 `SOC_DSG_REBOUND_SLOPE_MAX_MV`，让回弹稳定判断更严格。
3. 增大 `SOC_DSG_CORR_SMALL_GAP_MUL` / `SOC_DSG_CORR_MID_GAP_MUL`，让普通误差修正更慢。
4. 不建议先改 `SOC_0_VAL` 或 `CapacityFactory`。

### 4.3 如果用户反馈低电偏高

优先顺序：

1. 检查 `CapacityFactory` 是否和版型一致。
2. 检查 AFE 电流缩放和 deadband，确认积分没有长期少算放电。
3. 适当降低 `SOC_DSG_CORR_LARGE_GAP_PERCENT` 或 `SOC_DSG_CORR_MID_GAP_PERCENT`，让大误差更早进入较快修正。
4. 必要时把 `SOC_DSG_TERMINAL_L3_MV` 提高少量，让 1% 更早出现。
5. 不建议把 `SOC_0_VAL` 降到接近 2750mV，因为会损失保护前显示 0% 的余量。

### 4.4 如果停车静置 SOC 变化明显

优先顺序：

1. 增大 `SOC_OCV_CONFIDENCE_TARGET`。
2. 增大 `SOC_OCV_IDLE_MIN_STABLE_TICKS`。
3. 降低 `SOC_OCV_IDLE_SLOPE_MAX_MV`。
4. 增大 `SOC_IDLE_STATIC_DOWN_DIFF_THRESHOLD`，例如从 10% 到 12%。

## 5. 验证场景

| 场景 | 输入特征 | 预期 |
| --- | --- | --- |
| 平路轻载 | `IDSG <= 20`，电压变化小 | 积分主导，OCV 轻微辅助，不明显掉格 |
| 起步加速 | `IDSG > 50`，最低单体瞬间下探 | 非关键低端和放电 OCV 被 holdoff 阻断 |
| 爬坡大负载 | `IDSG > 100`，电压持续较低 | 90s holdoff，避免负载压降误校准 |
| 松油门回弹 | 电流下降，电压仍上升 | holdoff 等待回弹稳定 |
| 低电末端 | `VCELLMIN <= 3050mV` | 允许向 1% 收敛，不被 sag holdoff 阻断 |
| 安全端点 | `VCELLMIN <= 3000mV` 持续约 2s | real/display SOC 直接同步 0%，早于 2750mV 过放保护 |
| 停车静置 | 电压变化逐步变小 | confidence 达标后只 latch target；误差小于 10% 不静置下修 |

## 6. 边界说明

本次优化解决的是校准介入时机和节奏问题，不替代真实容量和电流链路校准。长期准确性的根基仍然是：

- `CapacityFactory` 与不同版型真实容量一致。
- AFE 电流采样、缩放、deadband 准确。
- 单体电压采样可靠，压差字段可信。
- `SOC_0_VAL = 3000mV` 与 AFE `CUV = 2750mV` 的保护余量保持一致。
- SOH 继续按 cycle 退化，不由本次策略动态学习。
