# Flash 与持久化 — D013

## 1. TLSR8251 512 KB 当前布局

`flash_store_cfg.h` 当前定义：Firmware A/B 位于 `0x00000..0x3FFFF`；event log `0x40000..0x47FFF`；runtime `0x51000..0x52FFF`；SOC KV `0x53000..0x5AFFF`；cold KV `0x5B000..0x5EFFF`；另保留 AFE-config KV 地址 `0x5F000..0x62FFF`；`0x74000..0x7FFFF` 属于 SDK pairing/MAC/calibration 保护区。

## 2. 当前 D013 参数实际存储所有权

### 软件保护

`g_tParam.protect` 的 First/Second/Third/Recover/Filter 由 cold KV 保存，是 MCU 软件保护参数。

### AFE Hardware Protection V2

当前 `bms_afe_hw_profile.c` 通过 `bms_cold_kv_store_get_afe_hw_profile()` / `set_afe_hw_profile()` 保存独立 35-word AFE hardware profile。D013 当前 SH backend 的 OV/UV/OCD/OCC/SC/HW temperature requested 值与软件三级参数分开。

首次发现 AFE profile 为空时允许从旧软件参数建立 migration default；完成迁移后两套参数独立。

### `0x5F000..0x62FFF` 保留区

`flash_store_cfg.h` 仍保留 `AFE_CFG_KV` 4-sector 区，并带历史 DVC 注释。这是仓库继承布局；当前 SH3673510 Hardware Protection V2 profile实际存于 cold KV。不得把这个地址区写成 D013 当前硬件保护 profile 的真实所有者。

## 3. D013 特别注意

当前 `conf.h` 仍保留 D11/D011 产品身份/默认参数。这些历史值即使能从 cold KV 正常读写，也不代表它们已经成为 D013 产品签核参数。完成 D013 原理图/BOM和产品参数确认后，再做有版本的迁移，不能靠 factory reset 静默覆盖未知客户参数。

## 4. 一致性要求

- 软件保护写入不得改 AFE HW profile。
- AFE HW profile 写入按 validate -> persist -> apply -> requested/effective readback -> verify。
- 失败回滚；rollback失败报告 `CONFIG_INCONSISTENT`。
- schema/epoch改变必须有迁移或明确fallback。
- Flash写擦不在ISR执行，遵守Telink BLE Flash约束。

## 5. OTA / 发布

最终 BIN、multiple-boot 地址和保留区以当前 linker、`bms_tools`、`flash_store_cfg.h` 为准。每次布局变化执行 `flash_quick_check.py`、MAP/manifest/verify，并实测掉电/journal/OTA恢复。禁止全片擦除 `0x74000..0x7FFFF`。