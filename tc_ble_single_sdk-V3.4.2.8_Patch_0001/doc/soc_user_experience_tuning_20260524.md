# SOC 用户体验优化与可调参数说明

更新时间：2026-05-25

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

## 2. 当前 SOC 体验策略

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

### 2.2 real SOC 与 display SOC 分离

当前内部真实 SOC 与对外显示 SOC 分离：

- `SOC_Calculate_Element.u8SOC_Now`：真实 SOC，用于积分、校准、KV 持久化。
- `g_soc_display_soc`：显示 SOC，用于 BLE/Modbus/SIF 对外上报。
- `SOC_Result_Pass()` 上报 `get_soc_display()`，不再直接上报 `get_soc_real()`。

显示跟随速度：

- `SOC_DISPLAY_STEP_TICKS = 5`，约 1s 跟随 1%。

低端安全例外：

- 放电时 `VCELLMIN <= SOC_0_VAL = 3000mV` 持续约 2s 后，real/display SOC 直接同步为 0，保证早于 `2750mV` 过放保护点。

### 2.3 静置 OCV 少校准

当前静置 OCV 策略不在静置阶段快速拉动 SOC，而是先记录 `deferred_ocv_target`：

- 静置且 OCV 样本有效。
- 单体压差不超过 `SOC_OCV_IDLE_CELL_DELTA_MAX_MV = 100mV`。
- weighted cell voltage 相邻采样变化不超过 `SOC_OCV_IDLE_SLOPE_MAX_MV = 8mV`。
- 至少稳定 `SOC_OCV_IDLE_MIN_STABLE_TICKS = 5 * 30`，约 30s。
- `idle_ocv_confidence` 达到 `SOC_OCV_CONFIDENCE_TARGET = 80` 后才允许 latch OCV target。
- 已有 target 时，`SOC_OCV_IDLE_TARGET_REFRESH_TICKS = 5 * 60`，约 60s 才刷新一次目标。

随后在运行状态中消化目标：

- 如果目标高于真实 SOC，只允许在 `isCHG()` 时上调。
- 如果目标低于真实 SOC，只允许在 `isDSG()` 时下调。
- 充电消化速度是 `SOC_CHG_DEFERRED_OCV_STEP_TICKS = 20s/1%`。
- 放电消化速度按 `CapacityFactory` 和 `IDSG` 动态计算。

长静置向下例外：

- 只有 `real_soc - deferred_ocv_target >= SOC_IDLE_STATIC_DOWN_DIFF_THRESHOLD = 10%` 时才允许静置下修。
- 下修速度是 `SOC_LONG_REST_DOWN_STEP_TICKS = 5 * 60 * 30`，约 30min 下调 1%。

### 2.4 放电修正按版型容量自适应

放电期间不再用固定 `2s/1%` 或固定 `20s/1%` 消化 OCV 误差，而是按当前版型容量和放电电流计算自然掉格时间：

```text
CapacityFactory 单位 = Ah * 10
IDSG 单位 = A * 10
自然放电 1% 时间(s) = 36 * CapacityFactory / IDSG
```

源码入口：

- `soc_discharge_natural_1pct_ticks(IDSG)`
- `soc_discharge_gap_correction_step_ticks(current_soc, target_soc)`

误差越大，修正越积极；误差越小，修正越慢：

| 误差 | 速度 |
| ---: | --- |
| `< 6%` | 自然 1% 时间乘 `SOC_DSG_CORR_SMALL_GAP_MUL = 4` |
| `>= 6%` | 自然 1% 时间乘 `SOC_DSG_CORR_MID_GAP_MUL = 3` |
| `>= 10%` | 自然 1% 时间乘 `SOC_DSG_CORR_LARGE_GAP_MUL = 2` |

整体限幅：

- 最快 `SOC_DSG_CORR_STEP_MIN_TICKS = 10s/1%`。
- 最慢 `SOC_DSG_CORR_STEP_MAX_TICKS = 180s/1%`。

这保证不同版型容量不同也能自适应，不写死 C700 或某一个 `CapacityFactory`。

### 2.5 放电低压端点

低压放电端点由 `g_soc_dsg_terminal_rules[]` 控制：

