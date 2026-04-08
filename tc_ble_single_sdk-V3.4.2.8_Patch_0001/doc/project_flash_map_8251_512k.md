# 8251 / 512K 当前 Flash Map（btname 已并入 cold_kv）

## 1. 适用范围

- MCU：8251 / 825x 系列
- Flash：512K
- OTA：保持默认双分区，boot 地址必须为 `0x20000`
- 当前策略：旧固件直接刷写新固件后，参数、运行时间、蓝牙名、SOC 历史值均按当前固件默认值重建，不做迁移

## 2. 当前正式使用分区

| 模块 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- |
| Firmware A | `0x00000 ~ 0x1EFFF` | `124K` | 当前运行固件槽位 |
| OTA meta A | `0x1F000 ~ 0x1FFFF` | `4K` | SDK OTA 元数据 |
| Firmware B | `0x20000 ~ 0x3EFFF` | `124K` | OTA 下载槽位 |
| OTA meta B | `0x3F000 ~ 0x3FFFF` | `4K` | SDK OTA 元数据 |
| `event_log` | `0x40000 ~ 0x47FFF` | `32K` | 8 个 sector |
| `runtime` | `0x51000 ~ 0x52FFF` | `8K` | 2 个 sector |
| `soc_kv` | `0x53000 ~ 0x5AFFF` | `32K` | 8 个 sector |
| `cold_kv` | `0x5B000 ~ 0x5EFFF` | `16K` | 4 个 sector，现已同时承载保护参数、系统参数、控制 epoch、蓝牙名 suffix |
| SDK 保留区 | `0x74000 ~ 0x7FFFF` | `48K` | SMP / MAC / calibration / pairing |

## 3. 当前不再作为正式分区使用的区域

| 区域 | 地址范围 | 大小 | 当前状态 |
| --- | --- | --- | --- |
| legacy btname 单 sector | `0x50000 ~ 0x50FFF` | `4K` | 不再使用，不再作为正式存储区 |
| 中间空闲区 | `0x48000 ~ 0x4FFFF` | `32K` | 当前未使用 |
| 中间空闲区 | `0x5F000 ~ 0x6FFFF` | `68K` | 当前未使用 |
| 历史 kv 兼容区 | `0x70000 ~ 0x73FFF` | `16K` | 不再使用，不再参与迁移 |

## 4. btname 当前存储方式

- `btname` 不再单独占用一个 flash sector。
- `btname suffix` 已并入 `cold_kv`，以多 key 事务方式写入。
- 优点：
  - 不再是“先擦后写”的单 sector 模式。
  - 写入走 `flash_kv32` 的事务提交路径，异常断电时更不容易把上一次已提交数据破坏掉。
  - 后续蓝牙名与其他低频配置共用一套 CRC / commit / wear leveling 机制。
- 影响：
  - `bms_cold_kv_store_factory_reset()` 或 `flash_kv32_format(cold_kv)` 会把蓝牙名一起恢复为默认值。

## 5. OTA 边界

- 512K 布局下，OTA boot 地址必须保持 `0x20000`。
- 如果改成 `0x40000`，会直接撞上 `event_log` 分区。
- 当前代码仍保留 fail-closed 保护，危险 OTA 组合会被拒绝。

## 6. 兼容与迁移策略

当前版本明确采用“不迁移”策略：

- 旧 `PARAM_ADDR` 不再作为读取回退源。
- 旧 `runtime` 单值存储不再导入。
- 旧 `soc_kv/cold_kv` 历史布局不再导入。
- 旧 `btname` 单 sector 不再导入。

这意味着：

- 旧固件直接刷到当前固件后，保护参数、系统参数、SOC 历史值、runtime、蓝牙名都按当前固件默认值重新建立。
- 好处是代码路径更短、状态机更清晰、异常边界更少。
- 代价是升级后不保留历史配置和历史运行状态。

## 7. 当前结论

- 当前 `8251 / 512K` 正式分区之间没有冲突。
- `btname` 已从独立分区并入 `cold_kv`，正式分区更简单。
- 当前项目已经不再依赖迁移兼容区运行；`0x50000` 和 `0x70000 ~ 0x73FFF` 现在都只是空闲历史区域。
- `flash_store_cfg.h` 已移除旧布局迁移 getter，当前地址入口只保留正式布局 getter 和必要历史锚点常量。
- `tests_flash_quick_check.py` 当前静态检查结果为 `21/21` 通过。
- 如果后续需要继续收缩分区或扩某个模块，可以优先评估这些空闲区，但本次改动没有移动其他正式分区地址。
