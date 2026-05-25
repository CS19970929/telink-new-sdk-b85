# SOC 当前逻辑总览

日期：2026-05-25

## 1. 文档定位

本文按当前源码整理 `ble_sample` 的 SOC 运行逻辑，作为后续调参、复盘和交接的入口文档。

源码真相源：

- `tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c`
- `tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c`
- `tc_ble_single_sdk/vendor/ble_sample/app.c`
- `tc_ble_single_sdk/vendor/ble_sample/tests_flash_quick_check.py`

详细文档：

- `soc_user_experience_tuning_20260524.md`：参数、影响、调参建议。
- `soc_ebike_discharge_experience_20260525.md`：eBike 放电体验专项。
- `soc_strategy_update_call_chain.md`：策略调用链和时间尺度。

## 2. 总体模型

当前 SOC 是组合模型：

```text
真实 SOC = 电流积分为主 + 端点/OCV 小步修正
显示 SOC = display_soc 平滑跟随真实 SOC
SOH = cycle 分段映射
持久化 = hot KV 保存真实 SOC / DSG / cycle
```

关键边界：

- 普通自动修正每次最多移动 `1%`。
- 对外 BLE/Modbus/SIF 上报显示 SOC，不直接暴露真实 SOC 的瞬时修正。
- 放电 `3000mV` 是安全端点，持续约 2s 后 real/display SOC 直接同步到 0。
- AFE 过放保护点是 `2750mV`，`3000mV` 归 0 给保护前显示预留约 `250mV` 余量。
- SOH 仍只按循环次数计算，不按容量误差或低端修正结果学习。

## 3. 启动与持久化

启动路径：

1. `user_init_normal()` 先执行一帧 `App_AFEGet()`，得到电压、电流、压差快照。
2. `soc_kv_store_init()` 初始化 hot KV。
3. `soc_kv_store_get()` 读取 `SOC/DSG/cycle`；空 Flash 使用默认值。
4. `soc_param_lib_init(&d)` 初始化 SOC 状态并计算容量。

KV 写回：

- `main_loop()` 每轮调用 `soc_kv_store_update_and_log_if_changed()`。
- 只有 `soc / dsg / cycle` 任一变化才写 Flash。
- display SOC 不写 KV，复位后 display SOC 初始化为真实 SOC。

## 4. 电流积分与 cycle/SOH

充电：

- `SOC_Cont_AH_Int_CHG()` 根据 `u16Ichg` 和 `SOC_INTEGRAL_PERIOD_MS = 200ms` 积分。
- 只有新 SOC 大于旧 SOC 时才更新。

放电：

- `SOC_Cont_AH_Int_DSG()` 根据 `u16IDischg` 积分。
- 若最低单体仍高于 `SOC_0_VAL = 3000mV`，纯积分不会直接打到 0，而是先钳到 1%。
- SOC 下降会累计 `u8DSG_SOC_Int`，每累计 `100%` 等效放电增加 1 次 cycle。

SOH：

- `bms_soh_from_cycle()` 是唯一 SOH 模型入口。
- `0~80` cycle：SOH = 100%。
- `81~500` cycle：100% 线性下降到约 90%。
- `501~799` cycle：90% 线性下降到约 80%。
- `>=800` cycle：SOH = 80%。
- `soc_recalc_full_capacity()` 用 `CapacityFactory * SOH` 得到 `u32CapFull`。

本次明确不做：

- 不根据 OCV 偏差学习 SOH。
- 不根据单次放电容量学习 SOH。
- 不动态改写 `CapacityFactory`。

## 5. 策略执行顺序

`APP_SOC_IntEnhance_Ctrl()` 每 200ms 执行一次，顺序是：

1. 充放电状态迁移或电流积分。
2. `soc_strategy_update()`。
3. `SOC_Result_Pass()` 输出到 `g_stCellInfoReport.SocElement`。

`soc_strategy_update()` 内部顺序：

1. `soc_update_discharge_sag_hold()`
2. `soc_apply_startup_ocv_correction()`
3. `soc_apply_terminal_sync()`
4. `soc_apply_discharge_terminal_tracking()`
5. `soc_apply_deferred_ocv_step()`
6. `soc_apply_discharge_ocv_tracking()`
7. `soc_apply_idle_ocv_tracking()`

## 6. 满电和空电端点

满电：

