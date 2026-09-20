# D014 上位机诊断协议适配

D014 在 `0x2A00..0x2DFF` 提供与统一上位机兼容的只读 Diagnostics schema 1：

- `0x2A00`：256-word Boot/Storage/FET/Runtime/SOC/Protection 快照；
- `0x2B00`：64 × 12-word RAM Trace；
- magic/schema：`0x4447` / `1`；
- AFE/MCU：`0x3510` / `0x8251`；Runtime version `2`；
- `0x06`/`0x10` 写入与诊断窗口重叠时返回 illegal address。

数据直接来自 D014 的 SH3673510 8S/667µΩ实现。FET 分为 Requested、SH command、
`BSTATUS1` AFE status，均不等于 Gate/Vgs 物理反馈。SOC 发布当前算法真实具备的 profile、OCV、
容量和 SOH；没有实现的 ETA/v3 决策字段保持 unavailable。Storage 覆盖 CONFIG/STATE/EVENT，
FACTORY 当前保持 NOT_RUN。完整诊断会独立读取软件保护与 AFE Hardware Protection V2
requested/meta/effective 窗口，不依赖 D008 参数协议。

```powershell
bms-cli health --mac <MAC> --output .\D014_health.zip --json
bms-cli diag --mac <MAC> --output .\D014_diag.zip --json
bms-cli test connection --mac <MAC> --count 20 --json
bms-cli test soc --mac <MAC> --count 10 --output .\D014_soc_test.json --json
bms-cli test diag --mac <MAC> --count 3 --full --output .\D014_diag_test.json --json
```

构建和 host contract 不能证明 667µΩ 电流标定、TS4 BOM、保护时序、FET 实际导通、
RS485/CMNT-WK、8S balance/open-wire 或低功耗行为；这些仍按 `HARDWARE_VALIDATION.md` 实板验证。
