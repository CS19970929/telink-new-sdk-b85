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
| cold_kv | `0x5B000..0x5EFFF` | 16 KB | 4 个 4 KB 扇区；保护参数/系统参数/控制项/BT name |
| 未分配区 | `0x5F000..0x73FFF` | 84 KB | 当前工程未分配；不得覆盖 SDK 保留区 |
| SMP pairing | 从 `0x74000` 起 | SDK 定义 | `FLASH_ADR_SMP_PAIRING_512K_FLASH` |
| master pairing | 从 `0x78000` 起 | SDK 定义 | `FLASH_ADR_MASTER_PAIRING_512K`；同时存在历史参数锚点，需保持兼容审查 |
| MAC / 校准 | 从 `0x7F000` 起 | 4 KB 顶部区域 | `CFG_ADR_MAC_512K_FLASH`；禁止擦除 |

## 强制约束

- 512 KB + OTA 配置只接受 multiple boot `0x20000`；`flash_store_cfg_layout_supported()` 会拒绝其它地址。
- Firmware BIN 必须小于等于 124 KB；当前主机资格构建为 91,076 字节。
- 烧录应用固件只使用 `0x00000` 起始地址。
- 禁止全片擦除，禁止擦除 `0x74000..0x7FFFF`。
- `0x40000` 以后不是 OTA 镜像空间；把 OTA boot 改为 `0x40000` 会直接覆盖 event_log。
- 未分配区只是当前代码没有使用，不代表未来可无审查占用。

## 源码取证位置

- `stack/ble/service/ota/ota_server.h`：默认最大固件 124 KB、默认新固件 boot 地址 `0x20000`。
- `vendor/ble_sample/flash_store_cfg.h`：event log、runtime、soc_kv、cold_kv 基址和扇区数，以及 512 KB OTA 地址门禁。
- `vendor/ble_sample/bms_cold_kv_store.h`：cold_kv 使用 4 个扇区。
- `vendor/common/ble_flash.h`：512 KB SMP、master pairing 和 MAC/校准地址。
- `vendor/ble_sample/tests_flash_quick_check.py`：布局互斥和 124 KB 上限的主机快速检查。
