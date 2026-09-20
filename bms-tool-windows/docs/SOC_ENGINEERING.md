# SOC 开发、记录与离线回放

输入契约和准确命令以 [SOC_WORKFLOW.md](SOC_WORKFLOW.md) 为入口。普通 SOC CSV 是稀疏观察；完整输入录制需要配套开发固件，两者不能作为等价 Replay 证据。

D008、D011、D013、D014 共用现有 `BmsClient`、transport、Modbus 和 Runtime Diagnostics decoder。SOC 工具没有第二套 BLE/串口通信栈，也不修改任何固件寄存器或 Flash 布局。

## CLI 记录

```powershell
bms-cli soc --mac <MAC> --json
bms-cli monitor soc --mac <MAC> --interval 5 --output monitor.jsonl --json
bms-cli record soc --mac <MAC> --output soc_record.csv --interval 5 --count 0
```

`record soc` 依次读取 quick diagnostics 和 Battery snapshot，并逐条流式落盘，不在内存中无限累积。这些窗口不构成原子快照，也不包含每次算法调用的完整输入。`--count 0` 持续到 Ctrl+C；取消时仍保留已完成样本。

CSV 的 `current_ma` 直接使用 Runtime word196 的 SOC 输入 signed mA（负充正放），不再从 realtime 的0.1A显示值反推或重复反号；`current_raw_ma` 保留 word194。温度使用 `(degC+40)*10`，均衡/加热来自 SystemStatus。charger/load 的电流推断仅作观察，`UNKNOWN_OR_IDLE` 不能解释成确定 absent。

## Windows GUI

客户版和内部版通过 `Shared/MainWindow.Diagnostics.cs` 共用实现：

- “开始/停止 SOC 记录”；
- “导出 SOC CSV”；
- “载入 SOC CSV”离线查看已有曲线，不连接、不写入设备，也不执行生产 C 算法；
- “SOC 曲线/回放”显示 estimate、display、OCV、归一化 current 和事件线；
- GUI 最多保留 20000 条，长时间采集使用 CLI。

## 离线分析

完整输入使用 `record soc --inputs`，之后执行 `audit` / `replay` / `ab`；配套分支、编译选项和参数见 [SOC_WORKFLOW.md](SOC_WORKFLOW.md)。普通观察CSV缺输入或周期过稀时必须拒绝精确Replay，不能补假数据后声称已复现。没有外部电量计时，`firmware_soc_est`不是独立ground truth；seeded Replay也不是任意现场完整checkpoint恢复。

## 证据边界

离线 CSV、Windows build 与 Host replay 能证明协议解析、记录格式、确定性和软件不变量；不能证明 BLE 长稳、电流测量精度、真实 OCV/容量、保护时序、休眠复电、MOS Gate/Vgs 或板级安全。
