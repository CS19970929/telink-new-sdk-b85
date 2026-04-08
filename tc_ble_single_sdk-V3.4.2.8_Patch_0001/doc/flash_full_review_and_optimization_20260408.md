# 8251 / 512K Flash 模块完整复核与优化结论

日期：`2026-04-08`

## 1. 范围

本次针对当前项目所有业务 flash 模块做了重新梳理、代码复核、静态回归和文档收口，覆盖：

- `flash_store_cfg.h`
- `flash_store_safe.h`
- `flash_kv32.c/.h`
- `param.c/.h`
- `bms_cold_kv_store.c/.h`
- `btname_modbus.c/.h`
- `soc_kv_store.c/.h`
- `runtime.c/.h`
- `bms_event_log.c/.h`

## 2. 总结论

- 当前 `8251 / 512K` 项目没有发现正式 flash 分区冲突
- 当前 OTA 边界与业务分区边界没有重叠
- 本轮修复后，没有发现新的 P1/P0 级 flash 代码问题
- 不能把结论表述成“绝对无 bug”，因为还没有覆盖完整 TC32 编译和真机异常断电注入

当前最重要的状态是：

- `btname` 已并入 `cold_kv`
- 旧固件迁移逻辑已全部删除
- `soc_kv` 恢复为节流写入
- `flash_store_cfg` 已清掉失效迁移接口，当前地址来源更单纯

## 3. 当前正式分区

| 模块 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- |
| Firmware A | `0x00000 ~ 0x1EFFF` | `124K` | 当前运行固件 |
| OTA meta A | `0x1F000 ~ 0x1FFFF` | `4K` | SDK OTA 元数据 |
| Firmware B | `0x20000 ~ 0x3EFFF` | `124K` | OTA 下载槽位 |
| OTA meta B | `0x3F000 ~ 0x3FFFF` | `4K` | SDK OTA 元数据 |
| `event_log` | `0x40000 ~ 0x47FFF` | `32K` | 8 sector |
| `runtime` | `0x51000 ~ 0x52FFF` | `8K` | 2 sector |
| `soc_kv` | `0x53000 ~ 0x5AFFF` | `32K` | 8 sector |
| `cold_kv` | `0x5B000 ~ 0x5EFFF` | `16K` | 4 sector |
| SDK 保留区 | `0x74000 ~ 0x7FFFF` | `48K` | SMP / MAC / calibration |

历史空闲区：

- `0x50000 ~ 0x50FFF`：旧 `btname` 区，现已废弃
- `0x70000 ~ 0x73FFF`：旧 `soc_kv/cold_kv` 兼容区，现已废弃

## 4. 各模块 review 结论

### 4.1 flash_store_cfg

- 仅保留当前布局 getter 和必要的历史锚点常量
- 删除了旧布局迁移 getter
- 512K 场景继续 fail-closed 拒绝危险 OTA 组合
- 1M 路径继续保留 `0x80000` 危险组合拒绝

结论：当前配置层足够简单，误用面明显下降。

### 4.2 flash_store_safe

- 写前解锁、写后校验、按需恢复锁
- 继续保证 stack 使用 flash 时不被应用侧误重锁

结论：没有发现新增问题。

### 4.3 flash_kv32

- 仍然是当前项目最核心的通用持久化基础层
- 事务格式包含 CRC 和 commit 末写
- `soc_kv` / `cold_kv` 都依赖它

结论：当前没有发现明显实现缺陷，异常断电下通常只会丢最后一次未完成提交。

### 4.4 cold_kv

- 当前承载保护参数、系统参数、升级 epoch、蓝牙名 suffix
- 相同值写入会直接跳过
- 4 sector 对当前低频配置足够

结论：寿命和稳定性都满足正常使用。

### 4.5 btname

- 已不再单独擦写 sector
- 通过 `cold_kv` 事务写入
- 掉电风险明显小于旧方案

结论：旧方案最薄弱的原子性问题已经消掉。

### 4.6 soc_kv

- 当前恢复为 `cycle` 变化立即写，`soc/dsg` 最多每 `5s` 落盘一次
- 8 sector 布局满足当前热数据寿命要求

结论：寿命瓶颈已经从“结构性风险”降回“策略不要再被改坏”的级别。

### 4.7 runtime

- 使用 2 sector append-log
- 每分钟落盘一次
- 提供 `Runtime_FactoryReset()`

结论：从寿命上安全；当前更像产品策略模块，而不是 flash 风险模块。

### 4.8 event_log

- 使用 snapshot + CRC + commit
- 8 sector 日志池
- 工程中保留测试入口 `test_log_app/test_log_balance_first`，目前确实被 `app.c` 调用，不属于死代码

结论：当前实现合理，没有必要再合并到其他存储池。

### 4.9 param

- 现在只通过 `cold_kv` 读写保护参数
- 升级 reset epoch 只有在对应默认值应用成功后才落盘

结论：当前控制路径清晰，没有再依赖旧 `PARAM_ADDR` 迁移。

## 5. 本轮已做优化

- 恢复 `soc_kv` 节流写入，修掉工作区里重新引入的高频刷写问题
- 清理 `flash_store_cfg.h` 中已失效的迁移 getter 和过期地址残留
- 清理 `soc_kv_store.h`、`bms_event_log.h`、`bms_cold_kv_store.h` 里的死宏
- 加强 `tests_flash_quick_check.py`

脚本新增收口点：

- 明确检查 `flash_store_cfg` 不再导出迁移 helper
- 明确检查 `soc_kv` 的节流逻辑存在于有效代码，而不是只出现在注释里

## 6. 寿命结论

按 `100k P/E` 保守估算：

- `soc_kv`：满足使用，关键前提是保留当前 5 秒节流
- `cold_kv`：满足使用
- `runtime`：满足使用
- `event_log`：满足使用
- `btname`：已并入 `cold_kv`，满足使用

当前没有发现“因为分区太小导致必须立刻扩区”的模块。

## 7. 剩余风险与边界

当前剩余的风险不是分区冲突，而是策略与验证覆盖：

- `FW_UPGRADE_RESET_RUNTIME_EPOCH` 当前配置为 `1u`，下一次升级会重置 runtime
- `runtime` 当前 `1 min` 保存周期是可接受的，但属于偏积极配置
- 上位机如果长期通过 `0x06` 单寄存器逐项改保护参数，会带来额外写放大
- 还没有完成完整 TC32 编译和真机异常断电注入

## 8. 验证

已执行：

- `tests_flash_quick_check.py`

结果：

- `21/21` 通过

未执行：

- TC32 完整编译
- 真机 power-cut 注入
- 长时寿命压力测试
