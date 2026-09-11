# TLSR8251 512 KB Flash 布局

本文件记录当前工程实际采用的 512 KB 布局。地址依据当前 SDK OTA 头文件、`vendor/ble_sample/flash_store_cfg.h`、`bms_cold_kv_store.h` 和 `vendor/common/ble_flash.h`；更改任何地址前必须同时检查 OTA、业务存储和 SDK 保留区是否冲突。

| 区域 | 地址范围 | 大小 | 依据 / 用途 |
|---|---:|---:|---|
| Firmware A | `0x00000..0x1EFFF` | 124 KB | SDK 默认最大固件 124 KB；当前运行镜像 |
| A 侧 OTA 保留尾扇区 | `0x1F000..0x1FFFF` | 4 KB | SDK 明确规定每个默认 128 KB OTA 区最后 4 KB 不可用于固件 |
| Firmware B | `0x20000..0x3EFFF` | 124 KB | B85 默认 OTA multiple boot 地址为 `0x20000` |
| B 侧 OTA 保留尾扇区 | `0x3F000..0x3FFFF` | 4 KB | 与 Firmware B 配套的保留 4 KB |
| event_log | `0x40000..0x47FFF` | 32 KB | `FLASH_ADDR_LAYOUT_512K_LOG_BASE`，8 个 4 KB 扇区 |
| 预留间隙 | `0x48000..0x4FFFF` | 32 KB | 当前业务存储未分配；不得擅自占用 |
| 历史 BT name 锚点 | `0x50000..0x50FFF` | 4 KB | 代码保留的兼容锚点，当前布局注释定义为 vacant region |
| runtime | `0x51000..0x52FFF` | 8 KB | 2 个 4 KB 扇区 |
| soc_kv | `0x53000..0x5AFFF` | 32 KB | 8 个 4 KB 扇区 |
| cold_kv | `0x5B000..0x5EFFF` | 16 KB | 4 个 4 KB 扇区；BMS 保护参数/系统参数/控制项/BT name |
| dvc1124_afe_cfg_kv | `0x5F000..0x62FFF` | 16 KB | 4 个 4 KB 扇区；DVC1124 工作模式、GP、WDT、SCD 等语义配置，独立于 `g_tParam` |
| 未分配区 | `0x63000..0x73FFF` | 68 KB | 当前工程未分配；不得覆盖 SDK 保留区 |
| SMP pairing | 从 `0x74000` 起 | SDK 定义 | `FLASH_ADR_SMP_PAIRING_512K_FLASH` |
| master pairing | 从 `0x78000` 起 | SDK 定义 | `FLASH_ADR_MASTER_PAIRING_512K`；同时存在历史参数锚点，需保持兼容审查 |
| MAC / 校准 | 从 `0x7F000` 起 | 4 KB 顶部区域 | `CFG_ADR_MAC_512K_FLASH`；禁止擦除 |

## DVC1124 配置持久化边界

DVC1124 新增的 `dvc1124_afe_cfg_kv` 只保存**器件工作配置和 DVC 专属硬件策略**，例如：

- CADC / CC1 timing；
- charge pump / VADC；
- GP1..GP6 mode；
- V3P3 / I2C watchdog / timed wake / interrupt mask；
- current wake / body diode；
- DSG pull-down；
- DVC core over-temperature；
- SCD threshold / delay。

以下保护参数**不复制**到该区域：COV、CUV、OCD1/OCC1、OCD2/OCC2。它们继续以现有 `g_tParam.protect` / `cold_kv` 为唯一请求值来源，DVC 驱动只负责量化为 AFE 可实现值。

这样避免出现两套保护参数来源：

```text
g_tParam requested protection
        -> DVC encode / quantize
        -> live AFE register
        -> effective value readback
```

## 强制约束

- 512 KB + OTA 配置只接受 multiple boot `0x20000`；`flash_store_cfg_layout_supported()` 会拒绝其它地址。
- Firmware BIN 必须小于等于 124 KB；每次增加 DVC 配置服务后必须重新检查最终 BIN 大小。
- 烧录应用固件只使用 `0x00000` 起始地址。
- 禁止全片擦除，禁止擦除 `0x74000..0x7FFFF`。
- `0x40000` 以后不是 OTA 镜像空间；把 OTA boot 改为 `0x40000` 会直接覆盖 event_log。
- `0x5F000..0x62FFF` 已分配给 DVC1124 AFE KV，不再属于未分配空间。
- AFE KV schema 变化必须使用兼容迁移或明确 fallback-to-default 策略，不能把旧数据按新结构直接解释。
- 未分配区只是当前代码没有使用，不代表未来可无审查占用。

## 源码取证位置

- `stack/ble/service/ota/ota_server.h`：默认最大固件 124 KB、默认新固件 boot 地址 `0x20000`。
- `vendor/ble_sample/flash_store_cfg.h`：event log、runtime、soc_kv、cold_kv、dvc1124_afe_cfg_kv 基址和扇区数，以及 512 KB OTA 地址门禁。
- `vendor/ble_sample/bms_cold_kv_store.h`：cold_kv 使用 4 个扇区。
- `vendor/ble_sample/dvc1124_config_store.*`：DVC1124 AFE 语义配置 KV schema、校验、应用和恢复。
- `vendor/common/ble_flash.h`：512 KB SMP、master pairing 和 MAC/校准地址。
- `vendor/ble_sample/tests_flash_quick_check.py`：布局互斥和 124 KB 上限的主机快速检查；后续应持续覆盖 AFE KV 区。
