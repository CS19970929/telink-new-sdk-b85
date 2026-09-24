# D014 上位机诊断协议适配

D014 在 `0x2A00..0x2DFF` 提供与统一上位机兼容的只读 Diagnostics schema 1：

- `0x2A00`：256-word Boot/Storage/FET/Runtime/SOC/Protection 快照；
- `0x2B00`：64 × 12-word RAM Trace；
- magic/schema：`0x4447` / `1`；
- AFE/MCU：`0x3510` / `0x8251`；Runtime version `3`；
- `0x06`/`0x10` 写入与诊断窗口重叠时返回 illegal address。

数据直接来自 D014 的 SH3673510 8S/667µΩ实现。FET 分为 Requested、SH command、
`BSTATUS1` AFE status，均不等于 Gate/Vgs 物理反馈。SOC 发布当前算法真实具备的 profile、OCV、
容量和 SOH。Storage 覆盖 CONFIG/STATE/EVENT，FACTORY 当前保持 NOT_RUN。完整诊断会独立
读取软件保护与 AFE Hardware Protection V2 requested/meta/effective 窗口，不依赖 D008 参数协议。

```powershell
bms-cli health --mac <MAC> --output .\D014_health.zip --json
bms-cli diag --mac <MAC> --output .\D014_diag.zip --json
bms-cli test connection --mac <MAC> --count 20 --json
bms-cli test soc --mac <MAC> --count 10 --output .\D014_soc_test.json --json
bms-cli test diag --mac <MAC> --count 3 --full --output .\D014_diag_test.json --json
```

构建和 host contract 不能证明 667µΩ 电流标定、TS4 BOM、保护时序、FET 实际导通、
RS485/CMNT-WK、8S balance/open-wire 或低功耗行为；这些仍按 `HARDWARE_VALIDATION.md` 实板验证。

## MOS 关断诊断与温度传感器策略

### 现场现象与定位

2026-09-24 使用安卓 BMS Tool 对 `BT_D014-8S` 读取完整只读诊断。设备报告 Build ID `7513d77e`、8 个有效单体、AFE 采样有效；CHG/DSG 请求均为 ON，AFE 命令和状态均为 OFF。软件保护三级故障字为 0，但原固件将阻断原因归为“其他 backend 阻断”。这些数据能证明**软件未下发开通命令**，不能证明 Gate/Vgs 或功率通道的物理状态。

源码调用链为 `publish_measurements` → `bms_sw_protection_update` → `BMS_ERROR_TEMP_BREAK` → `bms_sw_protection_charge_blocked` / `discharge_blocked` → `sh3510_apply_requested_fets`。旧 D014 固件错误地将用户确认的 TS4 10K-3435 MOS NTC 配置为不支持，却仍要求 `mos_temp_valid=1` 才清除温度断线，因此从源码推断两路被温度断线阻断。旧固件未直接上报 `TEMP_BREAK`，这一现场原因尚待新诊断固件实板确认。

### 修复后的传感器规则

- TS1/TS2 是必需电池温度输入；任一无效仍触发 `BMS_ERROR_TEMP_BREAK`，充放电关断。
- TS4 是必需的 10K-3435 MOS NTC，单独用于 `u16TmosOTp_*` 高温保护，不进入电池温度 min/max。
- TS4 无效时维持温度断线关断，不能跳过或伪造 MOS 温度。AFE TS4 硬件位暂不开启，因为会共用 TS1/TS2 的 OTC/UTC 阈值。

### 只读诊断字段

保留 Diagnostics schema 1、既有寄存器和所有保护/Flash/OTA 布局。`word29` 增加 `0x0002` 能力位；未声明该位的旧固件仍按旧字段解码。

| 字 | 含义 |
|---|---|
| 136–139 | CHG/DSG 阻断原因；`0x0010` 软件保护，`0x0200` 温度断线，`0x0020` AFE 硬件保护，`0x0008` 通信资格/故障 |
| 142–143 | 最近一次 SH 状态采样缓存的 FLAG1/FLAG2，按原始位保留 |
| 145 | 最近一次 SH BSTATUS2 缓存 |
| 146 | SH backend 状态：输出使能、采样有效、输出抑制、E2P 错误、双向硬件阻断、短路锁存、等待重配、温度断线、AFE/SPI 错误 |
| 147 | 通用 AFE guard：输出授权、通信抑制、总线静默、故障锁存、连续采样合格 |
| 148 | 传感器：bit0 TS1 有效、bit1 TS2 有效、bit2 MOS NTC 保护启用、bit3 MOS NTC 有效 |
| 149–152 | TS4 原始 ADC、有效电阻 Ω（32 位）、编码温度 `(°C+40)*10`；采样无效时结合 word146 判读，不视为实时物理反馈 |

上位机和 CLI 共享解码与健康评估：当请求 ON 而 AFE 命令 OFF 时，生成 `mos.command_gap` 并列出两路阻断原因；温度断线时显示必需传感器状态。FLAG 缓存没有额外触发 AFE 读操作，不能当作故障发生瞬间的锁存证据。`AFE Status` 仍是 BSTATUS1，不是 Gate/Vgs 反馈。

### 复测顺序

1. 记录固件 Build ID、软件版本和设备 SN；确认烧录的是包含本修复的新版本。
2. Windows CLI 可执行 `bms-cli diag --mac <MAC> --json`，或在 BMS Tool 诊断页运行完整诊断；CLI 使用现有 BLE/串口传输，Windows BLE 连接失败时可用安卓读取同一共享解码。
3. 检查 `Requested`、`AFE Command`、`AFE Status`、`CHG/DSG 阻断原因`、`SH backend 状态`、`通信保护状态`、`SH 温度传感器状态` 和 TS4 原始值/电阻/温度。正常 D014 应显示 TS1/TS2/TS4 有效、MOS NTC 保护启用、无温度断线；若命令仍 OFF，按新阻断原因继续定位。
4. 在受控台架上验证 TS1/TS2 断线仍能关断 MOS，恢复后符合现有恢复策略；确认充放电物理 Gate/Vgs、电流和 AFE 硬件保护行为。未经这一步不能把软件/host 检查当作实板安全验收。

本轮仅进行了源码和 host 验证；按用户约定没有自动生成 BIN，也没有将代码刷入设备。
