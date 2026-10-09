# SOC：当前样本接口与持久化

入口是 `bms/app/app.c::app_update_soc_from_sample()` → `bms/core/bms_soc.c::bms_soc_process_sample()`。数据定义在 `bms_soc.h`，OCV 曲线在 `bms_soc_profile.h`，ETA 独立在 `bms_soc_eta.c/.h`。本页替代旧 Cold/Hot KV 和旧分支 schema 说明。

## 1. 样本与时间

`bms_soc_sample_t` 携带 mV、电流 mA、编码温度、32K 时间戳、有效性、均衡/加热/open-wire、故障和外部状态。公共电流正放电、负充电，SH 已在后端转换；app 不能再次取反。

- app 名义 200 ms 采样；积分使用实际样本时间差，不是每次调用固定加 200 ms。
- 首个有效样本只建立时间基准；重复时间戳不积分。
- 无效样本/电压作废区间；相邻间隔大于 12800 tick（400 ms）不补算盲区，并用当前帧重新建立基准。
- 时基按 32000 tick/s，无符号减法处理正常回绕；策略按累计实际时间推进 200 ms quantum，每个合格间隔最多两步。
- `abs(current_ma)` 必须同时大于可靠电流下限和配置 deadband 才计入充/放电。当前下限和默认 deadband 均 200 mA，**恰好 200 mA 不积分**。

estimate 与 display 分离；显示节奏受普通、端点接近、确认满电等状态影响，不能统一写成任何时候“1 s 最多 1%”。读取 SOC 诊断时区分估计、显示与最后样本接受状态。

## 2. 配置和曲线

chemistry ID：AUTO=0、LFP=1、NMC=2；profile ID：AUTO=0、GENERIC_LFP=1、GENERIC_NMC=2。当前两个 generic profile version 均为 2，见 `bms_soc_defs.h`。

D008 产品 profile 显式提供 chemistry；SH 当前默认 AUTO/AUTO。AUTO 的解析由 `soc_profile_refresh` 等逻辑完成，保留单体三级 OVP 3900 mV 分界的推断。明确产品应核对真实化学体系，不能把继承默认当标定结果。

默认配置由 `bms_soc_get_default_config()` 和产品 system identity 共同形成：deadband 200 mA，静置准备 600 s，OCV 误差带 ±5 个百分点。`bms_soc_configure()` 校验并通过 Config 保存，成功后切换运行配置及作废旧积分区间。当前没有 `bms_soc_set_product_config()` 或旧 `0x2009` KV 配置入口。

generic 曲线是通用数据，不是某型号电芯的实测曲线；改曲线需实测依据、profile version 与回归。换 chemistry/容量需评估 OTA 的 SOC 和 SOC_STATE 两类编号，详见 [参数策略](OTA_PARAMETERS.md)。

2026-10-09 D008 充电日志确认：约 -2.9 A 充电时 SOC/容量持续为零，手动设置 60% 的写入和回读成功，约一秒后设备原始 SOC 又变为零；同时三级单体欠压位持续存在。公共空电锚点原先只检查该保护位，未排除充电方向，会清掉刚积分的容量和手动设置值。现按可靠充电方向禁止该 SOC 空电锚定，不清除欠压保护位，不改变 MOS 仲裁、保护门限、协议、Flash 或 OTA 参数更新编号。此公共修正适用于四产品；D008 欠压标志持续的底层原因仍需单独定位。

已补充通过实际 app 样本入口的 LFP/NMC 回归场景：从 0% 充电且保留欠压位时累计容量、手动设置 60% 后继续充电不被清零、转入放电后保留原空电锚点。按本机验证约定，本次仅完成日志回放、源码与差异审查，未执行新增 host 回归、目标编译或 OTA，不能据此宣称实板已修复。

## 3. OCV、端点与循环 SOH

OCV 电压取 `(3 * cell_min_mv + cell_max_mv) / 4`。低电流只是静置条件之一，还检查电压稳定、压差、温度、feature/故障及 charger/load 变化。普通 OCV 在达到配置静置时间和质量条件后，只允许估计值向区间上界缓慢下降；不会凭开机高电压、回弹或一次静置测量主动向上校准。

确认充电方向下才允许满电锚点或向 100% 收敛；三级 UVP/放电末端策略可驱动空电锚点。大电流 sag hold、端点质量和 open-wire 诊断暂停/恢复都是独立条件，需沿算法和测试核对，不用瞬时单体电压代替完整路径。

容量学习已删除，没有候选容量、学习窗口或隐藏容量状态。SOH 只使用原有等效放电循环数曲线：

| 循环数 C | SOH（整数百分比） |
|---|---|
| 0..80 | 100 |
| 81..500 | `100 - (C - 80) * 10 / 420` |
| 501..799 | `90 - (C - 500) * 10 / 300` |
| ≥800 | 80 |

整数除法向下取整；循环数饱和到 65535。累计放电百分比达到 100 计一等效循环；浅放电可以累计，充电和一次开关机不计作新循环。有效满容量按名义容量乘 SOH 比例计算，内部保留原积分单位，对外容量仍为 0.01 Ah；容量始终正常显示。修改名义容量保留循环数，并按原 SOC 百分比重算容量。更换电池需主动使用既有循环/SOC 设置或 SOC_STATE 更新编号。

这是基于循环数的估算 SOH，不是测得的电芯容量；沿用既有曲线，不新增未经签核的寿命参数。ETA 使用独立状态和输入，不作为硬件保护依据。

SOC Low 的 `soc_low_first_percent/Second/Third/Rcv/Filter` 由 `soc_update_low_faults()` 消费，写三级 `soc_low`。它是低电量告警，不直接纳入 MOS 阻断掩码。恢复值不足时对相应级别钳位到 trip+1；没有未解决的 SocUp 命名语义。

## 4. 存储与诊断

Config 内部 journal schema 3 保存 chemistry/profile 和 SOC 配置；State schema 3 保存 SOC、循环、放电累计，不再保存工厂老化计时。State 正常约 60 s checkpoint、失败约 5 s 退避，连续三次实际擦写失败后本次启动停止物理写入，并有显式保存入口；不能保证异常断电前最后一帧已落盘。积分余量、OCV 静置和显示跟随在 RAM，启动从整数 SOC 重建剩余容量。完整保存/恢复与各产品休眠差异见 [STORAGE](STORAGE.md)。

`bms_soc_get_diag()` 提供实际 chemistry/profile/version、estimate/display、OCV/rest、循环 SOH、端点、ETA 和样本拒收原因。设备实际值必须读设备；编译默认和 host 仿真不能替代电流标定、真实容量或实板结果。

## 5. 修改后的验证

按 [构建指南](BUILD_AND_TEST.md) 运行 `core_contract_check.py`、`soc_scenarios_host_check.py`，输入边界变化加对应 open-wire/方向/调度测试；存储变化加公共 storage 回归。至少检查首帧、重复、400 ms 边界、gap、时间回绕、200 mA 边界、方向切换、OCV 不向上、满空锚点、循环曲线边界/饱和、容量更新、重启与保存失败。

真实充放电电流、采样时间、长休息、端点、电池容量与掉电恢复仍按 [硬件验收](HARDWARE_VALIDATION.md) 测量。仿真使用生产算法及硬件桩，不能等同整板全链路。

旧学习诊断槽固定为零，SOH source 为 `BMS_SOC_SOH_SOURCE_ESTIMATED_CYCLE`、confidence 为 25。CFG2 的原学习开关两字节与 State 的学习数据位置继续保留，旧值忽略，不因本次删除而重置 SOC/循环；精确布局见 [存储说明](STORAGE.md)。
