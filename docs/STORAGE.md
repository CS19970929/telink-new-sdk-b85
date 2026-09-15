# Flash 与持久化 — D008

## 1. TLSR8251 512 KB 当前布局

| 区域 | 地址 | 大小 | 当前所有者 |
|---|---:|---:|---|
| Firmware A | `0x00000..0x1EFFF` | 124 KB | 当前镜像 |
| OTA A meta | `0x1F000..0x1FFFF` | 4 KB | SDK OTA |
| Firmware B | `0x20000..0x3EFFF` | 124 KB | OTA 镜像 |
| OTA B meta | `0x3F000..0x3FFFF` | 4 KB | SDK OTA |
| Event log | `0x40000..0x47FFF` | 32 KB | `bms_event_log` |
| 未分配 | `0x48000..0x4FFFF` | 32 KB | 禁止无审查占用 |
| 历史 BT name 锚点 | `0x50000..0x50FFF` | 4 KB | 当前空置/兼容锚点 |
| Runtime | `0x51000..0x52FFF` | 8 KB | runtime |
| SOC KV | `0x53000..0x5AFFF` | 32 KB | SOC/DSG/cycle |
| Cold KV | `0x5B000..0x5EFFF` | 16 KB | BMS software protect/system、SOC identity、AFE HW Protection V2 profile |
| Legacy DVC config reservation | `0x5F000..0x62FFF` | 16 KB | **当前不读取、不写入；保留窗口，后续复用前必须评审** |
| 未分配 | `0x63000..0x73FFF` | 68 KB | 禁止无审查占用 |
| SDK/pairing | `0x74000..0x7EFFF` | SDK 定义 | BLE SDK |
| MAC/calibration | `0x7F000..0x7FFFF` | 4 KB | 芯片身份/校准 |

地址宏仍在 `flash_store_cfg.h` 中保留部分历史兼容/布局信息；**地址被保留不代表当前存在 DVC operating-config Flash owner**。

## 2. 当前持久化所有权

### 软件保护参数

`g_tParam.protect` / `bms_cold_kv_store`：

- First / Second / Third
- Recover
- Filter
- 软件 OV/UV/OC/温度等保护参数

### AFE Hardware Protection V2

`bms_afe_hw_profile.c` 通过 cold KV 持久化独立的 AFE hardware requested profile：

- COV / CUV
- OCD1 / OCD2
- OCC1 / OCC2
- SCD
- delay / recover / enable mask

首次发现空 profile 时允许从旧软件保护参数生成一次 migration default；保存成功后软件保护与 AFE HW profile 独立演进。

### 固定 DVC 工作配置：不持久化

以下配置不再保存到 DVC 专属 KV，而由 `dvc1124_project_config.h` 编译期唯一拥有，每次 AFE reset/init 后重新下发：

- GP / topology / high-side mask
- CADC / CC1 / VADC
- Charge Pump
- V3P3 / timed wake / interrupt mask
- DPC
- current-wake 固定策略
- Body-Diode / DBDM / CBDM
- DVC I2C watchdog
- I2C timeout close CHG/DSG
- fixed Core-OT policy

历史 `0x5F000..0x62FFF` 中即使存在旧 `flash_kv32` journal，新固件也不得读取、restore 或用其覆盖当前固件宏。

## 3. 当前 Store 职责

- `bms_cold_kv_store.*`：软件保护/system 参数、SOC 产品身份、升级/reset epoch、BT name、AFE HW V2 profile。
- `soc_kv_store.*`：SOC/累计放电量/cycle。
- `runtime.*`：累计运行时间。
- `bms_event_log.*`：事件记录。
- `flash_kv32.*`：仍可被其他现存 KV 使用；**不得重新用于 DVC fixed operating config**。
- `dvc1124_config_store.*`：文件名为历史兼容；当前不再是 Flash store，只承载 DVC backend 生命周期和 compile-time fixed config 应用。

## 4. 配置一致性规则

- 软件保护写入不得副作用修改 AFE HW profile。
- AFE HW profile 写入必须完整 validate / persist / apply / readback / verify；失败回滚。
- 固定 DVC configuration 不允许通信写入，也不允许 raw write 创建第二配置 owner。
- `0x2800` fixed semantic fields 与 `0x2900` raw mirror 仅用于 read-only diagnostics。
- Flash schema 变化必须迁移或明确 fallback；未知旧记录不能强行解释成新结构。
- Flash 写/擦不得在 ISR 执行，并遵守 Telink BLE Flash session 约束。

## 5. 发布验证

每次调整 storage/schema/OTA 后至少执行：

- `flash_quick_check.py`
- TC32 clean build
- MAP / manifest / verify
- 掉电写入、损坏尾记录、journal 切换、factory/reset epoch、OTA 中断、参数恢复实测

DVC fixed configuration 的发布验证应独立确认：旧 `0x5F000..0x62FFF` 内容不会影响上电后的 R77/R53/R54/R66 等最终配置。

禁止全片擦除 SDK 保留区 `0x74000..0x7FFFF`。
