# SOC 模块（当前实现）

本文件记录 `SocEnhance.c/.h` 与 `soc_kv_store.c/.h` 的真实行为。SOC 算法仍保留旧文件名以维持工程兼容，但内部已经按可复用 BMS SOC 模块整理。

## 1. 核心模型

- `SOC estimate`：库仑积分主线，固定 200 ms 积分周期。
- 电流报告单位为 0.1 A；默认 **< 200 mA 不积分**，视为静置候选。
- `SOC display`：与 estimate 分离，每 1 s 最多变化 1%，避免对外跳变。
- Flash 只保存 estimate SOC / 等效放电百分比 / cycle；显示 SOC 不保存。
- OCV 只用于长期纠偏，不作为运行中的主 SOC。

## 2. 三元 / 铁锂兼容

模块内置两套可替换的通用中心 OCV 表：

- `BMS_SOC_CHEMISTRY_LFP`：磷酸铁锂；
- `BMS_SOC_CHEMISTRY_NMC`：三元；
- `BMS_SOC_CHEMISTRY_AUTO`：默认模式。依据已加载的三级单体过压参数自动选择：`<= 3900 mV` 视为 LFP，`> 3900 mV` 视为 NMC。

也可以通过 `bms_soc_configure()` 显式固定 chemistry。当前 D011 默认保护参数为 3750 mV，因此 AUTO 会选择 LFP；如果产品改为三元并把三级 OVP 配置到 4.2 V 区域，SOC 会自动切换 NMC profile。

OCV 表是“通用中心表”，不是某一型号电芯的实验标定曲线。量产项目如果有电芯厂家/实测静置曲线，应只替换 profile 表，不改算法。

## 3. OCV 置信区间

OCV 使用保守加权单体电压：

```text
V_ocv = (3 * Vcell_min + Vcell_max) / 4
```

静置校准条件：

- 充/放电有效电流均低于 200 mA；
- 单体压差 <= 100 mV；
- 相邻 200 ms 样本变化 <= 8 mV；
- 连续稳定 **>= 10 min**。

达到条件后，由化学体系中心表得到 `center SOC`，再形成默认 `center ± 5 percentage points` 的置信区间 `[low, high]`。

关键规则：

- estimate 在区间内：不修正；
- estimate 低于 `low`：**绝不通过普通 OCV 向上校准**；
- estimate 高于 `high`：每 30 min 最多下降 1%，长期缓慢回归到 `high` 边界；
- 唯一允许主动向上拉 SOC 的路径是确认满充锚点。

因此开机后会自动进入 OCV 准备判定，但不会因为一次开机电压读数直接跳 SOC。

## 4. 满 / 空锚点与低端体验

满电：

- 三级单体 OVP 已触发时，estimate 强锚定 100%；或
- 根据 chemistry profile 的满电电压条件稳定 60 s，之后约 2 s/1% 向 100% 收敛。

空电：

- 三级单体 UVP 已触发时，SOC 强锚定 0%；
- 放电低端提前分段收敛，避免到 UVP 时从较高 SOC 突然掉 0；
- LFP 与 NMC 使用不同低端 knee：LFP 不会再把 3.30 V 当成 12% 低端区。

高放电电流导致的压降会触发 sag hold；普通低端电压修正被抑制，1%/0% 安全端点仍保留。

## 5. SOC Low 告警

`g_tParam.protect.u16SocUp_First/Second/Third` 沿用历史字段名，但实际按低 SOC 阈值处理，并写入 First/Second/Third 的 `b1SocLow`。恢复值若低于对应 trip，会自动提升为 `trip + 1%`，避免原参数组合造成抖动。

SOC Low 目前只形成告警/故障位，不直接关闭 DSG MOS；是否把三级 SOC Low 变成保护动作应由产品策略单独决定。

## 6. 容量学习

框架已经实现，但 **默认关闭**：

- 0% 锚点 -> 完整充到 100%；
- 100% 锚点 -> 完整放到 0%；
- 中途出现反向电流或 MCU 重启，本次学习作废；
- 成功值必须在标称容量 50%~130% 的合理范围内；
- 学习成功后容量与 learned flag 写入 hot KV。

当 `capacity_learning_enable=1` 且 `hide_capacity_until_learned=1` 时，首次学习成功之前容量字段报告 0；SOC 百分比仍正常显示。默认学习关闭时继续报告标称/估算容量，不改变当前产品行为。

## 7. 诊断接口

`bms_soc_get_diag()` 可读取：chemistry、estimate/display SOC、OCV 状态、center/low/high、OCV 电压、静置秒数、confidence、容量学习状态和 learned capacity，便于以后直接映射到 Modbus/BLE 诊断寄存器。

## 8. 必测场景

1. LFP/NMC 两种 profile 的 0/100% 锚点和 OCV 表切换。
2. 0.1 A 不积分、0.2 A 开始积分的边界。
3. 静置 9 min 59 s 不校准，10 min 后只允许向下；长期只回归到 high 边界。
4. 充电到满、放电到 UVP，显示 SOC 不产生普通大跳变。
5. LFP 3.30 V 中平台不得误判为低端 12%。
6. 5 A / 10 A / 20 A 放电压降与松油门回弹。
7. SOC Low 20/10/5% 三级告警及恢复滞回。
8. Flash 掉电恢复；容量学习成功/中断/复位作废。
