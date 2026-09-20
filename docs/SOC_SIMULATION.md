# SOC PC 仿真与回放

## 1. 目标与边界

`tools/soc_simulator/soc_simulator.py` 不重写 SOC 算法。它通过 `tests/d008_power_soc_host_check.py --compile-soc-executable` 把生产 `SocEnhance.c` 编译为临时可执行文件，Python 只负责输入归一化、独立电池模型、场景生成、指标和报告。临时文件位于 `%LOCALAPPDATA%\CodexTemp\telink-bms\soc-simulator`。

支持 CSV、JSON、JSONL。相同归一化输入和 seed 必须得到逐字节相同输出。

## 2. 常用命令

```powershell
python tools\soc_simulator\soc_simulator.py scenario --all --seed 20260921
python tools\soc_simulator\soc_simulator.py validate --monte-carlo 5000 --seed 20260921
python tools\soc_simulator\soc_simulator.py replay record.csv --output replay.csv --metrics metrics.json
python tools\soc_simulator\soc_simulator.py compare record.csv --output ab.json
python tests\soc_simulator_check.py
```

`compare` 保留记录中的 `firmware_soc_est`，用相同输入重放当前 Host SOC，输出样本数、最大绝对差、平均差和 Host replay 路径，用于 Firmware vs Host / before vs after A/B。

## 3. 输入契约

标准列包括 `timestamp_32k`、`current_ma`、cell/pack voltage、`temp_min_x10/temp_max_x10`、sample/voltage valid、balance/heating/open-wire/fault/protection、charger/load known/present、event、`true_soc` 与 `firmware_seed_soc`。电流符号是负充正放，温度使用 `(degC+40)*10`。

缺失的真实 SOC 不会被描述成独立 ground truth。Windows 实板记录会保留 `firmware_soc_est`；这种记录主要验证重放差异和结构不变量。要评估绝对误差，必须由电量计、积分仪或受控充放电台架提供可追溯 `true_soc`。

## 4. 独立真值模型

场景模型使用独立的 LFP OCV 表、容量、库仑真值、充电效率、内阻、温度容量折减、电压噪声、电流量化/offset/deadzone。它不读取固件 OCV profile，因此不会拿算法自己的答案验证自己。

这仍是软件模型，不是特定 32700/21700 电芯标定。仿真误差只能用于回归和风险发现，不能代替真实电芯的多温度 OCV/容量数据。

## 5. 标准场景

共 27 个：标准充/放电、Ebike 脉冲、储能低功耗、静置 10 min/30 min/2 h、24 h 死区漏电、OCV 大偏差、MCU reset、Flash restore、50% 掉电、温度变化、大倍率压降、UVP 附近负载、充电器插拔、再生充电、电流零偏/突变、容量衰减、均衡、加热、保护触发、方向快速切换、AFE 无效、单体异常和 Open-Wire。

`validate` 另外生成指定数量的确定性随机场景，随机覆盖初值、容量、温度、电流、offset、噪声、时间间隔、时标回绕附近、无效帧、保护、均衡、加热、Open-Wire 与 reset。

## 6. 指标与不变量

- SOC 最大/平均/RMS/终值误差；
- display 最大跳变和“未被 endpoint/reset 解释”的最大跳变；
- 充放电方向反向变化计数；
- SOC 0..100、容量非负/非零不变量；
- OCV 纠偏次数和收敛误差；
- full/empty 到达时刻误差（场景实际触发时才有值）；
- capacity estimate error；
- TTE/TTF 在 `VALID` 状态下相对 recent-current 理想值的平均误差。

随机场景的初始 Firmware SOC 可故意偏离 truth，因此随机部分的 pass 只判结构不变量，不把任意初值误差伪装为算法精度结论。命名场景另有限定误差与专用 OCV 收敛判据。

## 7. 实板 Golden Trace

`tests/soc/traces/` 是唯一实板记录入口。`validate` 自动发现其中的 CSV/JSON/JSONL，并作为 `golden_<文件名>` 场景重放。本次仓库没有可确认来源的实板 SOC 记录，所以报告中 `golden_trace_count=0`；该缺口明确保留为 `TODO_VERIFY_HW`，没有用合成数据冒充实板数据。
