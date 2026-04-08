# ble_sample Flash Review 与风险评估

## 1. 评审范围

本次 review 目标是评估 `vendor/ble_sample` 下各类 Flash 持久化模块的以下问题：

- KV 分区是否偏小
- Flash 擦写寿命是否存在明显风险，是否需要扩容
- 极端异常场景，尤其是异常断电时，是否会导致数据损坏或严重系统问题
- 当前 Flash 分区是否合理
- 当前分区和写策略是否会影响 OTA 升级

本次 review 重点覆盖以下模块：

- `tc_ble_single_sdk/vendor/ble_sample/flash_store_cfg.h`
- `tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c`
- `tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c`
- `tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.c`
- `tc_ble_single_sdk/vendor/ble_sample/runtime.c`
- `tc_ble_single_sdk/vendor/ble_sample/bms_event_log.c`
- `tc_ble_single_sdk/vendor/ble_sample/btname_modbus.c`
- `tc_ble_single_sdk/vendor/ble_sample/param.c`
- `tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c`
- `tc_ble_single_sdk/vendor/ble_sample/app.c`
- `tc_ble_single_sdk/vendor/common/ble_flash.h`
- `tc_ble_single_sdk/vendor/common/flash_prot.h`

## 2. 结论摘要

结论先行：

1. 当前最主要的 Flash 风险不是 `runtime`，也不是 `event_log`，而是 hot `soc_kv` 的写策略。
2. `soc_kv` 当前采用 `2 sector + 值一变化就立刻写` 的组合，寿命风险明显偏高，建议优先整改。
3. `cold_kv` 的 `2 sector` 对低频参数保存可以接受，但对频繁调参场景偏小。
4. `flash_kv32`、`runtime`、`event_log` 的异常断电一致性设计总体较好，通常最多丢最后一次未提交记录，不会轻易破坏上一次已提交数据。
5. 蓝牙名存储仍然是单 sector 擦除后重写，不具备原子提交能力，极端断电下可能丢失蓝牙名。
6. 1M Flash 布局虽然当前未与系统保留区直接重叠，但缺少和 512K 一样的 OTA 边界防呆，存在后续配置复用时踩踏 OTA 区的风险。

## 3. 当前分区布局

`flash_store_cfg.h` 当前定义的多容量布局如下。

### 3.1 512K

- `btname`：`0x50000`
- `runtime`：`0x51000`
- `soc_kv`：`0x70000`
- `cold_kv`：`0x72000`
- `event_log`：`0x40000`

### 3.2 1M

- `btname`：`0xC0000`
- `runtime`：`0xC1000`
- `soc_kv`：`0xC3000`
- `cold_kv`：`0xC5000`
- `event_log`：`0xC7000`

### 3.3 2M

- `btname`：`0x1C0000`
- `runtime`：`0x1C1000`
- `soc_kv`：`0x1C3000`
- `cold_kv`：`0x1C5000`
- `event_log`：`0x1C7000`

系统保留区定义见 `vendor/common/ble_flash.h`：

- 512K：SMP 从 `0x74000` 起，MAC/校准在 `0x76000/0x77000`
- 1M：SMP 从 `0xFC000` 起，MAC/校准在 `0xFF000/0xFE000`
- 2M：SMP 从 `0x1FC000` 起，MAC/校准在 `0x1FF000/0x1FE000`

从静态地址范围看，当前 512K、1M、2M 的应用自定义区和系统保留区没有直接重叠。本次还执行了 `tests_flash_quick_check.py`，静态检查结果为 `10/10` 通过。

## 4. 最高优先级问题

### 4.1 P1: hot `soc_kv` 写放大导致寿命风险过高

问题位置：

- `tc_ble_single_sdk/vendor/ble_sample/app.c`
- `tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c`
- `tc_ble_single_sdk/vendor/ble_sample/flash_store_cfg.h`

关键现状：

- 主循环每圈都会调用 `soc_kv_store_update_and_log_if_changed(...)`
- 原有 5 秒节流逻辑已被注释掉
- 当前实现变成 `SOC/DSG/cycle` 任一变化就整包写入
- hot `soc_kv` 仅有 `2` 个 sector

