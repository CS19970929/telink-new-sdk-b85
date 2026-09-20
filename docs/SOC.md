# SOC 模块（当前实现）

本轮 D014 同步统一 core、存储/诊断版本和验证边界见 [SOC_UNIFIED_CORE_2026-09-21.md](SOC_UNIFIED_CORE_2026-09-21.md)。

本文件记录 `SocEnhance.c/.h`、`bms_soc_profile.h`、`bms_soc_defs.h` 与 SOC/Cold KV 的真实行为。旧文件名保留用于工程兼容，但算法、产品配置、OCV 数据和持久化已经分层。

## 1. 核心模型

- `SOC estimate`：库仑积分主线，固定 200 ms 积分周期。
- 电流报告单位为 0.1 A；默认 **< 200 mA 不积分**，视为静置候选。
- `SOC display`：与 estimate 分离，每 1 s 最多变化 1%，避免对外跳变。
- SOC hot KV 保存 estimate / 等效放电百分比 / cycle / learned capacity；显示 SOC 不保存。
- OCV 只用于长期纠偏，不作为运行中的主 SOC。
- 普通 OCV、开机、静置、电压回弹 **永远不得向上校准**；只有“确认正在充电 + 满电条件成立”可以主动向上校准到 100%。

## 2. 三元 / 铁锂产品参数

稳定 ID 定义位于 `bms_soc_defs.h`：

```text
chemistry:
0 = AUTO
1 = LFP
2 = NMC

profile_id:
0 = AUTO
1 = GENERIC_LFP
2 = GENERIC_NMC
```

产品选择现在是正式 Cold KV 参数，而不是只能根据 OVP 猜：

- `0x2009`：`battery_chemistry`
- `0x200A`：`soc_profile_id`

历史 `0x2001..0x2008` key 保持不变；这是**只追加 key**的兼容升级。旧设备 Flash 中没有 `0x2009/0x200A` 时，`flash_kv32` 使用新 key 的默认值 `AUTO/AUTO`，然后才回退到历史 OVP 推断。因此不依赖旧 `PARAM_VER`，也不会覆盖其它已出货参数。

新产品推荐显式持久化：

```c
bms_soc_set_product_config(BMS_SOC_CHEMISTRY_LFP,
                           BMS_SOC_PROFILE_GENERIC_LFP);
```

或 NMC。API 先校验 chemistry/profile 一致性，再原子保存 system KV，最后切换运行 profile。`LFP + NMC profile`、`NMC + LFP profile` 这类组合直接拒绝。

`AUTO` 只用于兼容/通用固件：两项均 AUTO 时继续根据已加载的三级单体 OVP 回退判断（`<=3900 mV` LFP，`>3900 mV` NMC）。量产产品不建议长期依赖该启发式。

## 3. OCV Profile 数据层

OCV 曲线和端点数据已经从 `SocEnhance.c` 算法中移到 `bms_soc_profile.h`。算法只消费 `soc_profile_t`：

- profile ID / version；
- chemistry；
- OCV 点表；
- 有效电压范围；
- full/empty anchor 参数；
- 放电末端 knee 参数。

当前内置：

- `GENERIC_LFP`, version 1；
- `GENERIC_NMC`, version 1。

它们是**通用中心表**，不是某一型号电芯的实验标定曲线。以后拿到 32700 LFP、21700 NMC 等实际静置数据时，应新增/替换 profile 数据并提升 profile version，不改库仑积分、置信区间、Flash、显示 SOC 或保护策略。

## 4. OCV 置信区间

OCV 使用保守加权单体电压：

```text
V_ocv = (3 * Vcell_min + Vcell_max) / 4
```

静置校准条件：

- 充/放电有效电流均低于 200 mA；
- 单体压差 <= 100 mV；
- 相邻 200 ms 样本变化 <= 8 mV；
- 连续稳定 **>= 10 min**。

达到条件后，由当前 profile 得到 `center SOC`，形成默认 `center ± 5 percentage points` 的 `[low, high]`：

- estimate 在区间内：不修正；
- estimate 低于 `low`：不动，绝不向上；
- estimate 高于 `high`：每 30 min 最多下降 1%，长期回归到 `high` 边界。

开机恢复 Flash SOC 后重新进入静置资格判定，不会用一次开机电压覆盖 SOC。

## 5. 满 / 空锚点与低端体验

满电：

- **仅在确认充电方向时**，三级单体 OVP 已触发才允许强锚定 100%；或
- **仅在确认充电方向时**，满足当前 profile 的满电电压条件稳定 60 s，之后约 2 s/1% 向 100% 收敛。
- 静置、高电压回弹、开机高电压、非充电状态即使达到满电区也不得向上校准。

空电：三级单体 UVP 可强锚定 0%；放电低端分段向 0 收敛。LFP/NMC 使用不同 knee，高电流压降有 sag hold，避免电摩起步时按瞬时端电压误杀 SOC。

## 6. SOC Low 告警

`g_tParam.protect.u16SocUp_First/Second/Third` 沿用历史字段名，实际按低 SOC 阈值处理，并写 First/Second/Third `b1SocLow`。当前只告警，不默认关闭 DSG；真正的安全欠压截止仍由 MCU Cell UV + AFE UV 完成。

## 7. 容量学习

框架已实现但默认关闭：支持 0->100 和 100->0 完整路径，中途反向电流或 MCU 重启作废；成功值限定在 nominal 50%~130%。开启 `hide_capacity_until_learned` 后首次有效学习前容量字段隐藏，SOC 百分比仍可用。

## 8. 诊断接口

`bms_soc_get_diag()` 当前返回：

- 实际 chemistry；
- 实际 profile ID + profile version；
- estimate / display SOC；
- OCV state / center / low / high / confidence；
- OCV cell voltage、静置秒数；
- 容量学习状态、learned capacity。

因此现场可以明确区分“LFP/NMC 选错”“曲线版本不同”“OCV 未达到静置条件”和“库仑累计偏差”。协议寄存器地址应由产品协议统一分配，不在 SOC 模块里硬编码。

## 9. 必测场景

1. 老固件 Cold KV 升级：原 `0x2001..0x2008` 不变，新 key 缺失时 AUTO/AUTO 正常 fallback。
2. 显式 LFP/NMC 保存、掉电重启后仍使用相同 chemistry/profile/version。
3. chemistry/profile 冲突配置必须拒绝且不能污染 Flash。
4. 0.1 A 不积分、0.2 A 开始积分边界。
5. 静置 9 min 59 s 不校准；10 min 后普通 OCV 只能向下。
6. 非充电的开机高电压/回弹绝不能使 SOC 上升；确认充满可到 100%。
7. 充电满锚点、放电 UVP、LFP 3.30 V 平台、大电流 sag hold。
8. SOC Low 三级告警、Flash 恢复、容量学习成功/中断/复位作废。
