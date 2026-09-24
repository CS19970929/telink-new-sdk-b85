# BMS 上位机、App、CLI 功能与协议指南

SOC 在线记录、GUI 曲线、CSV 与离线生产 C 回放见 [SOC_ENGINEERING.md](SOC_ENGINEERING.md)。

本文是 D008、D011、D013、D014 共用客户端的功能入口。具体字段量化和产品硬件结论仍以对应固件分支源码、产品参考文档及实板验证记录为准。

## 1. 单一真源与交付入口

客户端唯一真源为 `feature/windows-afe-hw-protection-editor-v2` 分支的 `bms-tool-windows/`：

| 入口 | 用途 | 主要边界 |
|---|---|---|
| `BmsTool.Windows` | Windows 客户版 | 日常监控、只读诊断、受控参数与 OTA |
| `BmsFactoryTest.Windows` | Windows 内部版 | 完整调试、Factory Session、校准、主动测试 |
| `BmsTool.Cli` | 自动化、AI、本地 CI/HIL | 稳定 JSON、exit code、无 UI 诊断和 OTA |
| `BmsTool.Android` | 手机现场工具 | BLE、诊断、参数、OTA；使用独立移动 UI |
| `BmsTool.Core` / `Shared` | 共用实现 | Modbus、诊断、健康评估、测试引擎、OTA 状态机 |

Windows、Android 和 CLI 不应各自复制协议或 OTA 成功逻辑。平台层只实现 BLE/串口和 UI，业务判断位于共享源码。

## 2. 产品适配矩阵

| 产品 | AFE | Diagnostics | Runtime | AFE Hardware V2 | D008 参数协议 | 当前证据边界 |
|---|---|---:|---:|---:|---:|---|
| D008 | DVC1124 (`0x1124`) | schema 1 | v3 | 支持 | v2 | 已有 D008 实板链路；具体保护仍需按板验证 |
| D011 | SH3673510 (`0x3510`) | schema 1 | v2 | 支持 | 不适用 | 固件构建/host contract；本轮无实板 |
| D013 | SH3673510 (`0x3510`) | schema 1 | v2 | 支持 | 不适用 | 软件协议已适配；专属原理图/BOM 尚缺，不宣称硬件适配完成 |
| D014 | SH3673510 (`0x3510`) | schema 1 | v2 | 支持 | 不适用 | 固件构建/host contract；本轮无实板 |

功能选择必须根据 capability、magic、schema 和 AFE model，不能根据广播名或分支名猜测。D008 专用参数页不得向其他产品发送 `0x2E00` 写命令。

该矩阵同时由共享 `ProductSupportMatrix` 提供给 Windows、Android/Core 和 CLI。自动化使用 `bms-cli capabilities --json` 获取版本与证据边界，避免脚本复制一份容易过期的表。

## 3. 通信与发现

- BLE 只显示 `BT_`、`BT-`，以及经 Nordic UART service 验证的兼容无名设备。
- `--auto` 只有在恰好发现一块兼容 BMS 时才会继续；多设备必须明确指定 `--mac` 或 `--name`。
- Windows/CLI 支持 `--serial COMx --baud 19200`；Android 当前仅支持 BLE。
- Modbus RTU slave address 为 1；读使用 `0x03`，单写 `0x06`，整组写 `0x10`。响应必须经过地址、功能、长度和 CRC 校验。
- `--json` 时 stdout 只输出最终 JSON；诊断日志仅在 `--verbose` 时写 stderr。

## 4. 公共协议窗口

| 地址 | 内容 | 访问 |
|---|---|---|
| `0x0000` | MAC | 只读、部分外置模块可能不支持 |
| `0x0100` | BluetoothName | 只读 |
| `0x1005` | 当前 SOC | 通用单写 `0..100`；写后必须读取实时快照核对，D008/D011/D013/D014 当前固件均支持 |
| `0x1007` | 清事件日志命令 | 内部版受控写入 `0x0001` |
| `0x1102` | 休眠/能力命令 | `0x000A` 休眠；D008 新协议会明确拒绝旧探测值 |
| `0x2100..0x2140` | 软件保护参数 65 words | 读取；写入必须整组校验和分组事务 |
| `0x2500..0x2522` | AFE Hardware V2 Requested | 读；受控整组写 |
| `0x2523..0x252B` | AFE Hardware V2 Metadata | 只读 |
| `0x2540..0x2562` | AFE Hardware V2 Effective | 只读 |
| `0x2A00..0x2AFF` | Diagnostics snapshot，256 words | 只读 |
| `0x2B00..0x2DFF` | Diagnostics trace，64 × 12 words | 只读 |
| `0x2E00..` | D008 参数 capability/参数块 | 仅 D008 capability 通过后使用 |
| `0xC002/0xC012/0xC022` | Serial/Hardware/Software | 只读 ASCII |
| `0xC008` | 事件日志 | 只读分页 |
| `0xD000` | Legacy 实时数据 | 只读 |
| `0xD115` | System status | 只读 |
| `0xD120` | Realtime v2 | 只读，magic `0x4253` |