| 最低单体电压上限 | 目标 SOC 上限 | 说明 |
| --- | ---: | --- |
| `SOC_DSG_TERMINAL_START_MV = 3300mV` | 12% | 低端开始介入，受 sag holdoff 阻断 |
| `SOC_DSG_TERMINAL_L1_MV = 3200mV` | 6% | 受 sag holdoff 阻断 |
| `SOC_DSG_TERMINAL_L2_MV = 3150mV` | 3% | 受 sag holdoff 阻断 |
| `SOC_DSG_TERMINAL_L3_MV = 3050mV` | 1% | 关键低端，不再被 sag holdoff 阻断 |
| `SOC_0_VAL = 3000mV` | 0% | 安全端点，不被 sag holdoff 阻断，约 2s 后同步 0 |

非 0% 端点的修正速度先按容量/电流动态计算，再按低端电压区间限幅：

- 目标 1%：最快不超过 `10s/1%`。
- 目标 3%：最快不超过 `20s/1%`。
- 目标 6%：最快不超过 `40s/1%`。
- 目标 12%：最快不超过 `90s/1%`。

`3000mV -> 0%` 是安全端点例外：因为 AFE 过放保护点是 `2750mV`，如果仍按 1% 慢慢掉，可能出现保护前显示还没归 0 的风险。

### 2.6 放电 sag/rebound holdoff

当前放电电流大于 `SOC_DSG_SAG_HOLD_CURR_MIN = SOC_DSG_OCV_MID_CURR_MAX = 50`，约高于 5A 时触发压降 holdoff：

| 电流 | holdoff |
| --- | ---: |
| `50 < IDSG <= 100` | `SOC_DSG_SAG_HOLDOFF_HIGH_TICKS = 5 * 60`，约 60s |
| `IDSG > 100` | `SOC_DSG_SAG_HOLDOFF_VHIGH_TICKS = 5 * 90`，约 90s |

释放前还要求回弹稳定：

- weighted cell voltage 相邻变化不超过 `SOC_DSG_REBOUND_SLOPE_MAX_MV = 12mV`。
- 连续稳定 `SOC_DSG_REBOUND_STABLE_TICKS = 5 * 10`，约 10s。

这会阻断放电 OCV 向下修正，以及 3300/3200/3150mV 非关键低端规则，减少起步、加速、爬坡导致的误掉格。

## 3. 可调参数与影响

### 3.1 显示平滑参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_DISPLAY_STEP_TICKS` | `5`，约 1s/1% | 显示更平滑，用户看到的变化更慢 | 显示更跟手 | 太大时真实 SOC 已变化，外部显示滞后 |

### 3.2 满电端同步参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_100_VAL` | `4180mV` | 更难触发 100% | 更容易满电同步 | 过低会提前满电，过高会长期不到 100% |
| `SOC_FULL_SYNC_MIN_MV` | `3980mV` | 均衡不足时更难满电 | 更容易触发满电 | 过低会掩盖单体差异 |
| `SOC_FULL_LOCK_TICKS` | `60s` | 抗瞬态更强 | 到 100% 更快 | 过短可能误触发 |
| `SOC_FULL_SYNC_STEP_TICKS` | `2s/1%` | 上调更慢 | 上调更快 | 过快像跳变，过慢满充后显示滞后 |

### 3.3 静置 OCV 参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_OCV_VALID_MIN_MV` | `2000mV` | 有效窗口变窄 | 有效窗口变宽 | 过宽会采纳异常样本 |
| `SOC_OCV_VALID_MAX_MV` | `4000mV` | 过滤高压端 OCV | 允许更高电压参与 OCV | 当前高 SOC 主要依赖满电端点 |
| `SOC_OCV_IDLE_CELL_DELTA_MAX_MV` | `100mV` | 单体差大时也允许 OCV | 更严格 | 过大可能误判不均衡包 |
| `SOC_OCV_IDLE_MIN_STABLE_TICKS` | `30s` | 静置判定更稳 | 更快形成 OCV 目标 | 过短会被回弹干扰 |
| `SOC_OCV_IDLE_SLOPE_MAX_MV` | `8mV` | 更容易认为稳定 | 更严格 | 过大容易在回弹中 latch |
| `SOC_OCV_CONFIDENCE_TARGET` | `80` | 更晚建立 target | 更快建立 target | 过低会更频繁校准 |
| `SOC_OCV_IDLE_TARGET_REFRESH_TICKS` | `60s` | 目标刷新更慢 | 目标刷新更快 | 过快追随短期波动 |
| `SOC_IDLE_STATIC_DOWN_DIFF_THRESHOLD` | `10%` | 静置更少下修 | 大误差更早下修 | 过低停车后掉格明显 |
| `SOC_LONG_REST_DOWN_STEP_TICKS` | `30min/1%` | 长静置下调更慢 | 长静置下调更快 | 过短会让停车后 SOC 下降明显 |

