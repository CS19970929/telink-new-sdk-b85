# BMS Factory Test Windows

这是从当前最新版 `BmsTool.Windows` 完整 UI、BLE、Modbus、参数、日志、长期监控和 OTA 代码派生的独立 WPF 工厂版。普通客户 EXE 不包含本页；工厂版额外提供“出厂测试”页。程序按以下顺序执行：

1. 建立私有 `0x41` Factory Session；
2. 读取正式保护参数（只读）；
3. 依次对单体/总压、充放电流、充放电温度、MOS 温度、压差和 SOC 低保护，验证一级、二级、三级触发；
4. 清除 RAM 注入并等待恢复；充放电过流遵循固件实际的三级 30 秒自动恢复策略；
5. `finally` 中无论测试成功、失败或取消，都尝试 `CLEAR` 和 `CLOSE`。

测试报告保存到当前 Windows 用户的 `Documents\BmsFactoryReports`。运行前应连接不带负载风险的测试夹具，并确认 MOS 驱动反馈与测试要求一致。未经真实硬件验证，不应把软件构建通过当作整机出厂合格。
