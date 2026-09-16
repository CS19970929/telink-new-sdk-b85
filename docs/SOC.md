# SOC 模块（当前实现）

本文件记录 `SocEnhance.c/.h`、`bms_soc_profile.h`、`bms_soc_defs.h` 与 Storage V1 的当前行为。2026-09-17 第 10 节要求已接入源码，主机数值/状态轨迹测试见 `tests/d008_power_soc_host_check.py`；实板验收待完成。旧兼容 API 名中含 KV 不代表仍使用旧 KV 引擎。

## 1. 核心模型

- `SOC estimate`：库仑积分主线，目标 200 ms 采样，按新样本的 32k 时标实际间隔积分。
- 协议电流报告仍为 0.1 A，算法使用带符号 mA；默认 **< 200 mA 不积分**，视为静置候选。
- `SOC display`：与 estimate 分离，每 1 s 最多变化 1%，避免对外跳变。
- State 域保存整数 SOC estimate / 等效放电百分比 / cycle / learned capacity、flag 和 runtime；显示 SOC、积分小数余量及 OCV 静置计时不作为独立持久化字段。来源为 `bms_state_store.c`，不新增存储布局。
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

产品选择是当前 Config 域的 system/SOC identity 参数；以下是既有参数接口地址，不是新的 Flash key：

- `0x2009`：`battery_chemistry`
- `0x200A`：`soc_profile_id`

当前 Storage V1 使用显式版本/编码与 Config/State 语义域，**不迁移旧 `flash_kv32`、SOC/Cold KV**；没有可读 V1 记录时加载默认值。这是当前开发期存储策略，不能把它描述成对所有旧设备“仅追加 key、无损升级”。详见 [STORAGE.md](STORAGE.md)。

新产品推荐显式持久化：

```c
bms_soc_set_product_config(BMS_SOC_CHEMISTRY_LFP,
                           BMS_SOC_PROFILE_GENERIC_LFP);
```

或 NMC。API 先校验 chemistry/profile 一致性，再保存 Config 域 system 参数，最后切换运行 profile。`LFP + NMC profile`、`NMC + LFP profile` 这类组合直接拒绝。

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
- 有界相邻样本变化 <= 8 mV（目标 200 ms，允许间隔最多 400 ms）；
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

1. Storage V1 缺失/损坏/掉电恢复：加载规则与 [STORAGE.md](STORAGE.md) 一致，不误读旧 KV；确认 chemistry/profile 的默认与显式配置路径。
2. 显式 LFP/NMC 保存、掉电重启后仍使用相同 chemistry/profile/version。
3. chemistry/profile 冲突配置必须拒绝且不能污染 Flash。
4. 0.1 A 不积分、0.2 A 开始积分边界。
5. 静置 9 min 59 s 不校准；10 min 后普通 OCV 只能向下。
6. 非充电的开机高电压/回弹绝不能使 SOC 上升；确认充满可到 100%。
7. 充电满锚点、放电 UVP、LFP 3.30 V 平台、大电流 sag hold。
8. SOC Low 三级告警、Flash 恢复、容量学习成功/中断/复位作废。

## 10. D008 suspend 与 MCU 断电下的 SOC 契约

### 10.1 与电源状态分离

电源时序和 IO 的唯一依据是 [D008_PRODUCT_REFERENCE.md](D008_PRODUCT_REFERENCE.md) 第 12 节：suspend 期间 MCU 仍供电；深度休眠则先 AFE shutdown，再将 PC4/MCU_LDO_PIN 拉低，MCU 完全断电。电路恢复 MCU 供电后再通过 I2C 唤醒 AFE。

**suspend 不自动等于电池静置，MCU 断电也不等于获得了一段有效静置记录。** 双向 ≥500 mA 是退出 suspend 的产品门槛，不能替代 SOC 默认 <200 mA 的静置/积分死区。两者由不同用途决定，不合并成一个参数。