### 3.4 放电容量自适应参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `CapacityFactory` | 来自 `conf.h` | 不应用于体验调节 | 不应用于体验调节 | 配错会让积分和修正节奏都错 |
| `SOC_DSG_CORR_STEP_MIN_TICKS` | `10s/1%` | 最快修正更慢 | 最快修正更快 | 过小会骑行掉格明显 |
| `SOC_DSG_CORR_STEP_MAX_TICKS` | `180s/1%` | 最慢修正更慢 | 最慢修正更快 | 过大长期偏差收敛慢 |
| `SOC_DSG_CORR_SMALL_GAP_MUL` | `4` | 小误差更慢 | 小误差更快 | 过小会频繁微掉格 |
| `SOC_DSG_CORR_MID_GAP_MUL` | `3` | 中误差更慢 | 中误差更快 | 过大误差收敛慢 |
| `SOC_DSG_CORR_LARGE_GAP_MUL` | `2` | 大误差更慢 | 大误差更快 | 过小会低端掉格明显 |
| `SOC_DSG_CORR_MID_GAP_PERCENT` | `6%` | 更晚进入中误差 | 更早进入中误差 | 过低会修正偏积极 |
| `SOC_DSG_CORR_LARGE_GAP_PERCENT` | `10%` | 更晚进入大误差 | 更早进入大误差 | 过高会大误差收敛慢 |

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
| `SOC_DSG_OCV_*_STABLE_TICKS` | `8/12/20/30` | 需要更长稳定时间 | 更快进入修正 | 过短受瞬态压降影响 |

### 3.6 放电低压端点参数

