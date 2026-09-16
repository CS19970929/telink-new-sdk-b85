# Flash 与持久化 — D008 / Storage V1

## 1. 目标与边界

Storage V1 把 BMS 持久化固定为四个语义域：`Config`、`State`、`Factory`、`Event`。业务代码不拥有物理 Flash 地址；MCU 相关的 read/program/erase、Flash lock 和 BLE session 约束全部收口到 `bms_storage_platform_telink.c`。

通用 `storage_record.c/.h` 只依赖 `stdint.h` 与 `storage_port.h`，不 include Telink/STM32 SDK，可移植到其他 MCU 内部 Flash、SPI Flash 或其他块设备。

## 2. TLSR8251 512 KB 布局

| 区域 | 地址 | 大小 | 所有者 |
|---|---:|---:|---|
| Firmware A | `0x00000..0x1EFFF` | 124 KB | 当前镜像 |
| OTA A meta | `0x1F000..0x1FFFF` | 4 KB | SDK OTA |
| Firmware B | `0x20000..0x3EFFF` | 124 KB | OTA 镜像 |
| OTA B meta | `0x3F000..0x3FFFF` | 4 KB | SDK OTA |
| Event | `0x40000..0x47FFF` | 32 KB | Event ring snapshot journal |
| 保留 | `0x48000..0x52FFF` | 44 KB | 未分配 |
| State | `0x53000..0x5AFFF` | 32 KB | SOC / DSG / cycle / learning / runtime |
| Config | `0x5B000..0x5EFFF` | 16 KB | software protect / system / AFE profile / BT name / control |
| Factory | `0x5F000..0x60FFF` | 8 KB | 预留给 SN/校准/生产身份，当前无 writer |
| 保留 | `0x61000..0x73FFF` | 76 KB | 未分配 |
| SDK/pairing | `0x74000..0x7EFFF` | SDK 定义 | BLE SDK |
| MAC/calibration | `0x7F000..0x7FFFF` | 4 KB | 芯片身份/校准 |

1 MB / 2 MB 仍使用相同逻辑域，地址由 `flash_store_cfg.h` 唯一定义，并由 `tests/flash_quick_check.py` 检查与 SDK 保留区的冲突。

## 3. 软件分层

```text
BMS business
  +-- Config
  +-- State
  +-- Factory (reserved)
  +-- Event
          |
     storage_record
          |
      storage_port
          |
 bms_storage_platform_telink
          |
    Telink Flash driver
```

`storage_record` 使用固定 little-endian 元数据、CRC32、sequence 和 commit-last；每个持久化域至少跨两个 erase sector。切换 sector 时先在新 sector 形成完整有效记录，因此任意写入阶段掉电都保留上一份完整记录。

## 4. 数据所有权

**Config** 表示“设备应该怎样工作”：当前包含 `g_tParam.protect`、system/SOC identity、独立 AFE Hardware Protection requested profile、reset/control epoch 与蓝牙名称后缀。Flash payload 使用显式 little-endian encode/decode，不直接把 C struct 原样 memcpy 到 Flash。D008 的 DVC1124 固定 operating/board 配置仍由编译期配置拥有，只有语义化 requested protection profile 进入 Config。

**State** 表示“设备已经运行到什么状态”：统一保存 SOC、DSG 累计量、cycle、learned capacity/flag 和 aging runtime minutes。`runtime.c` 不再维护第二套 Flash journal/CRC；SOC/DSG/cycle 仍保持值变化才保存的现有语义。

**Factory** 已拥有独立物理区域，但当前不创建无实际需求的业务 writer。后续 SN、生产日期、板级校准等进入该域，Factory Reset 不得清除此域。

**Event** 保留 100 条逻辑 ring，物理持久化复用同一 Record Journal。只有快照保存成功后才更新 event latch。

## 5. 开发期格式策略

当前三个项目仍在开发，因此不迁移旧 Flash 内容。旧 `flash_kv32`、旧 runtime journal、SOC KV 和 cold KV 不再解释；Storage V1 使用新的 magic/schema，读取不到 V1 数据时加载编译期默认值，首次正常保存时建立 V1 记录。

Firmware version 与 Storage/Config schema 分离。正式出货后若只需修改少量客户参数，应使用独立 config patch 机制，而不是用 firmware version 重置整套参数。

## 6. 验证约束

- Storage Core 不依赖 MCU SDK、RTOS 或动态内存。
- ISR 禁止 erase/program。
- BMS Flash 地址只由 `flash_store_cfg.h` 拥有。
- 业务模块禁止直接调用 `flash_read_page` / `flash_write_page` / `flash_erase_sector`。
- 不重新引入业务专用 KV engine。
- Host test 用 RAM 模拟 Flash `1 -> 0` program 规则，并覆盖普通写入、commit 中断和 sector rotation 中断恢复。
- 每次 layout/schema 修改必须运行 `tests/flash_quick_check.py`，TC32 发布继续执行 clean rebuild、firmware check、MAP/manifest/verify。
- 禁止擦除 SDK/pairing/MAC/calibration 保留区。
