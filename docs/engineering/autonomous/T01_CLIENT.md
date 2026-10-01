# T01：Windows / Factory 连接与参数快照可靠性

基线：`26d978af2538438edf7a2c8557f2bc4c50ee6824`。分支：`audit/client-reliability-20261001`。本项仅改变客户端连接、事务退出和快照所有权；未改变寄存器、协议帧、固件、产品版本或硬件。结果需与同目录 `T01_CLIENT_EVIDENCE.json` 的源文件 SHA256 一起使用。

## 复现与修复

CF01 在 Windows 和 Factory 的真实 AFE 事件处理函数中均重现：读取 A（word 24 = 2000），切换到 B（word 24 = 5000），保存旧候选，基线向 B 发出一次写入，把 word 24 改为 2000。基线还存在重复读取产生两套请求、Dispose 后排队事务未退出的问题。

连接更换先清空当前引用、增加 generation、取消旧连接 lifetime，并使 AFE、软件保护与 D008 参数捕获失效。释放旧资源使用局部引用，避免释放新连接。AFE 快照绑定读取时的 BmsClient 实例和 generation；相同后端或相同地址重新连接也必须重读。确认弹窗返回后再次验证归属。开始保存时使原快照失效；失败后必须重读，成功读回才重新绑定。AFE 读写忙标志阻止同时重复点击。

BLE / Serial 连接探测、基础测量及身份读取、软件保护读写、基本参数写入在发布结果前检查连接归属。D008 参数页同样丢弃旧连接的捕获、异常提示与刷新恢复；Factory 校准候选在 await 后检查连接取消。本文不声称其他所有诊断、日志、OTA 页面都已完成统一生命周期改造。

BmsClient.DisposeAsync 幂等取消进行中与排队事务，忽略迟到旧回调。普通调用方取消仍等待底层不支持取消的写入退出，避免提前释放事务锁；只有 Dispose 可以提前结束这段等待，因为该实例已禁止后续事务。SemaphoreSlim / CTS 不主动 Dispose，避免仍在退出的等待者访问已释放对象；此处未创建 WaitHandle，由 GC 回收托管对象。

## 验证方法与结果

正式测试入口：`bms-tool-windows/Tests/autonomous/run_client_lifecycle.py`。实际 AFE UI 文件、模型、协议和 BmsClient 直接编译；连接、测量及 ParameterRunAsync 从实际 MainWindow 逐字提取并记录哈希。WPF STA Dispatcher 与控件是真实实例，弹窗回答和 OS transport 由最小 fake 代替。没有连接真实设备、调用系统 BLE 扫描或安装应用。

Windows / Factory 各 16 个场景通过：跨设备旧快照零写入、重复读取、销毁进行中与排队事务、新设备重读后正常保存、确认取消、确认期间切换连接、迟到响应不改新快照、物理断连拒绝写入、调用方取消及销毁后拒绝请求、不协作写入在销毁时退出、迟到 BLE 连接、迟到 Serial 连接、旧参数异常不覆盖状态或重启刷新、旧电池测量不覆盖新测量、普通取消保持写入串行化、软件保护读取按钮入口不显示旧异常或恢复新连接轮询。

基线 CF01 / 重复读取 / Dispose 各有明确失败证据。补充参数 catch/finally 用例与普通取消串行化用例也分别在修复前失败、修复后通过。测试夹具因省略无关页面而有 CS0649 字段未赋值警告；没有抑制生产项目警告。

Windows、Factory、CLI、Core Release 与 Android Debug 均编译；Core 和 Android 纳入验证是因为它们实际引用被修改的 BmsClient。既有 `test-afe-fragments.ps1` 通过，执行时仅重定向源根与临时输出根。最终返回码、构建摘要、测试名称、输入哈希见证据 JSON。构建产物与完整日志全部位于外部临时目录。

复跑示例（在本工作树根执行）：

```powershell
python -B bms-tool-windows/Tests/autonomous/run_client_lifecycle.py --out "$env:LOCALAPPDATA/CodexTemp/telink-autonomous-20261001/client/recheck"
dotnet build bms-tool-windows/BmsTool.Windows/BmsTool.Windows.csproj -c Release --artifacts-path "$env:LOCALAPPDATA/CodexTemp/telink-autonomous-20261001/client/recheck-build" --nologo
```

## 证据边界

这是源码、真实 WPF 处理函数 + fake transport、客户端编译证据。尚未验证真实 BLE / Serial 设备切换、无线栈时序、连接丢失的 OS 通知时延和完整桌面交互。Modbus 同一存活连接上取消后的迟到响应本来没有事务 ID；本项没有宣称解决全部响应关联歧义。生产代码改动限 9 个文件，总增删量低于 600 行；不产生固件 BIN、不修改固件协议、不发布应用。

独立 Spec Review 的软件保护入口补充项：两端真实 `ReadProtection_Click` + `ReadProtectionAsync` + 保护模型/协议在修复前均产生旧错误 1 次和刷新恢复 2 次；增加同代 catch/finally 后均为旧错误 0 次、仅 B 操作恢复 1 次，且 B 参数保持 5000。两端 16 项最终通过，Windows / Factory Release 再编译零警告零错误；BmsClient 和其余构建输入保持已验证版本。
