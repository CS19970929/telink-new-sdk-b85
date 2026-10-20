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

## 2026-10-20：SOC模型v3配置入口

配套固件策略以固件仓库 `docs/SOC_REFACTOR_20261020.md` 为准。客户版继续隐藏算法页；内部版改为完整模型选择/JSON导入、设备值与候选对比、确认、原有stage/commit/readback事务。算法数值不再逐项开放给普通客户。两候选均标注工程评估，不代表适配全部电芯。

保持50-word布局和子接口签名1，新增能力0x0800识别策略v3。旧固件只读备份，保存/恢复默认前拒绝不支持的固件；v2模型不得静默改成v3。候选化学体系须与设备一致，改类型继续走整套类型事务。JSON包含格式版本、名称、来源及工程候选属性，限制1MiB。

严格100%由固件判满资格决定：人工设置100%不授予资格，内部可为100%、目标显示99%；运行显示每5秒最多变化1个百分点。满空端点内部立即同步，保护不等显示。普通OCV静置60分钟后还需累计30分钟合格修正时间，每次最多向下1个百分点；LFP任一单体3200～3400mV禁止普通OCV。SOC不作温度补偿。

源码回归入口 `test-soc-profile-policy.ps1`：实际Shared模型代码配合寄存器替身，覆盖JSON往返、曲线/版本/类型/能力门禁和事务读回；不能替代WPF或真实设备验收。本轮未执行该脚本、客户/内部/CLI构建或发布EXE。