具体表现：

- `soc_kv_store_write_all()` 每次都会写 3 个 key
- 事务大小约为 `44B`
- 单个 4KB sector 去掉头部后可用空间约 `4064B`
- compact 后一个 active sector 先写入 1 个 snapshot，随后大约还能追加 `91` 次更新

按此估算，若 `soc_kv` 写入频率不同，寿命大致如下：

- `1 次/分钟`：约 `5767` 次擦除/年/sector，100k P/E 量级下可接受
- `1 次/5 秒`：约 `190` 次擦除/天/sector，100k P/E 量级下约 `1.4 年`
- `1 次/秒`：约 `949` 次擦除/天/sector，100k P/E 量级下约 `105 天`

结论：

- hot `soc_kv` 当前不是“略紧”，而是“在高变化场景下明显偏小”
- 风险本质不只在 sector 数量，更在于写策略过于激进
- 如果实际运行时 SOC 或 DSG 在短周期内频繁变化，Flash 会很快进入高擦写负载

建议优先级：最高

### 4.2 P1: 1M 分区缺少 OTA 边界防呆

问题位置：

- `tc_ble_single_sdk/vendor/ble_sample/flash_store_cfg.h`
- `tc_ble_single_sdk/vendor/ble_sample/app.c`
- `tc_ble_single_sdk/vendor/common/flash_prot.h`

关键现状：

- `flash_store_cfg_layout_supported()` 仅对 `512K + OTA boot != 0x20000` 做了拒绝
- 1M 布局仍把用户自定义数据放在 `0xC0000 ~ 0xCEFFF`
- OTA 相关 sample 代码中存在 `MULTI_BOOT_ADDR_0x80000`
- `flash_prot.h` 对 `FLASH_LOCK_FW_LOW_1M` 的注释明确说明：用户数据应位于 `0xF0000` 以上

结论：

- 当前 1M 布局虽然没有和系统保留区重叠，但与 SDK 参考的 1M 固件保护边界不一致
- 如果后续板型、公共配置或移植版本启用了 `1M + OTA boot=0x80000`
- 则用户数据区可能落在 OTA/FW 保护边界内，存在踩踏或升级异常风险

这类风险的特点是：

- 当前版本不一定马上触发
- 但一旦配置复用或切换 OTA 模式，问题会直接暴露
- 属于应尽早做 fail-closed 防呆的设计缺口

建议优先级：最高

## 5. 其他风险评估

### 5.1 `cold_kv` 是否偏小

`bms_cold_kv_store.h` 当前定义：

- `BMS_COLD_KV_SECTORS = 2`

该模块保存的数据主要包括：

- protect 参数约 `65` 个 key
- system 参数 `8` 个 key
- control 参数 `4` 个 key

整包 snapshot 大小约为：

- `636B`

其中最关键的是 protect 参数保存路径：

- `modbus 0x06` 单寄存器写会触发 `SaveParam()`
- `SaveParam()` 最终调用 `bms_cold_kv_store_set_protect()`
- `bms_cold_kv_store_set_protect()` 会整包写入全部 protect key

整次 protect 保存事务大小约：

- `540B`

按当前格式估算：

- compact 后 active sector 还能承受大约 `6` 次完整 protect 保存

结论：

- 如果 protect 参数修改是低频行为，例如出厂配置一次、现场偶发改参，则 `2 sector` 基本可接受
- 如果存在逐项调参、批量反复试参、产线脚本高频单寄存器写入，则 `2 sector` 偏小

风险等级：中

建议：

- 上位机优先使用 `0x10` 批量写，而不是 `0x06` 单项写
- 如果项目存在高频改参需求，建议把 cold `kv` 扩到 `4 sector`

### 5.2 `runtime` 寿命与掉电风险

`runtime` 当前采用双 sector 顺序日志结构，记录格式包含：

- `seq`
- `runtime_min`
- `flag`
- `crc`
- `commit`

其安全特征：

- 先写 payload
- 最后写 `commit`
- 启动时依赖 `crc + commit` 判定记录有效性

这意味着：