| 输入状态 | 电源要求 | SOC 实现要求 |
|---|---|---|
| 新鲜有效样本，两方向均低于当前 SOC 死区（默认 200 mA） | 允许按独立条件保持 suspend | 同时满足电压、压差、稳定性才累计 OCV 静置资格 |
| 有效电流 200..499 mA | 单靠电流还未达到退出门槛 | 不满足默认静置条件；按合格测量和实际时间积分，清除静置资格 |
| 任一方向 ≥500 mA（含 500） | 退出 suspend，恢复正常采样/业务节奏 | 清除静置资格，按实际方向积分；不得丢失状态切换前后的有效时间 |
| 无效/陈旧样本、AFE reset/通信故障 | 进入受控采样或故障路径 | 冻结无法证明有效的积分/端点/OCV校准，清除静置资格；失败清零不等于零电流 |
| AFE shutdown / MCU 断电 | 不再执行测量和算法 | 事先保存必要 State；复电读取已提交状态，不补算未知时长、不继承静置资格 |

### 10.2 保留既有校准约束

- 使用匹配 24S LFP / 20S NMC 的 profile，输入必须来自有效物理通道，不能用未装通道参与 min/max。
- 保留普通 OCV 只能向下、不能因开机高压或电压回弹向上校准的约束。向上满电锚定仍必须有有效充电方向和满电条件。
- 保留当前默认静置 600 s、压差 ≤100 mV、默认中心带 ±5%、普通 OCV 每 30 min 最多下降 1% 的语义；本轮不更改电芯参数或校准方向。
- 调度目标固定 200 ms；仅接受 ≤400 ms 的有界间隔，超过即重置资格。8 mV 不按漏采时长放宽，实际间隔折算为原 200 ms 策略时间片；不能套用到任意长休眠。400 ms 是软件保守边界（两个名义周期），时钟误差/最坏调度延迟仍需实测。
- 满/空锚点也必须检查有效性和新鲜度，不能只修正普通 OCV 路径而保留旧低压样本触发空电锚点的问题。

### 10.3 时间、样本与持久化

1. 使用与既有 Runtime/PM 相同的 SDK `pm_get_32k_tick()` 时间源，按工程既有 32000 ticks/s 换算，无符号相减处理回绕。积分按 mA × elapsed ticks / (32000 × 100) 得到 As×10，有界 32 位分段计算保留余数；校准按实际时间累计 200 ms 策略片，而非按主循环次数。实际时钟精度仍需板测。
2. 静置时间只能累计有连续、有效测量支持的区间。缺少测量的长间隔不得假定零电流，也不能用恢复后的单个样本乘全部休眠时长。当前样本年龄/间隔上限均为 400 ms，重复时标不推进状态，恢复第一帧仅重新建立时间基准；产品实测必须验证此边界。
3. 积分使用明确单位（mA、ms、容量单位），检查乘法中间值溢出、符号及余量；零点校准与电流噪声应在实测后确认。应用不得读取 DVC 私有寄存器来绕过公共 guard。
4. 当前 State 只保存已有字段，不含断电期间可靠时钟或完整积分/OCV上下文。本轮不扩展 Flash schema；如果后续确需持久化更多状态，另审兼容性、寿命和失败恢复。
5. 在 AFE shutdown / PC4 断电前完成必要持久化并检查结果；禁止每次短暂 suspend 都擦写 Flash，禁止 ISR 内保存。改变保存节奏时必须同时评估寿命与关机数据损失窗口。
6. 冷启动、AFE 重初始化、测量失败及电流离开静置区均重新确认资格。已保存 SOC 不应被单次启动电压覆盖；旧化学体系或旧样本的静置资格不能沿用。

### 10.4 验证和实现状态

已在算法总入口阻断无效样本的积分、端点与 OCV 校准，并清除计时资格。主机测试对 F05 的旧电压+无效零电流场景验证 SOC 保持不变；历史审核文件保留原 SHA 的结论，实板仍未验收。

验证范围与后续板测至少覆盖：正负 199/200/499/500/501 mA、无效/陈旧帧、9 min 59 s/10 min 边界、OCV 每 30 min 的降幅、计时回绕、不同 suspend 周期、Flash 失败、断电重启、24S/20S 化学体系。源码契约测试不能替代数值轨迹回放及实板电流/时序验证。
