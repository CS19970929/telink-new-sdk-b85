# Flash 与持久化 — D008

## 1. TLSR8251 512 KB 当前布局

| 区域 | 地址 | 大小 | 当前所有者 |
|---|---:|---:|---|
| Firmware A | `0x00000..0x1EFFF` | 124 KB | 当前镜像 |
| OTA A meta | `0x1F000..0x1FFFF` | 4 KB | SDK OTA |
| Firmware B | `0x20000..0x3EFFF` | 124 KB | OTA镜像 |
| OTA B meta | `0x3F000..0x3FFFF` | 4 KB | SDK OTA |
| Event log | `0x40000..0x47FFF` | 32 KB | `bms_event_log` |
| 未分配 | `0x48000..0x4FFFF` | 32 KB | 禁止无审查占用 |
| 历史 BT name 锚点 | `0x50000..0x50FFF` | 4 KB | 当前空置/兼容锚点 |
| Runtime | `0x51000..0x52FFF` | 8 KB | runtime |
| SOC KV | `0x53000..0x5AFFF` | 32 KB | SOC/DSG/cycle |
| Cold KV | `0x5B000..0x5EFFF` | 16 KB | BMS protect/system、SOC chemistry/profile、**AFE HW Protection V2 profile** |
| DVC work-config KV | `0x5F000..0x62FFF` | 16 KB | DVC专属 GP/ADC/WDT/SCD/body-diode 等工作配置 |
| 未分配 | `0x63000..0x73FFF` | 68 KB | 禁止无审查占用 |
| SDK/pairing | `0x74000..0x7EFFF` | SDK定义 | BLE SDK |
| MAC/calibration | `0x7F000..0x7FFFF` | 4 KB | 芯片身份/校准 |

地址真值在 `flash_store_cfg.h`；最终 OTA/保留区还必须与 linker 和 `bms_tools` 一起检查。

## 2. 两类 AFE 数据不能混淆

### AFE Hardware Protection V2

`bms_afe_hw_profile.c` 通过 `bms_cold_kv_store_get/set_afe_hw_profile()` 持久化 35-word 语义 profile。它保存 COV/CUV/OCD/OCC/SCD 等 **独立硬件保护 requested 值**，不再把 `g_tParam.protect` 作为长期唯一硬件 requested 源。

第一次发现空 profile (`schema=0 && model=0`) 时允许从旧 `g_tParam.protect` 生成一次 migration default；保存成功后软件三级参数与 AFE HW profile 独立演进。

### DVC work-config KV

`0x5F000..0x62FFF` 是 DVC 专属工作配置 journal，用于 GP、CADC/VADC、WDT、wake、body-diode、DVC SCD/其它芯片专属语义等。它与 AFE Hardware Protection V2 的 common semantic profile不是同一个 store。

## 3. Store 职责

- `flash_kv32.*`：32-bit key/value journal、CRC、追加、扇区轮换。
- `bms_cold_kv_store.*`：软件保护/system参数、SOC产品身份、升级/reset epoch、BT name、AFE HW V2 profile。
- `soc_kv_store.*`：真实SOC/累计放电量/cycle。
- `dvc1124_config_store.*`：DVC专属工作配置。
- `runtime.*`：累计运行时间。
- `bms_event_log.*`：事件边沿记录。

## 4. 一致性规则

- 软件保护参数写入不得副作用修改 AFE HW profile。
- AFE HW profile 写入必须完整 validate/persist/apply/readback/verify；失败回滚。
- rollback失败必须保持可诊断的 `CONFIG_INCONSISTENT`，不能返回成功。
- 配置 schema 变化必须迁移或明确 fallback；不能把未知旧记录强行解释成新结构。
- Flash 写/擦不在 ISR 执行，并遵守 Telink BLE Flash session 约束。

## 5. 发布验证

每次调整 storage/schema/OTA 后至少执行 `flash_quick_check.py`、TC32 MAP/manifest/verify，并实测：写入中掉电、损坏尾记录、journal切换、factory/reset epoch、OTA中断和参数恢复。禁止全片擦除 `0x74000..0x7FFFF`。