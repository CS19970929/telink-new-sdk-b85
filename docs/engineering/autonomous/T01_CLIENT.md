# 客户端连接与参数快照可靠性

## 2026-11-14 统一客户端主线

从 `audit/client-reliability-20261001@cdd1797b` 合入生命周期修复，并与当前串行连接入口、C060/C077 参数、AFE 原子事务和设备数据清屏规则合并。未修改固件、寄存器、参数缩放或存储布局。

- 连接替换增加 generation，取消旧连接 lifetime，使 AFE、软件保护和业务参数捕获失效。相同地址重连也必须重新读取。
- AFE 与软件保护候选绑定读取时的 `BmsClient` 和 generation；确认返回及实际发送前再次检查归属。
- 异步读取、保存结果和旧异常不得覆盖新连接的界面或恢复新连接轮询。
- `BmsClient.DisposeAsync` 幂等取消进行中和排队事务，忽略迟到回调。普通调用方取消等待不支持取消的物理写入退出，保持事务串行。
- 保留现有 `_connectionOperations` 连接串行化、软件保护五字段整组提交和 AFE expected-snapshot 原子事务，不恢复旧的逐寄存器保存实现。

## 本轮验证

实际客户版和内部版 WPF 程序集各通过 15 项连接生命周期检查：Dispose 取消进行中及排队请求、销毁后拒绝请求、普通取消不重叠底层写入、未读取快照拒绝保存、跨连接快照失效，以及迟到电池/软件保护读取不能覆盖新连接。测试使用真实 `MainWindow`、`BmsClient` 和 STA Dispatcher，传输替换为受控 fake；没有设备 IO。

`test-client-lifecycle.ps1` 引用实际构建的程序集，不再复制生产方法或使用精简版 MainWindow 代替实际窗口。

```powershell
$evidence = Join-Path $env:LOCALAPPDATA 'CodexTemp/bms-client/lifecycle'
python -B bms-tool-windows/Tests/autonomous/run_client_lifecycle.py --out $evidence
```

也可用 `test-client-lifecycle.ps1 -CustomerAssembly <实际DLL> -InternalAssembly <实际DLL> -OutputDirectory <仓库外路径>` 检查已经构建的程序。Python 入口在仓库外构建实际 WPF 项目，保存日志和输入程序集 SHA-256。

本轮三端 Release 编译零警告、零错误。完整客户端整合和平台验证记录见 [统一客户端开发指南](../../../bms-tool-windows/docs/CLIENT_MAIN.md)。

## 历史证据与边界

`T01_CLIENT_EVIDENCE.json` 是原审计分支的历史证据，基线为 `26d978af2538438edf7a2c8557f2bc4c50ee6824`，文件 SHA 与场景数量仅适用于当时的测试输入，不能作为当前主线的构建指纹。本轮不把历史 16 场景报告冒充当前测试结果。

未验证真实 BLE / Serial 切换、OS 断连通知时延、手机或实板操作。本项不声称所有诊断/日志/OTA 页面都已统一改造生命周期，也不解决 Modbus 无事务 ID 下所有迟到响应关联问题；软件检查不能替代实板验收。
