# BMS CLI：快速 OTA 与 AI 实板诊断

`BmsTool.Cli` 是 Windows 上位机的无 UI 入口。它不复制另一套 BMS 协议，而是直接复用当前上位机的 BLE、串口、Modbus、Telink OTA、STM32 IAP 和统一 Diagnostics 源码。完整的跨产品功能、协议窗口与验证边界见 [BMS 上位机、App、CLI 功能与协议指南](BMS_TOOL_GUIDE.md)。

## 目标

典型使用场景：

- 不打开 WPF，快速给桌面上一块明确识别的兼容 BMS OTA。
- Codex/AI 在 Windows 机器上自动执行 `scan -> info -> ota -> diag`。
- 远程终端、PowerShell、CI 或批处理调用。
- 出问题时直接取得结构化 JSON 和 AI 诊断 ZIP，而不是截图 UI。

## 命令

```powershell
bms-cli capabilities
bms-cli scan
bms-cli info --auto
bms-cli health --auto
bms-cli soc --auto
bms-cli monitor soc --auto --interval 5
bms-cli record soc --auto --output soc_record.csv --interval 5 --count 0
bms-cli diag --auto
bms-cli test connection --auto --count 10 --output connection-test.json
bms-cli test soc --auto --count 10 --output soc-test.json
bms-cli test diag --auto --count 3 --full --output diag-test.json
bms-cli parameters export --auto --output parameters.zip
bms-cli compare before.zip after.zip --scope all --output compare.md
bms-cli ota firmware.bin --auto
```

指定设备：

```powershell
bms-cli info --mac A4:C1:38:12:34:56
bms-cli info --name BT_D008_01
bms-cli info --serial COM7 --baud 19200
```

### 快速 OTA

桌面上只有一块兼容 BMS：

```powershell
bms-cli ota .\825x_ble_sample.bin --auto --yes
```

如果扫描到多块设备，`--auto` 会返回 exit code 11，不会擅自选择第一块。

指定 MAC：

```powershell
bms-cli ota .\825x_ble_sample.bin --mac A4:C1:38:12:34:56 --yes
```

Telink D008 当前建议：

```powershell
bms-cli ota .\825x_ble_sample.bin --mac A4:C1:38:12:34:56 --target telink --mode auto --yes
```

D008 当前 MTU=23，因此 Auto 实际使用 Legacy 16B。

## AI / Codex 使用

机器调用必须优先使用 `--json`。stdout 只保留最终 JSON；`--verbose` 的通信细节写到 stderr，不污染 JSON。

发现设备：

```powershell
bms-cli scan --json
```

读取身份和实时状态：

```powershell
bms-cli info --auto --json
```

完整诊断：

```powershell
bms-cli diag --auto --json
```

`record soc` 与 Windows 双版本的 SOC 记录/曲线、CSV 字段和生产 C 离线回放见 [SOC_ENGINEERING.md](SOC_ENGINEERING.md)。

统一健康评估（默认同时采集完整诊断证据）：

```powershell
bms-cli health --mac A4:C1:38:12:34:56 --output .\health.zip --json
```

健康评估只根据固件明确声明的保护、采样、启动、存储、构建来源及数据自洽性给出 `pass / info / warning / critical / unknown`，不会自行发明产品安全阈值，也不会把 DVC `CHGF/DSGF` 或 SH `BSTATUS1` 当作物理 MOS 反馈。

多轮真实连接/读取/断开测试：

```powershell
bms-cli test connection --mac A4:C1:38:12:34:56 --count 20 --delay-ms 500 --json
```

每轮都会重新建立 transport、完成 Modbus probe、读取身份/实时状态/Build ID；D008 capability 是可选证据，然后释放连接。任一轮失败时仍输出全部尝试记录，并以 exit code 50 结束。

报告包含每轮 UTC、错误类型、成功率、平均值以及 min/P50/P95/max 成功耗时。`--output` 保存独立 JSON，便于长稳回归留档；原有 JSON 顶层字段保持兼容，新指标只做追加。当前统计的是“连接 + probe + 身份/实时读取”的端到端耗时；RSSI、GATT status、首帧与断开阶段耗时仍属于后续 transport instrumentation。

只读取 typed SOC Runtime Diagnostics v2：

```powershell
bms-cli soc --auto
bms-cli soc --auto --json
bms-cli monitor soc --auto --interval 5 --count 12
bms-cli monitor soc --auto --interval 5 --count 0 --reconnect --output .\soc-monitor.jsonl --json
```

`--count 0` 表示持续监视，Ctrl+C 停止。`monitor soc --json` 每个样本输出一行独立 JSON；`--output` 同步保存 JSONL。`--reconnect` 仅在显式指定时启用，采样错误也会写入 JSONL，旧连接会先释放，再按 `--max-reconnects` 的上限尝试恢复。SOC 输出包含 estimate/display、nominal/effective/remaining capacity、chemistry/profile、OCV band/confidence、endpoint、filtered current/variation、TTE/TTF、ETA state/direction/confidence、SOH source/confidence，以及容量学习 candidate/count/reject。固件 runtime version <2 时明确返回 `soc_diagnostics_unavailable`，不会把保留零解释成有效数据；runtime v3 还包含最近一次采样判定、积分方向、SOC 动作、动作前后值、目标/原因、elapsed 和积分增量。

自动测试和参数备份：