- 异常断电通常只会导致最后一条未完成记录失效
- 不会轻易破坏之前已经提交的运行时间记录

当前写频率也不高：

- 达到 `FACTORY_TIME_LIMIT_MIN`
- 或距离上次保存超过 `10` 分钟

此外，当前 `runtime.h` 实际定义：

- `FACTORY_TIME_LIMIT_MIN = 30`

这比注释所表达的“7 天”更保守，实际写入次数更少。因此 `runtime` 目前不是寿命瓶颈。

结论：

- 掉电一致性较好
- 寿命压力较低
- 不是当前需要优先扩容的模块

### 5.3 `event_log` 寿命与掉电风险

`bms_event_log` 当前采用多 sector snapshot 轮转：

- 每个 snapshot `224B`
- 每个 4KB sector 可放约 `18` 个 snapshot
- 当前一共 `8 sector`
- 全区总容量约 `144` 个 snapshot

掉电一致性设计：

- snapshot 包含 `magic/version/seq/write_pos/records/crc/commit`
- `commit` 最后写入
- 写失败时 RAM 状态会回滚，不会直接把 latch 状态提前置位

同时事件记录策略也做了控制：

- 大多数故障位是 0 -> 1 边沿触发
- `cbc_err` 走变化触发

结论：

- 在正常故障事件频率下，`event_log` 容量和寿命基本足够
- 异常断电通常只会丢最后一条未完成日志
- 不容易破坏历史已提交日志

风险主要出现在：

- 故障位抖动频繁
- 调试阶段把事件日志当作高频 trace 使用

总体风险等级：中低

### 5.4 `btname` 的断电原子性不足

`btname_modbus.c` 当前流程仍然是：

1. 擦除整个 sector
2. 重写完整 `btname_rec_t`
3. 读回校验 `magic/len/checksum`

问题在于：

- 没有双副本
- 没有 journal
- 没有 `commit` 标志分阶段提交

因此在极端断电情况下，最坏结果是：

- 蓝牙名称写入失败
- 存储内容失效
- 下次启动回退到默认名

结论：

- 这是一个“可恢复但不原子”的问题
- 影响范围相对局部
- 一般不会造成 OTA、保护参数或系统运行时数据的严重问题

风险等级：中低

## 6. 异常断电一致性分析

### 6.1 `flash_kv32` 框架

`flash_kv32` 是当前 hot/cold KV 的基础实现，其关键特征是：

- sector header 有 `prepare_mark` 和 `active_mark`
- transaction 有头、payload、CRC 和最终 `commit_magic`
- compact 流程是“新 sector 准备 -> 写 snapshot -> 标 active -> 切换 active -> 擦旧 sector”

因此对 `soc_kv` 和 `cold_kv` 来说，异常断电的一般后果是：

- 最后一笔未提交事务丢失
- 或 compact 中途未完成时继续使用旧 active sector

正常情况下不会出现：

- 上一版已提交 KV 整体失效
- 因半写入事务把历史数据全部破坏

### 6.2 `runtime`

`runtime` 的记录同样采用：

- `crc`
- `commit` 末写
- 双 sector 滚动

因此掉电一致性结论与 `flash_kv32` 类似，风险较低。

### 6.3 `event_log`

`event_log` 写 snapshot 时也是：

- 先写 `commit` 之前的内容
- 最后单独写 `commit`

因此极端断电一般只影响最后一个 slot，不会把整个历史日志链破坏。

### 6.4 `btname`

`btname` 是当前唯一明显不具备原子提交语义的路径，掉电时可能丢值。

## 7. 是否影响 OTA 升级

### 7.1 当前 512K 场景

当前实现已对 512K 场景做了显式限制：

- 若 `OTA boot != 0x20000`，布局 getter 直接返回不支持

这意味着：

- 512K 下对危险 OTA 组合已经做了 fail-closed
- 对 OTA 的保护意识是存在的

### 7.2 当前 1M 场景

当前 1M 场景的问题不是“已经确认重叠”，而是：

- 自定义用户区位置不满足 SDK 对 1M FW lock 参考边界的要求
- 代码层又没有像 512K 那样主动拒绝危险组合

