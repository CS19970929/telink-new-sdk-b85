# SOC 实板 Golden Trace 入口

把已脱敏的实板 `CSV`、`JSON`、`JSONL` 记录放入本目录。`soc_simulator.py validate` 会自动发现并用生产 `SocEnhance.c` 重放，报告中的场景名为 `golden_<文件名>`。

至少应逐步补齐：正常充电、正常放电、Ebike 脉冲、长期静置、接近 UVP、温度变化、充电器插拔、再生充电、MCU reset、Flash restore、保护触发、均衡、加热和 AFE 无效样本。记录必须来自明确的设备/固件/电芯/温度/时间基准；没有可靠 ground truth 时保留 `firmware_soc_est` 用于 A/B 和结构不变量，不能把其自称为独立真值。

本次仓库没有可确认来源的实板 SOC 记录，因此这里不伪造 golden trace。采集格式和导入方式见 `docs/SOC_HOST_TOOL.md` 与 `docs/SOC_SIMULATION.md`。
