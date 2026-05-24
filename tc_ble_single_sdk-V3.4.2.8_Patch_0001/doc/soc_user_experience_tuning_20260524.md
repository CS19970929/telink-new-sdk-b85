# SOC 用户体验优化与可调参数说明

日期：2026-05-24

## 1. 文档定位

本文同步当前 `ble_sample` SOC 模块的最新修改点，并把可调参数、影响范围和验证方法整理成调参文档。

源码真相源：

- `tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c`
- `tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c`
- `tc_ble_single_sdk/vendor/ble_sample/app.c`
- `tc_ble_single_sdk/vendor/ble_sample/tests_flash_quick_check.py`

时间换算前提：

- `APP_SOC_IntEnhance_Ctrl()` 由 `main_loop()` 每 200ms 调用一次。
- 大多数 `*_TICKS` 宏以 200ms 为一个 tick。
- 近似换算：`5 ticks = 1s`，`300 ticks = 60s`，`9000 ticks = 30min`。

## 2. 本次已同步的 SOC 修改点

### 2.1 满电同步不再要求电流条件

当前满电端点同步条件是：

```c
if ((VCELLMAX >= SOC_100_VAL) && (VCELLMIN >= SOC_FULL_SYNC_MIN_MV))
```

含义：

- 最高单体达到 `SOC_100_VAL = 4180mV`。
- 最低单体不低于 `SOC_FULL_SYNC_MIN_MV = SOC_100_VAL - 200 = 3980mV`。
- 不再要求 `isCHG()`，也不要求尾电流。
- 条件持续 `SOC_FULL_LOCK_TICKS = 5 * 60`，约 60s 后开始向 100% 收敛。
- 收敛速度是 `SOC_FULL_SYNC_STEP_TICKS = 10`，约 2s 上调 1%。

用户体验影响：

- 用户满充后即使充电状态位不可靠，也能在电压满足条件后逐步显示到 100%。
- 不会瞬间跳到 100%，仍按 1% 慢速上调。
- 风险是单体电压被瞬态拉高时可能触发满电同步，因此用 60s 锁定和最低单体 3980mV 约束降低误判。

### 2.2 real SOC 与 display SOC 分离

当前内部真实 SOC 与对外显示 SOC 分离：

- `SOC_Calculate_Element.u8SOC_Now`：真实 SOC，用于积分、校准、KV 持久化。
- `g_soc_display_soc`：显示 SOC，用于 BLE/Modbus/SIF 对外上报。
- `SOC_Result_Pass()` 上报 `get_soc_display()`，不再直接上报 `get_soc_real()`。

显示跟随速度：

- `SOC_DISPLAY_STEP_TICKS = 5`，约 1s 跟随 1%。

用户体验影响：

- 内部真实 SOC 可以被端点或 OCV 策略每次修正 1%，显示侧再按 1s/1% 平滑跟随。
- BLE/Modbus 上看到的 SOC 不会因为内部连续修正而突兀跳变。
- KV 仍保存真实 SOC，不保存显示 SOC；重启后显示 SOC 会初始化为真实 SOC。

### 2.3 静置 OCV 改为延迟目标，不直接跳变

当前静置 OCV 策略不再在静置时立即把 SOC 拉向 OCV 估算值，而是先记录一个 `deferred_ocv_target`：

- 静置且样本有效。
- 稳定 `SOC_OCV_IDLE_STABLE_TICKS = 5 * 60`，约 60s。
- 每 `SOC_OCV_IDLE_ADJUST_TICKS = 5 * 60`，约 60s 评估一次 OCV 目标。
- OCV 与真实 SOC 差值小于 `SOC_OCV_RUNTIME_DIFF_THRESHOLD = 3%` 时不修正。
- 差值达到阈值后只记录目标，不在静置时向上修正。

随后在运行状态中消化目标：

- 如果目标高于真实 SOC，只允许在 `isCHG()` 时上调。
- 如果目标低于真实 SOC，只允许在 `isDSG()` 时下调。
- 运行中按 `SOC_DEFERRED_OCV_ACTIVE_STEP_TICKS = 5 * 2`，约 2s 修正 1%。

长静置向下例外：

- 如果长时间静置且 OCV 目标低于真实 SOC，允许极慢向下修正。
- `SOC_LONG_REST_DOWN_STEP_TICKS = 5 * 60 * 30`，约 30min 下调 1%。
- 该计数现在按 200ms tick 累计，避免被 60s OCV 评估周期二次放大。

用户体验影响：

