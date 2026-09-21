# SOC HIL CLI

`bms-cli test soc-hil` 在真实 BMS MCU 上按确定性的 200 ms 虚拟时间步运行生产 SOC 算法，并自动保存逐场景证据。每条样本仍由板端采样任务消费，CLI 等待 `applied_count` 确认后才提交下一条；BLE 轮询延迟不会被错误计入算法时间。它需要带 `BMS_SOC_HIL_ENABLE=1` 的开发固件；生产固件明确不支持此命令。

```powershell
bms-cli test soc-hil `
  --mac A4:C1:38:1D:70:8B `
  --suite all `
  --yes `
  --output C:\BmsEvidence\soc-hil.json `
  --json
```

安全规则：

- 只接受明确 `--mac` 或 `--serial`，拒绝 `--auto`/`--name`。
- 必须提供 `--yes`，不会在 JSON 自动化中等待输入。
- 板端要求真实 AFE 样本有效且真实电流在 `-200..+200 mA`。
- 模拟输入不替代真实保护/MOS 数据，不写 Flash。
- CLI 在 `finally` 中发送 `CLOSE`；断链时固件会立即恢复，最迟也会在 8 秒超时恢复。
- 报告检查 `GAP`、方向、SOC 单调性、无效样本恢复、会话关闭和正常状态回读。
- 该命令验证真实 MCU 上的算法与命令消费链，不单独证明物理采样周期；真实周期由普通 runtime diagnostics/recording 验证。

套件：

- `basic`：静置、死区、加速放电、加速充电。
- `all`：在 `basic` 上增加单体压差、无效样本/恢复、开线疑似。

可选参数：

- `--seed 1..99`：沙箱初始 SOC；默认采用测试前真实 SOC。
- `--accelerated-current-ma 1000..3000000`：加速积分电流；默认 `1500000`。
- `--output PATH`：保存完整 JSON 证据。

加速电流仅是 SOC 算法输入，不会控制外部电源/电子负载，也不代表硬件额定能力。物理电流采样、MOS 路径和保护阈值仍需真实充放电设备验证。