AFE Hardware V2 的 Requested、Metadata、Effective 是所有已适配产品的独立证据块，读取不得依赖 D008 `0x2E00` capability。Requested 表示请求参数，Effective 才表示固件最终应用值。

## 5. Diagnostics schema 1

快照 magic 为 `0x4447`，schema 为 1。主要 capability：

- bit0 `Boot`：启动冻结快照；
- bit1 `Trace`：RAM 环形事件；
- bit2 `Storage`：Config/State/Factory/Event 存储状态；
- bit3 `MOS`：Requested、AFE command、AFE status；
- bit4 `Upgrade`：D008 启动存储升级细节；
- bit5 `Runtime`：电流、SOC、保护与可选 PM 数据。

`word[14]` 是 AFE model。DVC1124 的 command/status 按 `R81` 和 `R6 CHGF/DSGF` 解码；SH3673510 使用 CHG/DSG bit command 和 `BSTATUS1` status。两者都不是 Gate/Vgs 物理反馈。SH 当前只提供换算后的电流，没有独立 raw 电流；客户端不得将同一个值误标成独立 raw。D008 PM 字段不能套用到未声明这些字段的 SH 产品。

读取流程：先读 16-word header，再分页读取冻结区、MOS 区和 Runtime 区，最后重读 header 判断采集期间是否重启。Trace 分页前后 sequence 必须一致；变化时重试一次，仍变化则保留数据并标记 `TraceConsistent=false`。

完整诊断还会尽力读取身份、事件、软件保护和 AFE 三个证据块。任一可选块失败不丢弃已经取得的快照；错误写入 `errors`。诊断命令不发送参数写入、控制或授权帧。

## 6. CLI 命令

```powershell
bms-cli capabilities --json
bms-cli scan [--scan-seconds 4] [--json]
bms-cli info (--mac MAC | --name NAME | --auto | --serial COMx) [--baud 19200] [--json]
bms-cli soc (--mac MAC | --name NAME | --auto | --serial COMx) [--json]
bms-cli monitor soc (--mac MAC | --name NAME | --auto | --serial COMx) --interval 5 --count 0 --reconnect --output monitor.jsonl --json
bms-cli health (--mac MAC | --name NAME | --auto | --serial COMx) [--quick] [--output health.zip] [--json]
bms-cli diag (--mac MAC | --name NAME | --auto | --serial COMx) [--quick] [--output diag.zip] [--json]
bms-cli test connection (--mac MAC | --name NAME | --auto | --serial COMx) --count 20 --delay-ms 500 --output connection-test.json --json
bms-cli test soc (--mac MAC | --name NAME | --auto | --serial COMx) --count 10 --interval 1 --output soc-test.json --json
bms-cli test diag (--mac MAC | --name NAME | --auto | --serial COMx) --count 3 --interval 1 --full --output diag-test.json --json
bms-cli parameters get (--mac MAC | --name NAME | --auto | --serial COMx) --json
bms-cli parameters export (--mac MAC | --name NAME | --auto | --serial COMx) --output parameters.zip --json
bms-cli compare before-diag.zip after-diag.zip --scope all --output compare.md --json
bms-cli ota firmware.bin (--mac MAC | --name NAME | --auto | --serial COMx) --target auto --mode auto --yes --json
```

`parameters` 中的公共软件保护/AFE 块可用于所有声明相应协议的产品；D008 扩展块只在 D008 capability 通过时出现。`soc` 要求 Runtime v2；v3 才有最近一次积分决策字段。

## 7. 自动测试与判定

