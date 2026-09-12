# SOC 当前行为

本文只记录当前源码行为；调参和实测结论应更新本文，避免继续新增按日期命名的分析文档。

源码真值：`SocEnhance.c/.h`、`soc_kv_store.c/.h`、`app.c`。主机契约位于 `tests/flash_quick_check.py`。

## 1. 模型和单位

```text
real SOC    = 200 ms 电流积分为主 + 端点/OCV 小步修正
display SOC = 平滑跟随 real SOC，对外由 BLE/Modbus/SIF 上报
SOH         = cycle 分段映射
persistence = real SOC / DSG / cycle 完整快照
```

- `CapacityFactory` 单位为 `0.1 Ah`，不能用于补偿电流测量误差。
- 充/放电电流报告单位为 `0.1 A`。
- 普通自动修正每次最多移动 1%。
- display SOC 不写 Flash，复位后从 real SOC 初始化。
- SOC、DSG、cycle 任一发生变化才写 hot KV，不是固定每 5 秒强制写。

## 2. 启动和调度

正常启动先通过 `bms_afe_sample()` 获取一帧有效测量，再初始化 SOC KV 和运行状态。`APP_SOC_IntEnhance_Ctrl()` 每 200 ms 执行：

1. 更新充放电状态并按实际周期积分。
2. 执行 `soc_strategy_update()`。
3. 通过 `SOC_Result_Pass()` 更新对外报告。

策略顺序为 sag hold -> 启动 OCV -> 满/空端点 -> 放电低端 -> deferred OCV -> 放电 OCV -> 静置 OCV。调整顺序会改变安全端点和用户体验，必须单独验证。

## 3. 电流积分、cycle 和 SOH

- 充电只在积分结果上升时更新 SOC。
- 放电最低单体高于 3000 mV 时，纯积分先钳在 1%，不直接归零。
- 累计 100% 等效放电增加一个 cycle。
- SOH：0..80 cycle 为 100%；81..500 线性到约 90%；501..799 线性到约 80%；800+ 为 80%。
- 满容量由 `CapacityFactory * SOH` 计算；不根据 OCV、单次放电结果或传感器误差学习/改写标称容量。

## 4. 端点

满电：`VCELLMAX >= 4180 mV` 且 `VCELLMIN >= 3980 mV`，保持约 60 s 后以约 2 s/1% 向 100% 收敛，不要求充电状态。

静置空电：空闲、`VCELLMIN <= 3000 mV` 且 `VCELLMAX <= 3200 mV`，保持约 5 s 后以约 1 s/1% 向 0% 收敛。

放电安全端点：放电时最低单体持续约 2 s 达到 3000 mV，real/display SOC 直接同步到 0。这一例外在 2750 mV AFE 过放保护前预留显示余量。

| 最低单体 | 目标 SOC | sag hold |
|---:|---:|---|
| 3300 mV | 12% | 阻断普通下修 |
| 3200 mV | 6% | 阻断普通下修 |
| 3150 mV | 3% | 阻断普通下修 |
| 3050 mV | 1% | 不阻断 |
| 3000 mV | 0% | 不阻断，安全同步 |

## 5. OCV 与压降抑制

OCV 使用 `(VCELLMIN * 3 + VCELLMAX) / 4` 的保守加权电压。静置校准要求空闲、压差不超过 100 mV、相邻变化不超过 8 mV，并稳定约 30 s；它只记录 deferred target。误差不足 10% 不下修，达到 10% 后仍以约 30 min/1% 慢速下修，静置不向上校准。

当放电电流大于约 5 A 时启用 sag hold：约 5..10 A 保持 60 s，大于 10 A 保持 90 s；松油门后电压还需连续稳定约 10 s。它阻断 3300/3200/3150 mV 普通 OCV 下修，但不阻断 3050/3000 mV 安全端点。

放电修正按容量和电流缩放：

```text
自然下降 1% 时间(s) = 36 * CapacityFactory / IDSG
```

根据误差使用 2x..4x 时间系数，并限制在 10..180 s/1%。

## 6. 必测场景

1. 不同容量版型在相同倍率下的积分和 1% 修正节奏。
2. 5 A / 10 A / 20 A 起步、爬坡、松油门和回弹。
3. 3050 mV -> 1% 与 3000 mV -> 0% 的去抖和保护余量。
4. 静置 10 min / 30 min / 2 h，确认不向上跳变。
5. 复位、掉电和 deep sleep 唤醒后的 real/display/KV 一致性。
6. 电流增益误差必须在采样校准层修正，禁止通过修改 `CapacityFactory` 掩盖。
