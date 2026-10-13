# 一线通监控

Windows客户版与内部版在实时监控连接区域提供“一线通监控”。先断开原BLE/串口BMS连接并等待任务结束，打开窗口，选择Comm Tool的COM口，点击开始；PC串口固定115200 8N1，不使用主窗口Modbus的19200波特率。模态窗口独占该操作入口，串口由操作系统独占打开。

只移植上位机功能，来源为Comm Tool仓库提交 `5521e85` 的 `tools/comm_tool_owc.py` 和监控流程。不改BMS、Comm Tool、Android或现有CLI命令。两版WPF共用 `Shared/CommToolOwcProtocol.cs`、`CommToolOwcClient.cs`、`OwcMonitorWindow.cs` 和 `MainWindow.Owc.cs`，不启动Python或另一个EXE。

## 连接与显示

- 使用现有Comm Tool硬件接BMS OW/P-；普通USB-UART、USB-RS485或BLE不能直接代替Comm Tool采集一线通。
- PC外层为 `55 AA`、小端、CRC16-Modbus；只发送 `0x43 OWC_CONTROL` 和 `0x44 OWC_SNAPSHOT`。每条命令有1s上限，读等待可取消；停止和关闭尝试发送停止命令，失败后由Comm Tool已有10s租约释放上拉。
- 同时识别32字节头的快照v1/v2。v1末两字节必须为零，不是会话号；v2使用真实session。五ID查询版本、enable、session不一致时整组重读一次，再不一致则停止报错。v1没有自动会话隔离；换电池或BMS重启后必须停止再开始，启用命令会清缓存。
- 所有BMS字段统一按A1.3.3解释，不提供Telink legacy选项。电流为 `raw*0.1-500 A`，正放电负充电；温度为 `raw-40 ℃`，SOC为 `raw*0.5%`。无效及超范围值显示 `--`。
- 展示公有静态资料、3A实时测量/故障/工作状态、3B单体和3C电池编码，以及报文原始字节、年龄、序号、SUM/时序/溢出计数。3C仅显示可打印ASCII；空白、FF及非法编码显示 `--`。
- 5s内有有效帧才在线；3A有效期5s，3B/3C有效期75s。离线隐藏测量及编码；01仅保留静态接入资料，标记离线。3A电压无效或过期时隐藏旧单体。停止、串口错误或租约重启清显示。
- 加密、充电策略和互认按用户要求标记未实现，不解码加密BMS状态，不推断认证成功，也不发送充电应答或参数写入。

## 验证与交付

`powershell -ExecutionPolicy Bypass -File bms-tool-windows/test-owc-protocol.ps1` 仅回放PC快照和BMS报文，不连接设备或生成固件。覆盖v1/v2、量纲、长度/SUM/CRC、非法字段、身份编码、会话一致性和过期隐藏；不能替代实板验证。

`test-owc-ui.ps1 -CustomerAssembly <客户DLL> -InternalAssembly <内部DLL>` 离屏创建两版已编译窗口，检查入口、v1实时数据显示和离线清空，临时截图放用户临时区。2026-10-13协议41项检查与两版窗口检查通过，三入口发布成功；新EXE启动/关闭及CLI帮助入口通过。客户版有原有 `MainWindow._socRecording` 未赋值编译警告，本次新增代码无编译警告；未连接实板复验。

编译客户版、内部版和原CLI，编译缓存放用户临时区。正式包放用户指定固件项目根目录 `releases/bms-tool-windows/<发布标签>/customer`、`internal`、`cli`，附源码提交、原有未提交修改及EXE SHA-256。当前工作树的已有修改保留，发布清单如实标记，不提交无关文件。