- `test connection`：每轮新建 transport、probe、读身份/Build ID、释放连接，记录逐轮 UTC、失败类型、成功率以及 min/P50/P95/max 耗时；`--output` 保存独立 JSON 报告。
- `test soc`：检查读取完整性、SOC/SOH 范围、容量关系、profile、ETA 一致性和 Build ID 稳定性。
- `test diag`：检查 snapshot、trace、errors、Build ID 以及多轮稳定性；`--full` 读取完整证据。
- `health`：只依据 capability 声明和真实数据输出 `pass/info/warning/critical/unknown`，不硬编码未知产品安全阈值。
- `compare`：离线比较升级前后 ZIP，按字段对齐，忽略采集时间；差异分为 `identity / configuration / runtime`，可用 `--scope` 过滤并用 `--output` 生成 Markdown 报告。

exit code 0 表示命令成功；2 参数/确认错误；10 未找到设备；11 多设备拒绝自动选择；12 连接失败；20/21/22 为 BIN/产品/尺寸预检失败；30 OTA 传输失败；33 OTA 证据不足；40 OTA 后通信恢复失败；41 目标版本不匹配；50 自动测试存在失败项；130 取消。

## 电流方向统一约定

应用层、D008 固件内部、SOC、诊断和工厂电流校准统一使用：**正值=放电，负值=充电**。`BatterySnapshot.CurrentA`、CLI、长期监控和内部完整测试版均使用这一约定。

`0xD120` realtime protocol v1 为兼容历史设备保留旧 wire convention：**正值=充电，负值=放电**。该差异只允许存在于 `BmsClient` 协议边界，读取 v1 后立即反转为应用层统一方向；Legacy D000 的独立充/放电幅值也在同一边界转换。不要在 UI、SOC、诊断或校准逻辑中再次翻转符号。未来若定义 D120 v2，应直接采用应用层统一方向。

## 8. 诊断 ZIP

ZIP 至少包含 manifest、boot、storage、MOS、current、SOC、power、runtime protection、software protection、AFE、health、trace、raw frames 和 errors。`SnapshotConsistent`、`TraceConsistent` 以及 `errors` 必须随报告保存，不能只导出看似正常的解析值。

建议故障闭环：

1. `info --json` 固定设备身份和 Build ID；
2. `health --output before.zip --json` 保存升级前证据；
3. 执行明确目标、明确 BIN 的 OTA；
4. `health --output after.zip --json` 保存升级后证据；
5. `compare before.zip after.zip --json` 区分身份/配置/运行态变化。

## 9. OTA 安全边界

- 拒绝 `*.raw.bin`；Telink BIN 必须通过格式、TLNK marker、size 和 CRC/trailer 预检。
- 多设备时禁止 `--auto`；机器模式不带 `--yes` 返回 `confirmation_required`，不得等待输入。
- D008 MTU=23 时 Auto 使用 Legacy 16-byte；不能擅自强制 Extend64。
- “重新连上”不是成功证据。至少需要 `OTA_SUCCESS`、Software 变化或 Firmware Build ID 变化之一；同 Build 且无 `OTA_RESULT` 必须返回 `ota_unconfirmed`。
- OTA 前后证据目录应保存固件 SHA-256、身份、Build ID、诊断和参数备份。

## 10. 构建与本地 CI

```powershell
dotnet build .\bms-tool-windows\BmsTool.Cli\BmsTool.Cli.csproj -c Release
.\bms-tool-windows\test-ota-protocol.ps1
.\bms-tool-windows\test-diagnostics.ps1
.\bms-tool-windows\build-release.ps1
```

普通本地 CI/host test 能证明编译、编码、状态机、JSON 和静态契约；不能证明 BLE 射频可靠性、真实保护动作时间、MOS Gate/Vgs、实板电流精度、Flash 掉电一致性或 OTA 断电恢复。涉及这些结论必须使用目标板和测量设备留下 HIL/实测证据。

## 11. 相关文档

- [CLI 详细说明](CLI.md)
- [诊断与测试路线](DIAGNOSTICS_TEST_ROADMAP.md)
- [D008 Diagnostics 字段](D008_DIAGNOSTICS.md)
- [AFE Hardware Protection V2](AFE_HARDWARE_PROTECTION_V2.md)
- [Telink OTA 审计](TELINK_OTA_MASTER_AND_WINDOWS_OTA.md)
- [Android 使用与验证](ANDROID.md)
