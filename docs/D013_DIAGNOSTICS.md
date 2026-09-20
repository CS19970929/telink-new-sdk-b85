# D013 上位机诊断协议适配

D013 当前源码在 `0x2A00..0x2DFF` 提供统一上位机只读 Diagnostics schema 1：

- `0x2A00`：256-word Boot/Storage/FET/Runtime/SOC/Protection 快照；
- `0x2B00`：64 × 12-word RAM Trace；
- magic/schema：`0x4447` / `1`；
- AFE/MCU：`0x3510` / `0x8251`；Runtime version `2`；
- `0x06`/`0x10` 写入与诊断窗口重叠时返回 illegal address。

当前数据源基于源码中的 SH3673510 / 4S / 100µΩ profile 和 direct UART/BLE Modbus 通道。
FET 分为 Requested、SH command、`BSTATUS1` AFE status，均不等于 Gate/Vgs 物理反馈。
SOC 仅发布当前算法真实具备的 profile、OCV、容量和 SOH；ETA/v3 决策字段保持 unavailable。
Storage 覆盖 CONFIG/STATE/EVENT，完整诊断独立读取软件保护和 AFE Hardware Protection V2
requested/meta/effective 窗口，不依赖 D008 参数协议。

```powershell
bms-cli health --mac <MAC> --output .\D013_health.zip --json
bms-cli diag --mac <MAC> --output .\D013_diag.zip --json
bms-cli test connection --mac <MAC> --count 20 --json
bms-cli test soc --mac <MAC> --count 10 --output .\D013_soc_test.json --json
bms-cli test diag --mac <MAC> --count 3 --full --output .\D013_diag_test.json --json
```

重要边界：当前缺少 D013 专属原理图/BOM。上述实现证明软件协议已适配，不能证明继承的 `D011_*`
GPIO、NTC、heater/fuse、100µΩ或通信网络已经完成硬件适配。取得产品资料后仍须按
`D013_PRODUCT_REFERENCE.md` 与 `HARDWARE_VALIDATION.md` 逐项签核。
