# D008 SOC 回放

`short-discharge.csv` 是合成的固定验收向量。它覆盖 CC2 时间回绕、测量连续区间和重启切断积分。实板向量应记录装配、固件、时间源、负载及基准仪表。

从仓库根目录运行 `python tools/soc_simulator/soc_simulator.py tests/soc/traces/short-discharge.csv --start 82`。工具从当前生产 C 源码临时编译，并输出每行的内部电量和显示。写了 `expected_shown` 的行会检查预期。

CSV 字段为时间 `timestamp_ms`、最近一次 CC2 时间 `cc2_ms`、带符号的 `current_ma`、单体极值及均值 `min_mv/max_mv/mean_mv`、`temperature_x10` 和 `flags`。电流正值放电。

| `flags` 位 | 含义 |
|---|---|
| 0、1、2 | 电流有效、电压有效、新 VADF |
| 3、4、5 | OV 截断、UV 截断、允许静置校正 |
| 6、7 | COW 检查中、COW 检查成功 |
| 8 | 按上一行检查点重启 |
