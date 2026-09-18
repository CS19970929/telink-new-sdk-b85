# BMS CLI：快速 OTA 与 AI 实板诊断

`BmsTool.Cli` 是 Windows 上位机的无 UI 入口。它不复制另一套 BMS 协议，而是直接复用当前上位机的 BLE、串口、Modbus、Telink OTA、STM32 IAP 和 D008 Diagnostics 源码。

## 目标

典型使用场景：

- 不打开 WPF，快速给桌面上一块 D008 OTA。
- Codex/AI 在 Windows 机器上自动执行 `scan -> info -> ota -> diag`。
- 远程终端、PowerShell、CI 或批处理调用。
- 出问题时直接取得结构化 JSON 和 AI 诊断 ZIP，而不是截图 UI。

## 命令

```powershell
bms-cli scan
bms-cli info --auto
bms-cli diag --auto
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

同时生成现有 AI 诊断包：

```powershell
bms-cli diag --auto --output .\D008_diag.zip --json
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
修改源码
  -> python bms_tools/bms.py build
  -> bms-cli ota <bin> --mac <target> --yes --json
  -> bms-cli diag --mac <target> --json
  -> 分析保护 / SOC / PM / Storage / Trace
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
| 130 | 用户或 Ctrl+C 取消 |

## OTA 成功标准

CLI 与 WPF 使用同一套 Telink OTA 实现。CLI 不把“设备重新连上”当成成功。

至少需要：

- OTA Server 明确确认成功；或
- 软件版本实际变化；或
- Firmware Build ID 实际变化。

`--expected-version` 用于“不匹配即失败”，但同版本文本匹配不会单独证明重刷成功。

## 构建

```powershell
dotnet build .\bms-tool-windows\BmsTool.Cli\BmsTool.Cli.csproj -c Release
```

运行：

```powershell
dotnet run --project .\bms-tool-windows\BmsTool.Cli\BmsTool.Cli.csproj -- scan
```

正式发布由 `build-release.ps1` 与 GitHub Actions 同时生成 UI、内部完整版和 CLI。