因此从工程风险看：

- 现在的 1M 分区方案不够稳健
- 未来切 OTA 模式、移植别的板型或复用 sample 配置时，存在影响 OTA 的潜在风险

### 7.3 当前 2M 场景

本次没有发现 2M 布局与系统保留区的直接冲突问题，但如果未来也采用更严格的 firmware lock/OTA 映射策略，仍需按同样原则重新核对边界。

## 8. 是否需要加大分区

综合建议如下。

### 8.1 hot `soc_kv`

建议：需要加大

理由：

- 当前 `2 sector` 对高变化值保存过小
- 更关键的是写策略过激，导致小分区寿命问题被放大

建议目标：

- 至少 `4 sector`
- 更稳妥是 `8 sector`

但需要强调：

- 单纯扩大分区不能替代写策略整改
- 如果仍然保持“值一变就写”，即使扩容也只是延后问题出现时间

### 8.2 cold `kv`

建议：按业务场景决定

- 若参数保存是低频行为，可暂不扩
- 若现场调参频繁，建议扩到 `4 sector`

### 8.3 `runtime`

建议：暂不需要加大

### 8.4 `event_log`

建议：暂不需要加大

### 8.5 `btname`

建议：不需要加大分区，但若要求断电原子性，需要改存储策略

## 9. 推荐整改顺序

建议按以下优先级推进：

1. 恢复或重做 `soc_kv` 的节流/批量保存策略
2. 扩大 hot `soc_kv` 分区，建议至少 4 sector
3. 给 1M 布局增加 OTA 风险组合的 fail-closed 校验
4. 规范上位机参数写入方式，优先使用 `0x10` 批量写
5. 如确实存在高频调参场景，再扩大 cold `kv`
6. 如业务要求蓝牙名掉电不丢，再把 `btname` 改成双副本或 journal 方案

## 10. 建议的具体整改方向

### 10.1 `soc_kv`

建议至少采取以下一项，最好组合使用：

- 恢复 5 秒节流
- 改为“只在周期性时机或状态稳定时落盘”
- 对 `soc/dsg/cycle` 做不同级别的保存策略
- 扩容到 `4` 或 `8 sector`

### 10.2 `cold_kv`

- `0x06` 单寄存器写场景减少立即落盘次数
- 推动上位机使用 `0x10` 批量写
- 频繁改参项目扩容到 `4 sector`

### 10.3 1M OTA 边界

- 在 `flash_store_cfg_layout_supported()` 中增加 1M 危险 OTA 组合校验
- 明确规定 1M 用户数据区是否允许位于 `0xF0000` 以下
- 若不允许，则重排 1M 分区地址

### 10.4 `btname`

若要求断电原子性，建议改成：

- 双副本 A/B
- append-log
- 或 `payload + commit` 的轻量日志格式

## 11. 寿命估算口径说明

本次寿命估算采用的口径是：

- 以常见 SPI NOR `100k program/erase cycles` 作为保守基线
- 结合当前 sector 数、事务大小、compact 频率进行估算

需要说明：

- 本仓库支持的 Flash MID 涵盖 GigaDevice、Puya、ZB 等多个厂商
- 本次未逐一核实所有 MID 的官方 datasheet
- 因此本文的寿命结论用于工程评估和优先级判断是足够的，但不应替代量产前按实际 MID 做最终校核

## 12. 最终判断

最终判断如下：

- 当前设计不会因为一次异常断电，就把 `soc_kv`、`cold_kv`、`runtime`、`event_log` 的已提交内容整体写坏
- 当前最需要优先处理的问题是 hot `soc_kv` 寿命风险
- `cold_kv` 不是立即错误，但在高频改参业务下偏小
- `btname` 存在掉电丢值风险，但影响范围相对局部
- 当前分区的主要问题不在“已经重叠”，而在“1M OTA 边界缺少防呆”
- 如果后续要继续演进 OTA 方案或拓展板型，建议先补齐 1M 布局保护

总的来说：

- 需要优先整改 hot `soc_kv`
- 需要补齐 1M OTA 边界保护
- cold `kv` 是否扩容取决于现场参数写入频率