| 参数/表项 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_DSG_TERMINAL_START_MV` | `3300mV`，目标 12% | 更早进入低端 SOC 上限 | 更晚进入低端上限 | 过高会高 SOC 时提前被压低 |
| `SOC_DSG_TERMINAL_L1_MV` | `3200mV`，目标 6% | 更早压到 6% | 更晚压到 6% | 影响低端剩余里程感知 |
| `SOC_DSG_TERMINAL_L2_MV` | `3150mV`，目标 3% | 更早压到 3% | 更晚压到 3% | 低端显示与保护策略要一致 |
| `SOC_DSG_TERMINAL_L3_MV` | `3050mV`，目标 1% | 更早压到 1% | 更晚压到 1% | 过低可能用户看到有电但很快断电 |
| `SOC_0_VAL` | `3000mV`，目标 0% | 更晚归 0 | 更早归 0 | 必须早于 `2750mV` 过放保护点 |
| `SOC_DSG_EMPTY_LOCK_TICKS` | `10`，约 2s | 归 0 更稳 | 归 0 更快 | 过短可能误归 0 |

### 3.7 放电压降 holdoff 参数

| 参数 | 当前值 | 调大影响 | 调小影响 | 主要风险 |
| --- | --- | --- | --- | --- |
| `SOC_DSG_SAG_HOLD_CURR_MIN` | `50`，源码判断 `IDSG > 50` | 更难触发压降保护 | 更容易触发压降保护 | 过低会长期阻断真实修正 |
| `SOC_DSG_SAG_HOLDOFF_HIGH_TICKS` | `60s` | 5~10A 更保守 | 更快恢复校准 | 过长低端可能偏高 |
| `SOC_DSG_SAG_HOLDOFF_VHIGH_TICKS` | `90s` | 10A 以上更保守 | 更快恢复校准 | 过长低端可能偏高 |
| `SOC_DSG_REBOUND_STABLE_TICKS` | `10s` | 需要更久回弹稳定 | 更快释放 | 过短抗回弹不足 |
| `SOC_DSG_REBOUND_SLOPE_MAX_MV` | `12mV` | 更容易认为稳定 | 更严格 | 过大容易提前释放 |

### 3.8 容量、循环与持久化参数

| 参数 | 当前值/来源 | 影响 | 调整风险 |
| --- | --- | --- | --- |
| `SOC_INTEGRAL_PERIOD_MS` | `200ms` | 电流积分时间基准 | 必须和调度周期一致 |
| `CapacityFactory` | `conf.h` | 工厂容量，影响满容量、当前容量和放电修正节奏 | 改错会导致 SOC 积分斜率错误 |
| `SOC_CAPACITY_UNITS_PER_AH` | `3600 * 10` | 内部容量单位 | 不建议改 |
| `SOC_REPORT_CAPACITY_DIVISOR` | `360` | 对外容量单位换算 | 改动会影响 BLE/Modbus/SIF 容量字段 |
| `SOC_EQUIV_CYCLE_PERCENT` | `100` | 100% 等效放电记 1 cycle | 不建议改 |
| `SOC_PARAM_DEFAULT_SOC` | `FAC_INIT_soc` | 空 Flash 默认 SOC | 影响新板/重置后的初始显示 |
| `SOC_PARAM_DEFAULT_DSG` | `0` | 默认放电累计 | 不建议改 |
| `SOC_PARAM_DEFAULT_CYCLE` | `0` | 默认循环次数 | 不建议改 |
| `bms_soh_from_cycle()` | cycle 分段退化 | SOH 来源 | 本次明确不改，不按容量学习 SOH |

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
- SOH 计算来源。

### 4.2 对 KV/Flash 的影响

会影响：

- 真实 SOC 的修正频率可能改变 hot KV 写入频率。
- display SOC 不落 KV。
- `3000mV` 安全端点直接同步 0 会触发一次真实 SOC 写入。

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

1. 先确认真实输入：`CapacityFactory`、AFE 电流缩放、deadband、单体电压采样。
2. 再调骑行掉格体验：`SOC_DSG_SAG_HOLDOFF_*`、`SOC_DSG_REBOUND_*`、`SOC_DSG_CORR_*_MUL`。
3. 再调低电可信度：`SOC_DSG_TERMINAL_*_MV`、`SOC_DSG_CORR_*_GAP_PERCENT`。
4. 再调静置体验：`SOC_OCV_IDLE_*`、`SOC_IDLE_STATIC_DOWN_DIFF_THRESHOLD`、`SOC_LONG_REST_DOWN_STEP_TICKS`。
5. 最后才调 OCV 表和 `SOC_OCV_VALID_MAX_MV`，因为这需要实测电芯曲线支持。

不要用下面方式调体验：

- 不要把 `CapacityFactory` 当显示平滑参数。
- 不要把 SOH 改成容量学习模型。
- 不要把 `SOC_0_VAL` 降到接近 `2750mV`，否则过放保护前显示 0% 的余量不足。

## 6. 验证建议

| 场景 | 验证点 | 预期 |
| --- | --- | --- |
| 满电电压到位但电流状态异常 | 不要求 `isCHG()` | 满足 `4180/3980mV` 且持续约 60s 后，按约 2s/1% 上调到 100% |
| 大电流瞬时放电 | sag hold | 大于约 5A 触发 60s/90s holdoff，非关键低压端点和放电 OCV 不应立刻拉低 |
| 轻载放电到低端 | 端点表 | 3300/3200/3150/3050/3000mV 分段把 SOC 上限压到 12/6/3/1/0% |
| 低电末端 | 0% 安全锚点 | `VCELLMIN <= 3000mV` 持续约 2s 后 real/display SOC 同步 0，早于 2750mV 过放保护 |
| 静置 2 分钟后 OCV 与 SOC 差异大 | deferred target | 静置时只记录目标；误差小于 10% 不静置下修；运行后按方向逐步消化 |
| 长静置 OCV 低于真实 SOC 且误差大 | long rest down | 目标稳定且误差至少 10% 后，约 30min 才下调 1% |
| deep sleep 唤醒 | KV 恢复 | MCU 复位后真实 SOC 从 hot KV 恢复，display SOC 初始化为真实 SOC |

## 7. 当前仍建议关注的问题

1. `SOC_OCV_VALID_MAX_MV = 4000mV` 与 OCV 表 `4100/4180mV` 高端点不完全匹配。当前高 SOC 主要依赖满电端点同步，是否允许高压 OCV 参与需要实测数据确认。
2. `CapacityFactory` 是产品容量真值入口，若产品规格或协议单位变化，必须同步检查 SOC 积分、SOH、上报容量字段和放电修正节奏。
3. SOC 静态检查只能保证源码契约，最终仍需要台架验证电压、电流、温度、负载压降和 BLE/Modbus 上报一致性。
