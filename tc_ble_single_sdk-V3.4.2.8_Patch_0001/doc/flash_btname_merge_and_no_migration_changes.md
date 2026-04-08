# btname 并入 cold_kv 与迁移逻辑清理说明

## 1. 变更目标

本次变更做两件事：

- 将 `btname` 从独立单 sector flash 存储改为并入 `cold_kv`
- 删除旧布局迁移逻辑，统一采用“旧固件直刷后按默认值重建”的策略

## 2. 代码变更摘要

### 2.1 btname

- `btname_modbus.c/.h` 已重构。
- `btname` 不再直接擦写单独 flash sector。
- 蓝牙名 suffix 改为通过 `bms_cold_kv_store_get_bt_name_suffix()` / `bms_cold_kv_store_set_bt_name_suffix()` 读写。
- 原先的旧 sector 记录格式、checksum、擦写校验逻辑已删除。

### 2.2 cold_kv

- 在 `bms_cold_kv_store.c/.h` 中新增 `btname` 对应的 key 组。
- `btname suffix` 以 6 个 `u32` slot 写入 `cold_kv`。
- `btname` 写入与 `cold_kv` 其余内容一样，走同一套 `flash_kv32_write_pairs()` 事务提交流程。

### 2.3 迁移逻辑

以下迁移逻辑已删除：

- `soc_kv` 历史 2-sector 布局迁移
- `cold_kv` 历史 2-sector 布局迁移
- `runtime` 旧单值格式导入
- `PARAM_ADDR` 旧参数读取兼容
- `btname` 旧单 sector 读取兼容

## 3. 为什么这样改

### 3.1 稳定性更好

旧 `btname` 路径是典型的“先擦 sector，再写新值”，异常断电时最容易出现该项数据丢失。
并入 `cold_kv` 后，蓝牙名落盘改为 KV 事务提交模式，电源中断时通常最多丢最后一次未完成写入，不容易把上一次已提交状态整体写坏。

### 3.2 代码更简单

删除迁移逻辑后，不再需要：

- 判断当前区是否为空再决定是否导入
- 检查 legacy header / 校验旧格式
- 迁移成功后再擦旧区
- 维护一组历史地址和兼容分支

模块初始化路径因此更短，问题定位也更直接。

### 3.3 分区更清晰

`btname` 不再占用正式独立分区，当前正式 flash 模块只保留：

- `event_log`
- `runtime`
- `soc_kv`
- `cold_kv`

其中 `cold_kv` 统一承载低频配置类数据。

## 4. 行为变化

本次改动引入了明确行为变化：

- 旧固件直接刷到当前固件后，历史参数不会迁移。
- 自定义蓝牙名不会迁移，未重新设置时将使用默认值。
- 旧 `runtime` 不会导入，运行时间从当前固件初始状态开始。
- 旧 `soc_kv` 不会导入，SOC / cycle 等历史值按当前固件默认值重建。

这是有意为之，已经按“无需迁移历史数据”的产品策略收口。

## 5. 风险变化

### 已降低的风险

- `btname` 单 sector 擦写导致的掉电原子性风险下降。
- 迁移代码误判、旧区复活、旧新布局交叉影响的风险下降。
- 初始化链路复杂度下降。

### 仍然存在的风险

- 本次没有调整 `soc_kv` 的刷写策略本身，hot path 寿命问题仍应按独立结论继续看待。
- 本次没有做 TC32 完整编译验证，只做了静态检查。
- 本次没有做真机断电注入测试。

## 6. 验证

已执行：

```powershell
py -3 tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/tests_flash_quick_check.py
```

结果：

- `20/20` 通过

静态检查已覆盖：

- 当前 512K / 1M / 2M 布局不重叠
- 512K OTA 只能使用 `0x20000`
- `btname` legacy 区域不和当前 active 区冲突
- `btname` 只走 `cold_kv`
- `param/runtime/soc_kv/cold_kv` 已不再引用旧迁移路径

## 7. 结论

这次收口后，flash 模块职责更清晰：

- 高频状态留在 `soc_kv`
- 低频配置统一留在 `cold_kv`
- `btname` 作为低频配置的一部分，不再独立占区
- 旧兼容迁移路径全部去掉

如果后续还要继续简化，下一步建议优先考虑：

- 是否回收 `0x50000` 和 `0x70000 ~ 0x73FFF` 这些已空闲历史区域
- 是否进一步统一 `runtime/event_log` 的底层 append-log 公共实现
