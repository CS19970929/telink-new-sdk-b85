# D008 通用 SOC / ETA / 容量学习增强报告（2026-09-19）

## 范围

本轮直接修改生产 `SocEnhance.c/.h`、现有 chemistry profile、State Journal、Runtime Diagnostics 和配套 Windows Tool；没有建立 Demo、算法副本或第二套 SOC 状态 owner。保护输入、Cell OV/UV、Current/Temperature Protection、AFE Hardware Protection 和 FET 仲裁未被 SOC 显示逻辑替代或放宽。

## 原实现审核结论

保留并继续使用的基础包括：实际 32k elapsed Coulomb integration、estimate/display 分离、OCV center+band、普通 OCV 只向下、满电充电方向门禁、terminal tracking、sag hold、UVP final anchor、无效/长间隔冻结和 State checkpoint。主要缺口是 endpoint/display 没有统一可诊断状态，ETA 不存在，容量学习一次完整循环就直接接受、endpoint qualification 不足，SOH 来源不可区分，Windows/CLI 缺少机器可读 SOC 深度诊断。

## 当前策略

- profile v2：LFP/NMC 继续共用算法，只提供不同 OCV/full/empty/knee 参数；LFP OCV band 至少 7%，NMC 至少 5%。
- Full：真实充电 + Third Cell OVP 为强安全锚点；正常 Full 还要求 max/min voltage、cell delta 连续合格。estimate 分段靠近 100，display 在 approach/confirmed 区加快但不跳变。
- Empty：按距离 UVP 的 profile margin 逐步靠近 5/3/1/0；大倍率 sag hold 保留。Third Cell UVP 仍最终强制 0，并记录 early UVP / sag / imbalance / capacity mismatch 事件。
- VERY_LONG_REST：选择保守策略 A，完全禁止向上 OCV correction。没有 D008 实际电芯多温度 OCV 标定前，不启用默认关闭以外的长期上拉策略。
- ETA：30 s 稳定方向、signed EMA current、EMA variation；基于 remaining/effective capacity。变化负载、方向切换、死区、无效样本、endpoint correction 与充电 taper 返回 low-confidence/unavailable。
- Learning：默认关闭。高质量 endpoint、0.05 C 最大 endpoint 电流、50 mV 最大 delta、采样/AFE communication/Open-Wire/温度/保护/方向门禁；学习期间改变 current offset/gain 也作废。两次 5% 内一致 candidate 才接受，每组最多移动 effective capacity 5%。重启不续接 session。
- SOH：未学习时明确为 cycle-estimated/confidence 25；accepted capacity 后为 capacity-learned/confidence 100。

## 持久化与诊断

State schema 3 payload 从 32 增到 52 bytes，增加 candidate、valid/rejected count、last reason、candidate match 与 active marker；slot 64 增到 84 bytes，8 个 4 KiB sector 的理想完整轮转记录从 512 降到 384。按项目开发期规则不迁移 State schema 2。正常 checkpoint/受控关机、commit-last、CRC 和掉电回退机制保持。

Runtime Diagnostics 升为 v2，保留原 offset 193..225，在 226..248 增加 profile/capacity/endpoint/ETA/SOH/learning。Windows 客户版与内部版复用 `Shared/BmsDiagnostics.cs`；CLI 增加 `soc`、`soc --json`、`monitor soc`，没有复制 Modbus 协议。

## Host 证据

`tests/d008_power_soc_host_check.py` 编译并执行实际生产 SOC C translation unit，不复制算法。覆盖：

- LFP/NMC、正负实际时间积分、INT32_MIN、200 mA deadband、201 mA 起积分、32k wrap、无效/重复/>400 ms 样本；
- 10 min OCV qualification、band 内不动、只向下、静置高压不 Full、正常充电 Full、80% 强锚后的 display soft landing，以及 15% 到 12/6/3/1/0 的无故障低端轨迹；
- 稳定 10 A 充/放电约 300 min ETA、变化负载/taper/跨零 invalid、最大容量 ETA overflow；
- early UVP diagnostics、high-current false Empty、weak/imbalanced false Full、Open-Wire、温度、丢样、方向反转、reboot；
- candidate 一致/不一致，两轮确认，容量 100%->95%->90%->85% 的 <=5% bounded update；
- 30 天 Monte Carlo（load/rest/direction/noise/missing/reboot）与 90 天 20%~80% 浅循环；learning 默认关闭仍满足 SOC/capacity/ETA invariants，并分别执行 estimate 对模型 true SOC 误差不超过 15 / 8 percentage points 的保守门禁；
- 7 天 CSV 轨迹生成。该轨迹含长期 150 mA deadband 漏电：普通 OCV 从不向上，在向下 band correction 下 estimate 维持保守上偏，证明当前更需要实测小电流/OCV 收敛，而不是开放回弹上拉。

这些是软件模型/状态机证据，不是电芯模型精度、硬件电流精度或真实到端时间验收。

## 资源与门禁

新增 RAM 主要来自 SOC runtime 和更大的 State encode/decode payload；扩展诊断直接消费同一个 `bms_soc_diag_t`，没有在 200 ms sample task 再复制第二个大结构。精确 `.bss/.data`、固件增量、`_ram_use_end_` 与 600-byte stack guard 以本提交 TC32 MAP 为准。Host compiler 通过不能替代 TC32 link/check-fw/MAP/verify/cppcheck，也不能替代运行栈高水位测试。

实板未决统一保留在 [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md)：profile/OCV 标定、±200 mA 死区与零漂、多温度/倍率、Full/Empty 端点、弱单体、ETA 真实误差、容量学习误拒绝/漏拒绝、Flash 掉电、运行栈和 UART/BLE 诊断读取。
