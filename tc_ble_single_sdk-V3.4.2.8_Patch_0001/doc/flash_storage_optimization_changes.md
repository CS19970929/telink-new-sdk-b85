# Flash 存储优化实施说明

## 1. 本次实施目标

本次修改针对前一轮 Flash review 中最核心的三个问题落地：

1. hot `soc_kv` 擦写寿命偏短
2. cold `kv` 在频繁调参场景下余量不足
3. `runtime` 缺少标准化“恢复到初始时间”的 reset 接口

本次实施原则：

- 不占用系统保留区
- 不压缩现有 `event_log`、`btname`、`runtime` 功能分区
- 通过搬移 hot/cold KV 到空闲区扩容
- 升级后自动迁移旧布局数据
- 对 runtime 保持独立模块，不并入其他 KV

## 2. 已实施的布局调整

### 2.1 hot `soc_kv`

- sector 数：`2 -> 8`

新布局：

- 512K：`0x53000`
- 1M：`0xB0000`
- 2M：`0x1B0000`

旧布局迁移来源：

- 512K：`0x70000`
- 1M：`0xC3000`
- 2M：`0x1C3000`

### 2.2 cold `kv`

- sector 数：`2 -> 4`

新布局：

- 512K：`0x5B000`
- 1M：`0xB8000`
- 2M：`0x1B8000`

旧布局迁移来源：

- 512K：`0x72000`
- 1M：`0xC5000`
- 2M：`0x1C5000`

### 2.3 其他分区

本次没有缩减以下功能分区：

- `event_log`
- `btname`
- `runtime`

系统保留区也未被占用：

- 512K 仍低于 `0x74000`
- 1M 仍低于 `0xFC000`
- 2M 仍低于 `0x1FC000`

## 3. 已实施的寿命优化

### 3.1 hot `soc_kv` 写节流恢复

原逻辑已被恢复为：

- `cycle` 变化时立即保存
- 仅 `soc/dsg` 变化时，按 `5s` 节流保存

这保持了原始设计思路，行为变化最小，同时显著降低了热区写放大。

### 3.2 hot `soc_kv` 寿命变化

以当前事务大小估算：

- 单个 sector compact 后可继续追加约 `91` 次 hot 更新

优化前：

- `2 sector + 5s 节流`
- 单个 sector 大约每 `182` 次写入擦除一次

优化后：

- `8 sector + 5s 节流`
- 单个 sector 大约每 `728` 次写入擦除一次

按 `100k P/E` 量级 NOR 粗估：

- `1 次/5 秒`：约 `11.5 年`
- `1 次/分钟`：约 `138 年`

因此，hot `soc_kv` 已从“明显偏紧”降到“工程上可接受”。

### 3.3 cold `kv` 优化

本次对 cold `kv` 做了两层优化：

1. sector 数从 `2` 扩到 `4`
2. 对重复写入同值的场景直接跳过，不再重复落盘

这意味着：

- 高频调参场景下擦写寿命明显提升
- `modbus 0x06` 反复写同值时，不再产生无意义 Flash 写入

## 4. 旧数据迁移策略

### 4.1 hot `soc_kv`

升级到新布局后：

- 若新布局区仍是默认值
- 且旧布局区存在有效 `flash_kv32` sector header

则自动从旧布局读取：

- `soc`
- `dsg`
- `cycle`

迁移成功后，会擦除旧布局 sector，避免后续再次“复活”旧数据。

### 4.2 cold `kv`

升级到新布局后：

- 若新布局区仍是默认值
- 且旧布局区存在有效 `flash_kv32` sector header

则自动迁移：

- protect
- system
- control

迁移成功后同样擦除旧布局 sector，避免重复迁移。

## 5. 1M OTA 防呆

本次补充了 1M 场景下的布局保护：

- 继续保留 512K 对危险 OTA 组合的拒绝
- 新增 1M + `MULTI_BOOT_ADDR_0x80000` 的 fail-closed 拒绝

原因很直接：

- 当前自定义用户区没有整体搬到 `0xF0000` 以上
- 因此不能允许落入 SDK 参考之外的危险 OTA/lock 组合

这里的处理方式是“明确禁止危险组合”，而不是冒险去兼容。

## 6. runtime reset 方案

本次新增：

- `Runtime_FactoryReset()`

行为：

- 擦除 runtime 所在的两个 sector
- 把 runtime RAM 状态恢复为初始值
- 重新进入 factory 计时起点

同时增加了升级 reset 编排项：

- `FW_UPGRADE_RESET_RUNTIME_EPOCH`

这样如果以后需要在某次固件升级后强制把 runtime 恢复到初始时间，只需要修改 epoch，不需要额外写一次性脚本。

## 7. 为什么不把 runtime 合并进 KV

本次明确没有把 runtime 合并进 `soc_kv` 或 `cold_kv`，原因如下：

1. `runtime` 是周期性递增日志型数据，不是普通配置项
2. 当前独立双 sector 顺序日志模型已经足够轻量，掉电一致性也好
3. 如果合并进 hot `kv`，会把 runtime 写入和 `soc/dsg/cycle` 的热数据擦写绑在一起
4. 如果合并进 cold `kv`，会把运行时间和保护参数写入绑在一起，反而增加 cold 区磨损
5. 独立 runtime 模块更适合单独 reset，也更容易分析寿命

因此，本次选择是：

- 保留 runtime 独立模块
- 只补 reset 能力
- 不把它并入其他 KV

## 8. 验证结果

已执行静态检查脚本：

- `vendor/ble_sample/tests_flash_quick_check.py`

结果：

- `13/13` 通过

覆盖内容包括：

- 512K/1M/2M 布局不重叠
- 不与系统保留区冲突
- 512K 和 1M OTA 危险组合被显式拒绝
- hot `soc_kv` 已恢复节流
- runtime reset 接口存在
- 升级 reset 仅在成功后写入 epoch

## 9. 后续建议

本次优化后，当前优先级已经明显下降，但仍建议继续遵守以下约束：

1. 上位机参数写入尽量优先使用 `0x10` 批量写，而不是长时间逐项 `0x06`
2. 不要把 `event_log` 当高频 trace 使用
3. 若未来要支持新的 1M OTA 映射方案，先重新核对用户区与 FW lock 边界
4. 若业务要求蓝牙名断电原子性，再单独把 `btname` 改为双副本或 commit 型格式

