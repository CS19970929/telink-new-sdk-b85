# Windows SOC 开发、记录与回放

## 1. 单一通信入口

客户版 `BmsTool.Windows`、内部版 `BmsFactoryTest.Windows` 和 `BmsTool.Cli` 继续复用既有 `BmsClient`、BLE/串口 transport、Modbus、诊断窗口和 `SocDiagnosticSnapshot` decoder。没有另建 SOC 专用通信层，也没有修改固件协议地址。

Runtime Diagnostics v3 提供 estimate/display、三类容量、OCV band/state/confidence、endpoint、filtered current、TTE/TTF、ETA confidence、SOH、learning candidate/count/reject，以及最近样本/积分/SOC 动作。

## 2. CLI

在线读取与持续监控：

```powershell
bms-cli soc --mac <MAC> --json
bms-cli monitor soc --mac <MAC> --interval 5 --output monitor.jsonl --json
```

采集可直接送入 PC 仿真器的 CSV：

```powershell
bms-cli record soc --mac <MAC> --output soc_record.csv --interval 5 --count 0
```

`--count 0` 持续到 Ctrl+C。停止时已采集样本仍会保存。BLE 自动选择规则不变：只有显式 `--auto` 且恰好一台兼容设备时才连接。

## 3. GUI

两版 Windows 工具的“BMS 诊断”页共用以下功能：

1. “开始 SOC 记录”打开 5 s 自动诊断，并在同一采样点读取 Battery snapshot；
2. “停止 SOC 记录”只结束记录，不写设备；
3. “导出 SOC CSV”保存标准记录；
4. “载入 SOC CSV”离线显示，不连接也不写 BMS；
5. “SOC 曲线/回放”同时绘制 estimate、display、OCV 和归一化 current，并标出 SOC event。

内存中最多保留 20000 条 GUI 样本，避免无限监控耗尽上位机内存。长时间采集建议使用 CLI。

## 4. 记录语义

`current_ma` 按固件核心统一为负充正放；Windows 公共实时窗口原本是正充负放，记录层只在文件边界显式反号。`current_raw_ma` 来自固件诊断，便于检查通信显示值与算法输入差异。

CSV 同时保存 cell/pack voltage、温度、sample validity、Firmware estimate/display/capacity、OCV、endpoint、learning、TTE/TTF、charger/load 状态、L1/L2/L3 保护、均衡、加热和事件。charger/load 的 absent/idle 在没有独立物理检测时标为 `UNKNOWN_OR_IDLE`，不会写成确定“不在”；物理 MOS feedback 仍不由 SOC 工具伪造。

## 5. 离线诊断流程

```text
在线记录 -> 保存原始 CSV -> production-C replay -> metrics.json
                          \-> Firmware vs Host compare -> ab.json
                          \-> GUI 载入 -> 曲线/事件人工审查
```

推荐把设备身份、固件 commit、温度、负载/充电器工况和外部电量计文件与 CSV 一并归档。经确认可公开/入库的记录复制到固件分支 `tests/soc/traces/` 后，会自动进入 `validate` 回归。

上位机功能只证明通信、解码、记录和离线呈现；没有连接实板时不能证明 BLE 稳定性、采样同步、真实电流符号/精度或保护动作。
