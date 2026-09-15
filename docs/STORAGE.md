# Flash 与持久化 — D011

## 1. TLSR8251 512 KB 当前布局

`flash_store_cfg.h` 当前定义：Firmware A/B 位于 `0x00000..0x3FFFF`；event log `0x40000..0x47FFF`；runtime `0x51000..0x52FFF`；SOC KV `0x53000..0x5AFFF`；cold KV `0x5B000..0x5EFFF`；另保留 AFE-config KV 地址 `0x5F000..0x62FFF`；`0x74000..0x7FFFF` 属于 SDK pairing/MAC/calibration 保护区。

## 2. 当前 D011 参数实际存储所有权

### 软件保护

`g_tParam.protect` 的 First/Second/Third/Recover/Filter 由 cold KV 保存，是 MCU 软件保护参数。

### AFE Hardware Protection V2

当前 `bms_afe_hw_profile.c` 通过 `bms_cold_kv_store_get_afe_hw_profile()` / `set_afe_hw_profile()` 持久化独立 35-word AFE hardware profile。因此 D011 的 OV/UV/OCD/OCC/SC/HW temperature requested 值 **不应描述为长期从 `g_tParam.protect` 直接驱动**。

只有第一次发现 AFE profile 为空（schema/model 都为0）时，固件从旧软件参数建立一次 migration default；随后两套参数独立。

### `0x5F000..0x62FFF` 保留区

`flash_store_cfg.h` 仍保留名为 `AFE_CFG_KV` 的 4-sector 区，并带有历史 DVC 注释。这是仓库继承布局；当前 SH3673510 Hardware Protection V2 profile实际存于 cold KV。不要把该保留区误写成 D011 SH profile 的当前所有者。若未来复用该区，必须先审核兼容性和迁移策略。

## 3. 其它 Store

- `soc_kv_store.*`：真实SOC/累计放电量/cycle。
- `runtime.*`：累计运行时间。
- `bms_event_log.*`：事件日志。
- `bms_cold_kv_store.*`：软件保护/system/SOC产品身份/AFE HW V2 profile等低频参数。

## 4. 一致性要求

- 软件保护写入不得改 AFE HW profile。
- AFE HW profile 采用完整事务：validate -> persist -> apply SH3673510 -> requested/effective readback -> verify。
- apply/verify失败必须回滚；rollback失败报告 `CONFIG_INCONSISTENT`。
- schema/epoch 变化必须明确迁移，禁止拼接不同代记录。
- Flash写擦不在ISR执行；遵守Telink BLE Flash约束。

## 5. OTA / 发布

最终 BIN、multiple-boot 地址和保留区以当前 linker、`bms_tools`、`flash_store_cfg.h` 为准。每次布局变化必须跑 `flash_quick_check.py`、MAP/manifest/verify，并实测掉电/journal/OTA中断恢复。禁止全片擦除 `0x74000..0x7FFFF`。