```powershell
bms-cli test soc --mac A4:C1:38:12:34:56 --count 10 --interval 1 --output .\soc-test.json --json
bms-cli test diag --mac A4:C1:38:12:34:56 --count 3 --interval 1 --full --output .\diag-test.json --json
bms-cli parameters get --mac A4:C1:38:12:34:56 --json
bms-cli parameters export --mac A4:C1:38:12:34:56 --output .\parameters.zip --json
bms-cli compare .\before-diag.zip .\after-diag.zip --scope configuration --output .\compare.md --json
```

`test soc/diag` 的 exit code 50 表示测试完成但至少一个检查项失败；JSON 报告仍保存全部逐轮证据。`compare` 按 `Id/Field` 对齐数组项并忽略采集时间，只报告真实字段差异；原有 `beforePath / afterPath / differenceCount / differences` 保持顶层兼容，新增 `categoryCounts / scope / output`。`--scope` 可选择 `all / identity / configuration / runtime`，`--output` 输出适合审查与归档的 Markdown。

无需连接设备即可读取统一产品矩阵：

```powershell
bms-cli capabilities --json
```

该输出同时说明 Diagnostics/Runtime/参数协议版本和当前硬件证据边界；“软件已适配”不等于“对应实板已验收”。

同时生成现有 AI 诊断包：

```powershell
bms-cli diag --auto --output .\BMS_diag.zip --json
```

AI 可以直接使用 JSON 中的：

- 软件/硬件版本和 Serial。
- Firmware Build ID。
- 电流、SOC、低功耗、保护和 MOS 决策。
- Storage 状态。
- RAM Trace。
- 参数/AFE evidence。
- 原始 Modbus frames。
- errors / consistency 状态。

因此推荐 AI 调试闭环：

```text
修改对应产品源码
  -> python bms_tools/bms.py build
  -> bms-cli ota <bin> --mac <target> --yes --json
  -> bms-cli diag --mac <target> --json
  -> 按 capability 分析保护 / SOC / Power / Storage / Trace
  -> 下一轮修改
```

CLI 本身不调用 LLM；它负责给 AI 提供稳定、可重复、机器可解析的真实设备证据。

## JSON 契约

成功：

```json
{
  "schema": 1,
  "ok": true,
  "command": "info",
  "data": {}
}
```

失败：

```json
{
  "schema": 1,
  "ok": false,
  "error": {
    "code": 12,
    "kind": "connect_failed",
    "message": "...",
    "details": null
  }
}
```

不要让自动化脚本解析中文 UI 文本；判断结果使用 JSON 字段和进程 exit code。

## Exit code

| Code | 含义 |
|---:|---|
| 0 | 成功 |
| 1 | 未分类异常 |
| 2 | CLI 参数/确认错误 |
| 10 | 未找到设备 |
| 11 | 找到多块设备，拒绝自动选择 |
| 12 | BLE/串口/Modbus 连接失败 |
| 20 | BIN 无效 |
| 21 | 产品/固件预检不匹配 |
| 22 | 固件超尺寸 |
| 30 | OTA 传输失败 |
| 33 | OTA 后设备恢复，但没有足够的新固件启动证据 |
| 40 | OTA 后无法恢复 BMS 通信 |
| 41 | 目标软件版本不匹配 |
| 50 | 测试命令完成，但至少一个测试项失败 |
| 130 | 用户或 Ctrl+C 取消 |

## OTA 成功标准

CLI 与 WPF 使用同一套 Telink OTA 实现。CLI 不把“设备重新连上”当成成功。

至少需要：

- OTA Server 明确确认成功；或
- 软件版本实际变化；或
- Firmware Build ID 实际变化。

`--expected-version` 用于“不匹配即失败”，但同版本文本匹配不会单独证明重刷成功。

需要完整升级证据时使用：

```powershell
bms-cli ota .\825x_ble_sample.bin --mac A4:C1:38:12:34:56 --target telink --mode auto `
  --evidence-dir .\ota-evidence --yes --json
```

证据目录包含固件路径/大小/SHA-256、升级前后 `snapshot.json`、完整诊断 ZIP、参数 ZIP、每阶段证据采集状态以及 `ota-result.json`。证据采集失败会被明确记录，不会伪装成成功；OTA 本身仍以 `OTA_SUCCESS`、版本变化或 Build ID 变化为准。

## 构建

```powershell
dotnet build .\bms-tool-windows\BmsTool.Cli\BmsTool.Cli.csproj -c Release
```

运行：

```powershell
dotnet run --project .\bms-tool-windows\BmsTool.Cli\BmsTool.Cli.csproj -- scan
```

正式发布由 `build-release.ps1` 与 GitHub Actions 同时生成 UI、内部完整版和 CLI。

## 持续只读诊断会话

新增 capture 命令，支持固定设备、持续写盘、缺口和保守分段、异常前后证据。使用方法、JSONL schema、退出码与局限见 [只读诊断会话 v1](DIAGNOSTIC_SESSION.md)。


开发固件的完整 SOC 输入采集：`record soc --inputs --mac MAC --count 300 --output inputs.csv --json`。要求显式MAC/serial；只读、默认release不支持。见 [SOC工作流](SOC_WORKFLOW.md)，包含缺口/退出码、GUI导入、生产C Replay及A/B限制。

需要在真实 D008 MCU/200 ms 调度器上主动注入 SOC 场景时，使用 `test soc-hil --mac MAC --suite all --yes --output report.json --json`。该命令只支持专用开发固件，拒绝自动选设备，并在结束、异常、断链或超时后恢复原 SOC RAM 状态；协议与安全边界见 [SOC HIL CLI](SOC_HIL_CLI.md)。
