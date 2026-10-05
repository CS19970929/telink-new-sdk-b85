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

默认配置由 `bms_soc_get_default_config()` 和产品 system identity 共同形成：deadband 200 mA，静置准备 600 s，OCV 误差带 ±5 个百分点，学习关闭。`bms_soc_configure()` 校验并通过 Config 保存，成功后切换运行配置及作废旧积分区间。当前没有 `bms_soc_set_product_config()` 或旧 `0x2009` KV 配置入口。

generic 曲线是通用数据，不是某型号电芯的实测曲线；改曲线需实测依据、profile version 与回归。换 chemistry/容量需评估 OTA 的 SOC 和 SOC_STATE 两类编号，详见 [参数策略](OTA_PARAMETERS.md)。

## 3. OCV、端点与学习

OCV 电压取 `(3 * cell_min_mv + cell_max_mv) / 4`。低电流只是静置条件之一，还检查电压稳定、压差、温度、feature/故障及 charger/load 变化。普通 OCV 在达到配置静置时间和质量条件后，只允许估计值向区间上界缓慢下降；不会凭开机高电压、回弹或一次静置测量主动向上校准。

确认充电方向下才允许满电锚点或向 100% 收敛；三级 UVP/放电末端策略可驱动空电锚点。大电流 sag hold、端点质量和 open-wire 诊断暂停/恢复都是独立条件，需沿算法和测试核对，不用瞬时单体电压代替完整路径。

学习默认关闭。启用后要求合格完整充放路径；反向、重启、采样失效/gap、温度/保护、校准变化、均衡/加热等有拒收原因。候选容量范围为名义 50%～130%，并有重复候选一致性和更新幅度限制；不代表单次充放电一定完成学习。ETA 使用独立状态和输入，不作为硬件保护依据。

SOC Low 的 `u16SocLow_First/Second/Third/Rcv/Filter` 由 `soc_update_low_faults()` 消费，写三级 `b1SocLow`。它是低电量告警，不直接纳入 MOS 阻断掩码。恢复值不足时对相应级别钳位到 trip+1；没有未解决的 SocUp 命名语义。

## 4. 存储与诊断

Config schema 2 保存 chemistry/profile 和 SOC 配置；State schema 2 保存 SOC、循环、放电累计、学习数据及工厂计时。State 正常约 60 s checkpoint、失败约 5 s 退避，并有显式保存入口；不能保证异常断电前最后一帧已落盘。显示 SOC 不作为另一套 KV 保存。

`bms_soc_get_diag()` 提供实际 chemistry/profile/version、estimate/display、OCV/rest、学习、端点、ETA 和样本拒收原因。设备实际值必须读设备；编译默认和 host 仿真不能替代电流标定、真实容量或实板结果。

## 5. 修改后的验证

按 [构建指南](BUILD_AND_TEST.md) 运行 `soc_contract_check.py`、`soc_simulator_check.py`，输入边界变化加对应 open-wire/方向/调度测试；存储变化加公共 storage 回归。至少检查首帧、重复、400 ms 边界、gap、时间回绕、200 mA 边界、方向切换、OCV 不向上、满空锚点、学习拒收和保存失败。

真实充放电电流、采样时间、长休息、端点、电池容量与掉电恢复仍按 [硬件验收](HARDWARE_VALIDATION.md) 测量。仿真使用生产算法及硬件桩，不能等同整板全链路。