- 停车静置时不会因为 OCV 估算突然把 SOC 拉高或快速拉低。
- 充电或放电开始后，历史 OCV 目标会按实际运行方向逐步消化，显示更符合用户预期。
- 长时间静置确实存在自恢复需求时，最多按 30min/1% 慢速下调。

### 2.4 低压放电端点改为表驱动

当前低压放电端点不再由多段 `if` 硬编码，而是由 `g_soc_dsg_terminal_rules[]` 统一控制：

| 最低单体电压上限 | 目标 SOC 上限 | 低电流步进 | 中电流步进 | 高电流步进 | 超高电流步进 | 是否受压降 holdoff 阻断 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `SOC_0_VAL = 3000mV` | 0% | 5 tick | 10 tick | 20 tick | 30 tick | 否 |
| `SOC_DSG_TERMINAL_L3_MV = 3050mV` | 1% | 5 tick | 10 tick | 20 tick | 30 tick | 是 |
| `SOC_DSG_TERMINAL_L2_MV = 3150mV` | 3% | 8 tick | 12 tick | 20 tick | 30 tick | 是 |
| `SOC_DSG_TERMINAL_L1_MV = 3200mV` | 6% | 10 tick | 15 tick | 25 tick | 35 tick | 是 |
| `SOC_DSG_TERMINAL_START_MV = 3300mV` | 12% | 15 tick | 20 tick | 30 tick | 40 tick | 是 |

电流档位来自 `soc_discharge_ocv_current_band(IDSG)`：

- `IDSG <= 20`：低电流，约不超过 2A。
- `IDSG <= 50`：中电流，约不超过 5A。
- `IDSG <= 100`：高电流，约不超过 10A。
- `IDSG > 100`：超高电流。

用户体验影响：

- 电压越低，真实 SOC 上限越低。
- 电流越大，修正越慢，降低负载压降导致的提前掉格。
- 3000mV 到 0% 是最终安全端点，不受压降 holdoff 阻断。

### 2.5 放电压降 holdoff

当前放电电流大于 `SOC_DSG_SAG_HOLD_CURR_MIN = SOC_DSG_OCV_MID_CURR_MAX = 50`，约高于 5A 时：

- 置位 `dsg_sag_hold_ticks = SOC_DSG_SAG_HOLDOFF_TICKS = 5 * 30`，约 30s。
- holdoff 期间阻断放电 OCV 向下修正。
- holdoff 期间阻断低压端点表中标记 `sag_hold_blocks = 1` 的规则。
- `SOC_0_VAL = 3000mV` 到 0% 的最终端点不阻断。

用户体验影响：

- 大电流瞬间压降不会立刻把 SOC 拉低。
- 电压真正跌到 3000mV 时仍可进入 0% 保护性收敛。

## 3. 可调参数与影响

### 3.1 显示平滑参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_DISPLAY_STEP_TICKS` | `5`，约 1s/1% | 显示更平滑，用户看到的变化更慢 | 显示更跟手，但可能更容易感到跳格 | 太大时真实 SOC 已变化，外部显示滞后 |

建议：

- 优先调这个参数改善显示体验。
- 不影响真实 SOC、cycle、SOH、KV 保存。
- 会影响 BLE/Modbus/SIF 上报的 `Soc` 和按显示 SOC 换算的 `CapacityNow`。

### 3.2 满电端同步参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_100_VAL` | `4180mV` | 更难触发 100%，减少误判 | 更容易满电同步 | 过低会提前显示满电，过高会长期不到 100% |
| `SOC_FULL_SYNC_MIN_MV` | `SOC_100_VAL - 200 = 3980mV` | 要求最低单体更高，均衡不足时更难满电 | 更容易触发满电 | 过低会掩盖单体差异 |
| `SOC_FULL_LOCK_TICKS` | `5 * 60`，约 60s | 抗瞬态更强，到 100% 更慢 | 到 100% 更快 | 过短可能被瞬态高压误触发 |
| `SOC_FULL_SYNC_STEP_TICKS` | `10`，约 2s/1% | 上调更慢、更平滑 | 上调更快 | 过短会像跳变，过长会满充后显示滞后 |

建议：

- 若用户反馈“满充后长期不到 100%”，先看实际单体电压是否满足 `4180/3980mV`，再考虑降低 `SOC_FULL_SYNC_MIN_MV` 或缩短 `SOC_FULL_LOCK_TICKS`。
- 若用户反馈“插充瞬间跳满”，优先增大 `SOC_FULL_LOCK_TICKS`，不要直接提高 `SOC_100_VAL`。

