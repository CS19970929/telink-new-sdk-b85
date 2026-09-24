# 只读诊断会话 v1

本阶段在现有协议上增加持续观察和现场归档。客户 WPF、内部 WPF、CLI 共用 `Shared/BmsDiagnosticSession.cs`。

2026-09-24 D014 固件另行增加只读 MOS 原因字段：Diagnostics schema 仍为 1，`word29 & 0x0002` 表示 word142/143/145–152 有 SH 原始标志、backend、通信保护、温度传感器状态及 TS4 原始值/电阻/温度。共享解码仅在固件声明该位时显示新字段；请求 ON 而 AFE 命令 OFF 时，健康评估增加 `mos.command_gap`，展示阻断原因。旧固件不会显示这些新字段。此扩展没有改变客户端写入、Flash 或 OTA 格式；D014 的 TS4 10K-3435 必需传感器策略见固件分支 `docs/D014_DIAGNOSTICS.md`。

## 使用

CLI 必须明确 MAC 或串口；此命令不接受 `--auto` / `--name`。

```powershell
bms-cli capture --mac A4:C1:38:12:34:56 --output C:\BmsEvidence --count 120 --interval 5 --reconnect --max-reconnects 3 --json
bms-cli capture --serial COM7 --baud 19200 --output C:\BmsEvidence --count 60 --json
```

示例地址必须替换为实际设备。`--count 0` 持续至 Ctrl+C；interval 是一轮读取结束后的等待秒数，不是严格采样周期。每轮含连接/身份/诊断读取的取消期限为 60 秒，底层仍沿用既有通信超时。重连限额是整个会话的总额；重连固定使用初始端点，核对 SN/HW 后才能追加样本。SN/HW 为空或未知时拒绝记录，身份改变时停止。SN 唯一性无法由现有协议证明，不能区分使用相同身份的克隆设备。

两版 GUI：连接指定设备 → BMS 诊断 → “开始诊断会话（写盘）” → 选择目录。“结束诊断会话”会取消在途读取并保存结束信息。复用现有 5 秒刷新、busy 和通信暂停机制，不额外并发轮询。

GUI 连接对象被替换（含重新创建连接）时结束原会话，需要在当前明确选中的设备上开启新会话；不会把另一连接悄悄接入。CLI 支持同端点自动重连。GUI 暂停自动刷新、断线或其他受监控操作期间写入 gap。

## 文件与机器接口

输出目录下每次创建独立 `session-<UUID>/`，不会覆盖前一会话：

- `observations.jsonl`：start/sample/gap/segment/evidence/identity_mismatch/end。每行 schema=1、sessionId、递增 sequence、segment、capturedUtc、kind、data。
- `evidence-NNNNNN.zip`：首帧、每 12 个样本、状态变化、变化后下一帧以及部分失败证据。沿用诊断 ZIP，增加 `session.json` 关联样本与 journal。
- `summary.json`：结束状态、样本数、完整样本数、gap 和分段数。没有 end/summary 表示异常中断，不应判成功。

每行立即 flush 到文件流，进程异常退出后已完成行可以读取；不承诺 OS 缓存掉电持久性。实时读取 Windows 文件时读者需允许 `FileShare.ReadWrite`。最后一行被进程强制终止截断时应忽略该行并标明尾部缺失，不能伪造成功结束。

完整 history 保存在 journal，ZIP 本身不包含整个会话历史；交付现场时保留整个 session 目录。当前不自动轮转/删除文件，长时采集应设置 count 和磁盘容量预算。采集产生真实 BLE/UART 流量，会影响低功耗观察；不能将连接采集得到的功耗直接视为无人访问时功耗。

sample 包含实际读取的身份、诊断 raw words/frames、可读解码、health 和 quality。device.capabilities/runtimeVersion 来自该帧固件；`bms-cli capabilities` 仍是客户端支持目录，新增 `source=client_support_catalog, deviceProbed=false` 明确区别。SH 支持目录更新到 runtime v3，并不要求现场一定已经升级。

quality.complete 表示本轮请求证据读取完整，不代表设备健康，也不代表同一时刻的全局快照。完整采集仍可能 health=critical。bootCheck 沿用现有弱连续性检查；atomicSnapshot=false。仅请求 Trace 且设备声明支持时提供 traceCheck，否则 null。raw frames 只覆盖现有诊断采集中的请求，不包含独立身份探测的所有帧。

退出码：0=有限采集完成且全部证据完整；50=capture_partial（缺样/缺口）；12=连接失败或重连额度耗尽；21=身份改变；130=用户取消，已有数据保留；参数错误为 2。IO/其他错误仍为通用错误 1。stdout 在 --json 下只有最终 envelope，进度在 stderr。错误时也应检查现场目录，不要自动删除部分结果。

## 时间、复位与回放边界

当前固件没有唯一 boot ID，因此不宣称可靠识别所有复位。tick/Trace 倒退只记为 possible_reset_or_clock_discontinuity；跨缺口记为 after_gap_boot_continuity_unknown；长时间间隔记为 clock_continuity_unknown。uint32 tick 正常回绕不算复位；复位后数值恰好未倒退仍可能无法识别。

本记录是 observation，始终 replayReady=false。没有把稀疏采样插值成固件输入，没有将未知 Charger/Load/SOC 真值补成确定值，也没有声称可准确重演 SOC。完整算法输入、产品配置与中途算法初始状态的回放属于后续阶段。旧 SOC CSV 入口保留，并修正 GUI 对精确复现的误导提示。

## 安全与实现边界

会话仅复用 ReadIdentityAsync / ReadDiagnosticsAsync，不调用写寄存器、授权、Factory 注入、MOS 控制或 OTA。取消/断线不会触发设备写入。GUI/CLI 进程之间的全局设备租约、完整配置写审计和 Factory 功能门禁重做仍不在本阶段范围内。

共享诊断测试增加强制完整轮数检查，避免缺样或空集合 All 被误判 PASS。CLI 连接取消路径补充 transport/client 清理。已有协议和旧命令结果 envelope 不变，静态 capabilities 增加来源字段。

## 验证

`test-diagnostics.ps1` 包含会话实路径的模拟通道测试：首帧 ZIP、未关闭前 journal 可读、连续序号、gap 去重、同固件疑似复位、tick 正常回绕、错 SN 拒绝、部分读取失败、取消、缺样不 PASS，以及全部请求仅为读功能码。

`test-cli-offline.ps1 -CliDll <新构建的 bms-cli.dll>` 验证静态能力来源、capture 非显式目标拒绝、原诊断 ZIP 对比。构建客户 WPF、内部 WPF、CLI 和 Core。以上不证明真实设备 BLE/UART 重连或实板时序；实机验收仍需指定设备，执行有限只读采集、拔线/恢复和同固件复位后检查 gap/segment/身份。
