# D008 / D011 / D013 统一软件保护框架

更新日期：2026-09-14。

## 目标

三个产品共享同一份 `bms_sw_protection.c/.h`。AFE backend 只负责采样、寄存器量化、硬件保护状态和硬件锁存恢复；软件阈值判断、三级滤波、恢复滤波和软件故障历史不再按 AFE 各写一套。

## 三级语义

- **First**：一级告警/报告，不直接关闭 MOS。
- **Second**：二级告警/报告，预留降额/策略升级，不直接关闭 MOS。
- **Third**：软件保护级，参与 CHG/DSG MOS 禁止。
- **AFE Hardware Protection**：独立安全后备，可以因芯片量化和硬件时序早于软件 Third 动作；backend 将有效硬件保护状态 OR 到 Third，再统一记录故障边沿。

软件三级与硬件保护不是“谁覆盖谁”，而是并行保护通道。软件层不能读写 AFE 寄存器，硬件层不能重新定义 First/Second/Third 的软件语义。

## 当前统一的软件保护项

1. 单体过压 / 欠压；
2. 总压过压 / 欠压；
3. 充电过流 / 放电过流；
4. 充电高温 / 低温；
5. 放电高温 / 低温；
6. MOS 高温；
7. 单体压差过大。

参数仍来自 `g_tParam.protect`，采样节拍按 200 ms。触发和恢复都使用同一参数滤波时间转换为采样次数；恢复必须连续满足恢复条件，不再出现 D008 旧逻辑“单次达到恢复值立即清故障”的差异。

## 温度断线

backend 向公共层提供 Battery min/max、MOS 温度以及有效性。任一必需温度无效时置 `BMS_ERROR_TEMP_BREAK`，软件温度阈值状态清零，MOS 由既有 fail-safe 门控禁止；恢复有效后再重新经过正常温度保护滤波。

## 硬件保护仍保持产品/AFE差异

本轮不统一以下内容：

- DVC1124 与 SH3673510 的寄存器、量化步进和硬件延时；
- SCD/SC 的硬件阈值与恢复条件；
- AFE WDT；
- Load Detect / LOADOFF；
- Body Diode；
- OpenWire / Balance；
- 产品 GPIO、Rsense、NTC 和 FET 拓扑。

这些属于 backend / Product Profile，不应为了“代码看起来一样”强行采用同一寄存器配置。

## 暂不纳入的 SOC 保护

历史结构同时存在 `u16SocUp_*`、`b1SocLow` 和 `BMS_FAULT_SOC_HIGH_*` 三套互相矛盾的命名。当前代码没有足够证据确定其产品语义，因此 v1 明确不把 SOC 阈值加入公共保护状态机。待协议/产品语义确认后再单独迁移，避免静默改变已出货行为。

## 后续

统一软件保护以后，下一阶段应统一 `bms_output_arbiter` 和硬件故障恢复语义，使最终 MOS 决策固定为：

`product request × output enable × communication health × software Third × hardware protection × hardware lockout/recovery`
