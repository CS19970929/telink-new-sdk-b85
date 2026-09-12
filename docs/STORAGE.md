# Flash 与持久化

## 1. TLSR8251 512 KB 当前布局

| 区域 | 地址 | 大小 | 所有者 |
|---|---:|---:|---|
| Firmware A | `0x00000..0x1EFFF` | 124 KB | 当前镜像 |
| A 尾部保留 | `0x1F000..0x1FFFF` | 4 KB | SDK OTA |
| Firmware B | `0x20000..0x3EFFF` | 124 KB | OTA 镜像 |
| B 尾部保留 | `0x3F000..0x3FFFF` | 4 KB | SDK OTA |
| event log | `0x40000..0x47FFF` | 32 KB | 事件日志 |
| 未分配 | `0x48000..0x4FFFF` | 32 KB | 禁止无审查占用 |
| 历史 BT name 锚点 | `0x50000..0x50FFF` | 4 KB | 当前空置 |
| runtime | `0x51000..0x52FFF` | 8 KB | 老化/模式时间 |
| soc KV | `0x53000..0x5AFFF` | 32 KB | SOC/DSG/cycle |
| cold KV | `0x5B000..0x5EFFF` | 16 KB | BMS 参数、控制项、BT name |
| DVC config KV | `0x5F000..0x62FFF` | 16 KB | DVC 专属语义配置 |
| 未分配 | `0x63000..0x73FFF` | 68 KB | 禁止无审查占用 |
| SMP/master pairing | `0x74000..0x7EFFF` | SDK 定义 | BLE SDK |
| MAC/校准 | `0x7F000..0x7FFFF` | 4 KB | 芯片身份与校准 |

`flash_store_cfg.h` 是业务分区地址的源码真值。512 KB + OTA 只接受 multiple boot `0x20000`；`0x40000` 以后不是 OTA 镜像空间。

## 2. Store 职责

- `flash_kv32.*`：32 位 key/value journal 引擎，负责扫描、CRC、追加和扇区轮换。
- `soc_kv_store.*`：高频但变化缓慢的真实 SOC、DSG、cycle；任一值变化才写。
- `bms_cold_kv_store.*`：保护参数、系统参数、升级/reset epoch 和 BT name suffix。
- `dvc1124_config_store.*`：DVC 工作模式、GP、ADC、WDT、SCD、Body Diode 等 AFE 专属配置。
- `runtime.*`：独立 A/B 日志，记录累计 awake 时间和工厂/正常模式。
- `bms_event_log.*`：100 条事件边沿记录；只有持久化成功后才更新软件 latch。

COV、CUV、OCD1/OCC1、OCD2/OCC2 的 requested 值只属于 `g_tParam.protect` / cold KV。DVC config KV 不得复制这些值：

```text
g_tParam requested -> DVC encode/quantize -> live register -> effective readback
```

## 3. 一致性规则

- 所有 Flash 地址、扇区数、记录 schema、CRC 和 epoch 都是兼容性接口。
- 写入失败必须向调用者返回失败；不能更新内存缓存或事件 latch 后假装成功。
- 配置事务顺序为 validate -> apply/readback -> persist；persist 失败时回滚并验证。
- 遇到非法尾记录不能继续在脏尾后追加，应切换/重建有效 journal。
- 空 Flash、未知 schema 或损坏记录使用明确默认值；不能拼接不同代记录得到半新半旧状态。
- 擦除和写入不在 ISR 中执行；BLE stack Flash session 期间遵守现有锁定约束。

## 4. OTA 与升级

- 最终 BIN 必须 `<= 124 KB`。
- 不得把 OTA boot 改到 `0x40000`，否则会覆盖 event log。
- AFE KV schema 变化必须提供迁移或明确 fallback-to-default。
- 升级重置使用各 store 的 epoch，仅在重置成功后写入新 epoch。
- 禁止全片擦除或擦除 `0x74000..0x7FFFF`。

布局契约由 `tests/flash_quick_check.py` 覆盖；每次调整地址或 OTA 配置后仍必须查看最终 MAP 并实测 OTA、掉电和恢复路径。