### 3.3 空电端同步参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_0_VAL` | `3000mV` | 更晚进入 0%，低电风险增加 | 更早进入 0%，保护更保守 | 需要与保护阈值、BMS 放电截止体验一致 |
| `SOC_EMPTY_SYNC_MAX_MV` | `SOC_0_VAL + 200 = 3200mV` | 更容易在静置低端归 0 | 更严格，归 0 更慢 | 过高可能把单体不均衡误判为空 |
| `SOC_EMPTY_LOCK_TICKS` | `25`，约 5s | 归 0 更稳 | 归 0 更快 | 过短会受采样抖动影响 |
| `SOC_EMPTY_SYNC_STEP_TICKS` | `5`，约 1s/1% | 归 0 更慢 | 归 0 更快 | 过快会造成低端掉格明显 |

建议：

- `SOC_0_VAL` 是低端锚点，调整前必须和欠压保护、负载压降、恢复电压一起评估。
- 普通体验问题优先调锁定时间和步进速度，不优先改锚点电压。

### 3.4 静置 OCV 参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_OCV_VALID_MIN_MV` | `2000mV` | 有效窗口变窄 | 有效窗口变宽 | 过宽会采纳异常样本 |
| `SOC_OCV_VALID_MAX_MV` | `4000mV` | 可过滤高压端 OCV | 允许更高电压参与 OCV | 当前 OCV 表到 4180mV，但有效样本只到 4000mV，高 SOC OCV 会被过滤 |
| `SOC_OCV_CELL_DELTA_MAX_MV` | `200mV` | 单体差异大时更容易禁用 OCV | OCV 更容易参与 | 过大可能在不均衡状态误判 |
| `SOC_OCV_IDLE_STABLE_TICKS` | `5 * 60`，约 60s | 静置判定更稳 | 更快形成 OCV 判断 | 过短会被电压回弹过程干扰 |
| `SOC_OCV_IDLE_ADJUST_TICKS` | `5 * 60`，约 60s | OCV 目标更新更慢 | OCV 目标更新更快 | 过快会追随短期电压波动 |
| `SOC_OCV_RUNTIME_DIFF_THRESHOLD` | `3%` | 小偏差不修，显示更稳定 | 更积极校准 | 过小会频繁微调，过大长期偏差不修 |
| `SOC_DEFERRED_OCV_ACTIVE_STEP_TICKS` | `5 * 2`，约 2s/1% | 运行中消化 OCV 目标更慢 | 消化更快 | 过快会造成充/放电过程中掉格或涨格明显 |
| `SOC_LONG_REST_DOWN_STEP_TICKS` | `5 * 60 * 30`，约 30min/1% | 长静置下调更慢 | 长静置下调更快 | 过短会让停车后 SOC 下降过明显 |

建议：

- 不建议先改 `SOC_OCV_VALID_MAX_MV`，除非已有高 SOC 静置数据证明 4000mV 以上 OCV 需要参与校准。
- 低成本优化优先调 `SOC_OCV_RUNTIME_DIFF_THRESHOLD`、`SOC_DEFERRED_OCV_ACTIVE_STEP_TICKS` 和 `SOC_LONG_REST_DOWN_STEP_TICKS`。

### 3.5 放电 OCV 参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_DSG_OCV_LOW_CURR_MAX` | `20`，约 2A | 低电流档覆盖更宽 | 更快进入中电流档 | 档位错误会影响修正速度 |
| `SOC_DSG_OCV_MID_CURR_MAX` | `50`，约 5A | 中电流档覆盖更宽 | 更快进入高电流档 | 同时影响 sag hold 触发门槛 |
| `SOC_DSG_OCV_HIGH_CURR_MAX` | `100`，约 10A | 高电流档覆盖更宽 | 更快进入超高电流档 | 高负载下修正可能偏激或偏慢 |
| `SOC_DSG_OCV_LOW_DIFF_THRESHOLD` | `1%` | 低电流下更少修正 | 低电流下更敏感 | 过小会频繁修正 |
| `SOC_DSG_OCV_MID_DIFF_THRESHOLD` | `5%` | 中电流更保守 | 中电流更积极 | 需要结合压降测试 |
| `SOC_DSG_OCV_HIGH_DIFF_THRESHOLD` | `8%` | 高电流更保守 | 高电流更积极 | 大电流下过低容易提前掉 SOC |
| `SOC_DSG_OCV_VHIGH_DIFF_THRESHOLD` | `12%` | 超高电流更保守 | 超高电流更积极 | 大负载场景最容易误判 |
| `SOC_DSG_OCV_*_STABLE_TICKS` | `8/12/20/30` | 需要更长稳定时间 | 更快进入修正 | 过短会受瞬态压降影响 |
| `SOC_DSG_OCV_*_ADJUST_TICKS` | 均 `5 * 20`，约 20s | 修正更慢 | 修正更快 | 过快会掉格明显 |

