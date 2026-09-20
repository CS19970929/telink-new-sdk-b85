# SOC 开发、记录与离线回放

D008、D011、D013、D014 共用现有 `BmsClient`、transport、Modbus 和 Runtime Diagnostics decoder。SOC 工具没有第二套 BLE/串口通信栈，也不修改任何固件寄存器或 Flash 布局。

## CLI 记录

```powershell
bms-cli soc --mac <MAC> --json
bms-cli monitor soc --mac <MAC> --interval 5 --output monitor.jsonl --json
bms-cli record soc --mac <MAC> --output soc_record.csv --interval 5 --count 0
```

`record soc` 同步读取 quick diagnostics 和 Battery snapshot，并逐条流式落盘为可由固件仓库 `tools/soc_simulator/soc_simulator.py` 直接重放的 CSV，不在内存中无限累积。`--count 0` 持续到 Ctrl+C；取消时仍保留已完成样本。

CSV 的 `current_ma` 使用 SOC core 的负充正放约定。公共 realtime window 原本是正充负放，因此只在记录边界显式反号；`current_raw_ma` 保留固件 runtime 值供核对。温度使用 `(degC+40)*10`，均衡/加热来自 SystemStatus。charger/load 没有独立存在传感器时写 `UNKNOWN_OR_IDLE`，不能解释成确定 absent。

## Windows GUI

客户版和内部版通过 `Shared/MainWindow.Diagnostics.cs` 共用实现：

- “开始/停止 SOC 记录”；
- “导出 SOC CSV”；
- “载入 SOC CSV”离线回放，不连接、不写入设备；
- “SOC 曲线/回放”显示 estimate、display、OCV、归一化 current 和事件线；
- GUI 最多保留 20000 条，长时间采集使用 CLI。

## 离线分析

```powershell
python tools\soc_simulator\soc_simulator.py replay soc_record.csv --output replay.csv --metrics metrics.json
python tools\soc_simulator\soc_simulator.py compare soc_record.csv --output ab.json
```

`compare` 输出 Firmware vs 当前生产 C Host replay 的最大绝对差和平均差。没有外部电量计时，`firmware_soc_est` 只是 A/B 基线，不是独立 ground truth。经设备/固件/工况确认的实板记录应进入固件仓库 `tests/soc/traces/`，由 `validate` 自动纳入回归。

## 证据边界

离线 CSV、Windows build 与 Host replay 能证明协议解析、记录格式、确定性和软件不变量；不能证明 BLE 长稳、电流测量精度、真实 OCV/容量、保护时序、休眠复电、MOS Gate/Vgs 或板级安全。