- 条件：`VCELLMAX >= 4180mV` 且 `VCELLMIN >= 3980mV`。
- 不要求 `isCHG()`。
- 持续约 `60s` 后，按约 `2s/1%` 上调到 100%。

静置空电：

- 条件：空闲、`VCELLMIN <= 3000mV`、`VCELLMAX <= 3200mV`。
- 持续约 `5s` 后，按约 `1s/1%` 下调到 0%。

放电安全空电：

- 条件：放电中低端表命中 `3000mV -> 0%`。
- 持续约 `2s` 后直接同步 real/display SOC 到 0。
- 这是过放保护前归零的安全例外，不走普通 1% 慢速下降。

## 7. OCV 与静置策略

OCV 估算：

- 使用 weighted cell voltage：`(VCELLMIN * 3 + VCELLMAX) / 4`。
- 使用保守分段表，`3000mV = 0%`，`4180mV = 100%`。
- 当前 `SOC_OCV_VALID_MAX_MV = 4000mV`，高 SOC 主要靠满电端点同步。

静置 OCV：

- 必须空闲且样本有效。
- 单体压差不超过 `100mV`。
- weighted cell voltage 相邻变化不超过 `8mV`。
- 至少稳定约 `30s`，且 confidence 达到 `80`。
- 静置只 latch `deferred_ocv_target`，不快速跳变。
- 已有 target 时约 `60s` 刷新一次。
- 若 `real_soc - target < 10%`，静置不下修。
- 若误差 `>=10%`，才允许约 `30min/1%` 慢速下修。

## 8. 放电修正与骑行体验

容量自适应公式：

```text
CapacityFactory 单位 = Ah * 10
IDSG 单位 = A * 10
自然放电 1% 时间(s) = 36 * CapacityFactory / IDSG
```

放电延迟目标和放电 OCV 下修都使用这个自然时间，再按误差大小调整：

- 小误差 `<6%`：自然时间乘 4。
- 中误差 `>=6%`：自然时间乘 3。
- 大误差 `>=10%`：自然时间乘 2。
- 最快限制 `10s/1%`。
- 最慢限制 `180s/1%`。

这样不同容量版型会自动适配，不写死当前 `C700 / CapacityFactory = 87`。

## 9. sag/rebound holdoff

触发：

- `IDSG > 50`，约大于 5A。

holdoff：

- `50 < IDSG <= 100`：约 60s。
- `IDSG > 100`：约 90s。

释放：

- 松油门或电流下降后，还要看电压回弹是否稳定。
- weighted cell voltage 相邻变化不超过 `12mV`。
- 连续稳定约 `10s` 才释放。

holdoff 阻断：

- 放电 OCV 向下修正。
- `3300/3200/3150mV` 非关键低端端点。

holdoff 不阻断：

- `3050mV -> 1%`。
- `3000mV -> 0%`。

## 10. 低端端点表

| 最低单体电压 | 目标 SOC | 说明 |
| ---: | ---: | --- |
| `3300mV` | 12% | 普通低端开始，受 sag holdoff 阻断 |
| `3200mV` | 6% | 受 sag holdoff 阻断 |
| `3150mV` | 3% | 受 sag holdoff 阻断 |
| `3050mV` | 1% | 关键低端，不受 sag holdoff 阻断 |
| `3000mV` | 0% | 安全端点，不受 sag holdoff 阻断，约 2s 后同步 0 |

## 11. 对其它模块的影响

不改变：

- deep sleep 进入条件和时间。
- 老化 runtime 计时口径。
- 老化模式不允许 RTC mode 的策略。
- deep sleep 不补偿老化时间。
- BLE/Modbus/SIF 协议结构。
- KV key 布局。

可能影响：

- 真实 SOC 修正节奏变化后，hot KV 写入频率会随 SOC 变化而变化。
- `3000mV` 安全端点直接同步 0 会触发一次真实 SOC 写入。

## 12. 验证入口

主机静态契约：

```bash
python3 tc_ble_single_sdk/vendor/ble_sample/tests_flash_quick_check.py
```

建议台架验证：

1. 不同 `CapacityFactory` 版型在同一电流下的 1% 下修节奏。
2. 5A/10A/20A 起步、爬坡、松油门回弹场景。
3. `3050mV -> 1%` 和 `3000mV -> 0%` 低端安全行为。
4. 静置 10min/30min/2h 的 deferred target 和慢速下修行为。
5. deep sleep 唤醒后 hot KV 恢复真实 SOC，display SOC 初始化一致。