建议：

- 大电流压降体验不好时，优先增大高/超高电流档的 `DIFF_THRESHOLD` 或 `STABLE_TICKS`。
- 轻载长时间偏高时，优先检查低电流档阈值和 OCV 表，不要直接改低端端点。

### 3.6 放电低压端点表

| 参数/表项 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_DSG_TERMINAL_START_MV` | `3300mV`，目标 12% | 更早进入低端 SOC 上限 | 更晚进入低端上限 | 过高会高 SOC 时提前被压低 |
| `SOC_DSG_TERMINAL_L1_MV` | `3200mV`，目标 6% | 更早压到 6% | 更晚压到 6% | 影响低端剩余里程感知 |
| `SOC_DSG_TERMINAL_L2_MV` | `3150mV`，目标 3% | 更早压到 3% | 更晚压到 3% | 低端显示与保护策略要一致 |
| `SOC_DSG_TERMINAL_L3_MV` | `3050mV`，目标 1% | 更早压到 1% | 更晚压到 1% | 过低可能用户看到有电但很快断电 |
| `SOC_0_VAL` | `3000mV`，目标 0% | 更晚归 0 | 更早归 0 | 必须与放电截止保护配套 |
| 表内 `step_ticks[]` | 见 `g_soc_dsg_terminal_rules[]` | 掉格更慢 | 掉格更快 | 过快影响体验，过慢影响低端可信度 |
| 表内 `sag_hold_blocks` | 0% 不阻断，其余阻断 | 更多规则受压降保护 | 更少规则受压降保护 | 阻断太多会低端显示偏高 |
| `SOC_DSG_EMPTY_LOCK_TICKS` | `10`，约 2s | 归 0 更稳 | 归 0 更快 | 过短可能误归 0 |

建议：

- 如果用户反馈“大负载一开 SOC 掉很多”，先调 `sag_hold` 和高电流 `step_ticks`，不要先提高低端目标 SOC。
- 如果用户反馈“低端显示还有电但马上保护”，先检查 `SOC_DSG_TERMINAL_L3_MV`、`SOC_0_VAL` 和目标 SOC。

### 3.7 放电压降 holdoff 参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_DSG_SAG_HOLD_CURR_MIN` | `50`，约 5A，源码判断为 `IDSG > 50` | 更难触发压降保护 | 更容易触发压降保护 | 过低会导致正常放电校准长期被阻断 |
| `SOC_DSG_SAG_HOLDOFF_TICKS` | `5 * 30`，约 30s | 压降后等待更久 | 更快恢复校准 | 过长低端可能偏高，过短抗瞬态不足 |

建议：

- 大负载工具、电机类场景可适当延长 holdoff。
- 小电流持续放电场景不要把触发门槛设太低，否则会抑制真实低端修正。

### 3.8 容量、循环与持久化参数

| 参数 | 当前值/来源 | 影响 | 调整风险 |
| --- | --- | --- | --- |
| `SOC_INTEGRAL_PERIOD_MS` | `200ms` | 电流积分时间基准 | 必须和调度周期一致，不能单独改 |
| `CapacityFactory` | `conf.h`，当前 `87` | 工厂容量，影响满容量和当前容量 | 改错会导致 SOC 积分斜率错误 |
| `SOC_CAPACITY_UNITS_PER_AH` | `3600 * 10` | 内部容量单位 | 不建议改，协议和算法一起受影响 |
| `SOC_REPORT_CAPACITY_DIVISOR` | `360` | 对外容量单位换算 | 改动会影响 BLE/Modbus/SIF 容量字段 |
| `SOC_EQUIV_CYCLE_PERCENT` | `100` | 100% 等效放电记 1 cycle | 不建议改，会影响 SOH 退化 |
| `SOC_PARAM_DEFAULT_SOC` | `FAC_INIT_soc`，当前 60 | 空 Flash 默认 SOC | 影响新板/重置后的初始显示 |
| `SOC_PARAM_DEFAULT_DSG` | `0` | 默认放电累计 | 不建议改 |
| `SOC_PARAM_DEFAULT_CYCLE` | `0` | 默认循环次数 | 不建议改 |

建议：

- `SOC_INTEGRAL_PERIOD_MS` 是算法与调度契约，不是体验参数。
- `CapacityFactory` 是容量真实性参数，不是显示平滑参数。
- 体验问题优先调校准/显示参数，不要用改容量掩盖。

## 4. 对其它模块的影响

### 4.1 对 BLE/Modbus/SIF 上报的影响

会影响：

- `g_stCellInfoReport.SocElement.u16Soc`：当前上报显示 SOC。
- `g_stCellInfoReport.SocElement.u16CapacityNow`：当前按显示 SOC 换算。
- `u16Soh/u16CapacityFull/u16CapacityFactory/u16Cycle_times`：仍来自真实 cycle/SOH/capacity。

不会改变：

- BLE 协议结构。
- Modbus 寄存器地址。
- SIF 公共包字段定义。

### 4.2 对 KV/Flash 的影响

会影响：

- 真实 SOC 的修正频率可能改变 hot KV 写入频率。
- display SOC 不落 KV。

不会改变：

- `soc_kv_store` key 布局。
- `flash_kv32` transaction 机制。
- cold KV 与 Runtime Flash 分区。

### 4.3 对 deep sleep 和老化 runtime 的影响

本次 SOC 参数不直接改变：

- `blt_pm_proc()` 的 deep sleep 条件。
- `Runtime_Poll()` 的老化计时。
- `Runtime_PrepareForDeepSleep()` 的行为。
- 老化模式不允许 RTC mode 的策略。
- deep sleep 不补偿老化时间的口径。

需要注意：

- deep sleep 唤醒后 MCU 会复位，SOC 从 hot KV 读取真实 SOC，display SOC 初始化为真实 SOC。
- 如果 SOC 修正速度加快，SOC hot KV 写入频率可能增加，但不会改变 deep sleep 进入条件和时间。

## 5. 建议调参顺序

1. 先调显示体验：`SOC_DISPLAY_STEP_TICKS`。
2. 再调满电体验：`SOC_FULL_LOCK_TICKS`、`SOC_FULL_SYNC_STEP_TICKS`、`SOC_FULL_SYNC_MIN_MV`。
3. 再调大负载低端体验：`SOC_DSG_SAG_HOLD_CURR_MIN`、`SOC_DSG_SAG_HOLDOFF_TICKS`、端点表内高电流 `step_ticks`。
4. 再调静置 OCV：`SOC_OCV_RUNTIME_DIFF_THRESHOLD`、`SOC_DEFERRED_OCV_ACTIVE_STEP_TICKS`、`SOC_LONG_REST_DOWN_STEP_TICKS`。
5. 最后才调 OCV 表和 `SOC_OCV_VALID_MAX_MV`，因为这需要实测电芯曲线支持。

## 6. 验证建议

| 场景 | 验证点 | 预期 |
| --- | --- | --- |
| 满电电压到位但电流状态异常 | 不要求 `isCHG()` | 满足 `4180/3980mV` 且持续约 60s 后，按约 2s/1% 上调到 100% |
| 大电流瞬时放电 | sag hold | 大于约 5A 触发 30s holdoff，非 0% 低压端点和放电 OCV 不应立刻拉低 |
| 轻载放电到低端 | 端点表 | 3300/3200/3150/3050/3000mV 分段把 SOC 上限压到 12/6/3/1/0% |
| 静置 2 分钟后 OCV 与 SOC 差异大 | deferred target | 静置时只记录目标，不直接向上跳；运行后按方向逐步消化 |
| 长静置 OCV 低于真实 SOC | long rest down | 目标稳定后约 30min 才下调 1% |
| deep sleep 唤醒 | KV 恢复 | MCU 复位后真实 SOC 从 hot KV 恢复，display SOC 初始化为真实 SOC |

## 7. 当前仍建议关注的问题

1. `SOC_OCV_VALID_MAX_MV = 4000mV` 与 OCV 表 `4100/4180mV` 高端点不完全匹配。当前高 SOC 主要依赖满电端点同步，是否允许高压 OCV 参与需要实测数据确认。
2. `CapacityFactory` 当前是产品容量真值入口，若产品规格或协议单位变化，必须同步检查 SOC 积分、SOH、上报容量字段。
3. SOC 静态检查只能保证源码契约，最终仍需要台架验证电压、电流、温度、负载压降和 BLE/Modbus 上报一致性。
