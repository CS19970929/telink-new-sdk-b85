# D008 Flash 架构、寿命与 OTA 参数更新审核（2026-09-17）

审核代码固定到 `170b28e0862aba3df2026beaa203348555a39093`，分支 `refactor/d008-common-bms-features`。本文基线是上轮 IO/低功耗实现后的版本；本轮另落实电流 ≤200 mA 屏蔽与 SOC 边界，不改变 Flash 布局/schema/默认参数，也不把以下整改建议写成已完成。

用户确认：项目持续开发，不要求兼容旧版代码或迁移旧参数。后续格式调整可以明确升级 schema 并加载新默认值；仍须保证当前格式的掉电一致性、更新独立性及失败处理。保留上次有效记录是当前事务安全机制，与兼容旧产品不是一回事。

## 1. 结论

- **分层和现有分区基本合适，可继续使用。** Config/State/Event 的写入频率分离；Factory 单独预留；统一 journal 避免多个 KV 引擎。没有发现当前 512 KiB 布局内区域重叠，没必要仅为独立更新参数拆成多个物理分区。
- **“OTA 带代码并独立更新各类参数”只实现了一部分。** 软件保护、system、SOC 三个计数值、事件、老化时间有 reset epoch；硬件保护没有独立 epoch；SOC 算法配置/学习状态并非完整、独立的更新单元。更改固件版本号或默认宏不会统一更新已有 Flash 参数。
- **正常频率下擦写预算充足，高频事件/反复失败需要治理。** 同步擦除对 200 ms 采样、400 ms 有效间隔和 BLE 时序的影响比当前容量更值得优先验证。
- 本轮建议保留分区，优先补齐按语义域独立的参数版本及事务，之后再处理事件限频、写失败重试与 Flash 调度。旧版本兼容迁移代码可按开发策略删除，不继续扩大。

## 2. 分区与记录几何

依据 [flash_store_cfg.h:15](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_store_cfg.h#L15)，所有地址为半开范围以外的闭区间：

| 区域 | 地址范围 | 大小 | 评价 |
|---|---|---:|---|
| 固件 A + meta | 0x00000..0x1FFFF | 124 + 4 KiB | 固件代码与 meta 分开 |
| 固件 B + meta | 0x20000..0x3FFFF | 124 + 4 KiB | 当前 SDK OTA 双区 |
| Event | 0x40000..0x47FFF | 32 KiB / 8 扇区 | 100 条逻辑事件，每次持久化整个 ring 快照 |
| 保留 | 0x48000..0x52FFF | 44 KiB | 未分配 |
| State | 0x53000..0x5AFFF | 32 KiB / 8 扇区 | SOC/DSG/cycle/learning/runtime 高频域 |
| Config | 0x5B000..0x5EFFF | 16 KiB / 4 扇区 | 软件保护、系统、硬件保护、控制版本、蓝牙名低频域 |
| Factory | 0x5F000..0x60FFF | 8 KiB / 2 扇区 | 目前无 writer；适合后续生产身份/校准 |
| 保留 | 0x61000..0x73FFF | 76 KiB | 未分配 |
| SDK/pairing | 0x74000..0x7EFFF | 44 KiB | 保留给 SDK |
| MAC/calibration | 0x7F000..0x7FFFF | 4 KiB | 不可清除 |

有 120 KiB 尚未分配，但不能直接当成某个 OTA 槽位的连续扩展。改 OTA boot address 必须同步重新审查 SDK 映射与 Event 起始地址；当前 512 KiB + 非 0x20000 multi-boot 配置会被存储布局检查拒绝，见 [flash_store_cfg.h:38](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/flash_store_cfg.h#L38)。

基线默认固件 BIN 101540 bytes，124 KiB 槽位余量 25436 bytes（约 20%）；为上次已通过的实测构建尺寸，本次代码变更的最终尺寸以对应 Actions 为准。1 MiB/2 MiB 布局只有源码/host 区域检查，本轮不把它们当成 D008 实板装配或运行结果。

| 域 | payload | header + 对齐后 slot | 每 4 KiB 扇区 slot | 一轮所有扇区可提交数 |
|---|---:|---:|---:|---:|
| Config | 284 B | 316 B | 12 | 48 |
| State | 24 B | 56 B | 73 | 584 |
| Event | 202 B | 236 B | 17 | 136 |

Config=65×2 软件保护 +10×4 system +35×2 AFE +5×4 control +24 蓝牙名。header=32 B，4 B program 对齐；Event 尾部有 2 B 对齐填充。来源：[bms_config_store.c:20](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_config_store.c#L20)、[bms_event_log.c:10](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_event_log.c#L10)、[storage_record.c:194](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/storage_record.c#L194)。

## 3. 正常写入与掉电一致性

`storage_record` 使用 magic/format/schema/length/sequence/CRC32，两枚 commit word 最后写；同扇区追加，满后轮换下一扇区，保留包含上一份有效记录的扇区。写/擦后平台逐字节校验；成功后才替换 Config/State 缓存，Event latch 也在保存成功后更新。顺序与实现：[storage_record.c:256](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/storage_record.c#L256)、[storage_record.c:281](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/storage_record.c#L281)、[bms_config_store.c:146](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_config_store.c#L146)。

本轮扩充并执行真实 C journal host 测试：三种实际 payload/扇区组合连续三轮写入，验证各扇区擦除次数均为三次；元数据/payload/commit 共 56 个字节断点的失败恢复；最新已提交 payload CRC 损坏时回退；sequence 从 UINT32_MAX 到 1；原有普通写入及扇区切换断电检查。测试文件 `tests/storage_record_host_test.c`，入口 `python tests/flash_quick_check.py`。

限制：这些是 RAM Flash 模型，不模拟真实擦除中间电压、Flash timeout 后仍 BUSY、单粒子失效或芯片保留时间。首次初始化没有旧有效记录时无法恢复凭空不存在的数据；显式 format 也不是原子保留旧内容的操作。

## 4. 擦写寿命模型

官方 [TLSR8251 数据手册 V1.0.1，15.7 表 15-7](https://wiki.telink-semi.cn/doc/ds/DS_TLSR8251-E_Datasheet%20for%20Telink%20BLE%2BIEEE802.15.4%20Multi-Standard%20Wireless%20SoC%20TLSR8251.pdf)列出 -40..85°C 条件下 100k 擦除次数、20 年 retention、4 KiB 扇区；页编程典型/最大 1.6/6 ms，扇区擦除典型/最大 150/500 ms。本次获得官方检索索引中的表内容，全文下载未成功；实际料号/批次/MID/工作条件需核对，以下 100k 是计算依据，不是实物验收结论。

在连续成功追加、均匀轮换的理想条件下：

`可提交次数 ≈ 每扇区擦除额度 E × 扇区数 S × floor(4096 / slot_bytes)`

| 域 | E=100000 的理想提交预算 | 10% 工程降额预算（不是厂商另一规格） |
|---|---:|---:|
| Config | 4,800,000 | 480,000 |
| State | 58,400,000 | 5,840,000 |
| Event | 13,600,000 | 1,360,000 |

| 假设持续写入负载 | 擦写预算耗尽时间（100k / 10% 降额） | 解读 |
|---|---|---|
| Config 1000 次/日 | 13.15 / 1.32 年 | 远超常规参数修改频率，但通信脚本持续改值可能达到 |
| State 400 次/日 | 400 / 40 年 | 仅为擦写数学值，不能突破 retention/环境/整机寿命限制 |
| State 5 次/秒 | 135.2 / 13.5 天 | 压力假设，非正常 SOC 更新速率；不能按每 200 ms 强制保存 |
| Event 100 次/日 | 372.6 / 37.3 年 | 同样不是整机寿命承诺 |
| Event 1 次/分钟 | 25.88 / 2.59 年 | 故障频繁进出应评估降额 |
| Event 1 次/秒 | 157.4 / 15.7 天 | 事件抖动/反复复位时需限频与合并 |

实际调度：State 在 SOC/DSG/cycle 改变时保存，不是每次主循环固定写；runtime 每分钟仅在老化模式保存，默认限额 4320 min=3 天，达到后停止老化累计，不能误算为永久每分钟写。一个完整放电/充电循环大致有 200 次整数 SOC 变化只是估算，学习、人工设置、端点校准、重启与故障会改变次数。参考 [bms_state_store.c:81](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_state_store.c#L81)、[bms_state_store.c:138](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_state_store.c#L138)、[runtime.c:44](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/runtime.c#L44)。

Event 对故障上升沿去重，但解除后再次发生会再写；CBC 值变化也写。每次事件保存整个 100 条 ring（236 B slot），不是只写 2 B 新事件；当前没有全局写入速率预算。失败留下脏 slot 会提前跳扇区，反复失败还可能反复擦同一目标扇区，因此理想预算在故障场景会高估寿命。

结论：现有 32 KiB State、32 KiB Event 对正常频率足够，暂不建议为寿命盲目扩区。优先统计每域成功写/擦/失败次数及每小时峰值，合并重复事件、限制重试；量产寿命以降额模型与实际事件分布校验。

## 5. OTA 参数独立更新能力

启动顺序是 `LoadParam → Param_UpgradeReset_Apply → AFE init → SOC init`，见 [app.c:760](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L760)。reset epoch 非零且与保存值不同才执行；零表示禁用。同一固件后续启动若标记成功保存则不重复。当前 PROTECT/SYSTEM/SOC/EVENT 都为 0，RUNTIME 为 1。仅改固件版本字符串或默认阈值、但不改 epoch 时，已有有效 Flash 仍被采用。见 [conf.h:255](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/conf.h#L255)、[param.c:299](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/param.c#L299)。

| 类别 | 当前 OTA 后自动更新路径 | 完整性判断 |
|---|---|---|
| 软件 First/Second/Third/Recover/Filter | 改编译默认值 + `FW_UPGRADE_RESET_PROTECT_EPOCH` 非零新值 | 已有整组重置；不是按字段 patch；不应借它改硬件 profile |
| AFE 硬件阈值/延时/恢复/enable | 无独立 epoch；已存 valid requested profile 会继续使用 | **未实现独立 OTA 更新**；首次空 profile 才构造默认；更改 schema 会使旧 profile 验证失败，不能当成更新方案 |
| DVC 板级/fail-safe 固定配置 | 固件宏每次 AFE init/reset 重新下发 | 已有；不是 Flash 参数升级 |
| system / 化学体系 / SOC profile ID | `FW_UPGRADE_RESET_SYSTEM_EPOCH` 整组重置；AUTO 后按 D008 编译装配填身份 | 能整组重置，不能独立只换 SOC identity；非 AUTO 旧身份不会由 24S/20S 编译切换自动替换 |
| SOC/DSG/cycle 运行状态 | `FW_UPGRADE_RESET_SOC_EPOCH` | 只重置这三项；learned capacity/flags 不清、runtime 不清 |
| SOC 算法参数/OCV 曲线 | `g_soc_config` 与 profile 表主要为编译/RAM 配置 | OTA 改源码可变；没有独立持久化版本域；nominal capacity 算法读 `CapacityFactory` 宏，不读 Config 中的 capacity_factory 镜像 |
| Event | `FW_UPGRADE_RESET_EVENT_LOG_EPOCH` | 清空逻辑 ring，通过追加空快照提交；标记在另一 Config 记录 |
| runtime | `FW_UPGRADE_RESET_RUNTIME_EPOCH` | 重置老化累计，进入老化模式；标记在另一 Config 记录 |
| 蓝牙名 / Factory | 无专属升级 epoch / Factory 尚无 writer | 无完整 OTA 更新机制 |

硬件 profile 依据 [bms_afe_hw_profile.c:287](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_afe_hw_profile.c#L287)；SOC 运行状态依据 [param.c:221](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/param.c#L221)、[bms_state_store.c:119](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_state_store.c#L119)；算法容量依据 [SocEnhance.c:421](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L421)。Config 的 system 字段可编码/保存不代表都有通信入口或已被运行算法消费，维护时必须分别核对。

## 6. 确认缺陷与风险

### ST-01 · P1 · 软件保护升级保存失败仍改变运行 RAM

触发：非零保护 epoch 不匹配、LoadParam 已判定旧参数有效，随后 Config 保存失败。`param_upgrade_apply_default_protect` 先把 g_tParam 换成新默认，再尝试保存；失败只抬 EEPROM 错误，既没有恢复旧 RAM，也没有撤销已成立的 s_protection_params_valid。可造成“本次运行新阈值、重启后旧阈值”，且输出资格不因该标志被撤销。证据：[param.c:207](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/param.c#L207)、[param.c:243](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/param.c#L243)、[param.c:299](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/param.c#L299)。

最小建议：候选副本校验 → 参数与版本原子持久化 → 发布 RAM；失败保持旧值并明确升级失败状态，禁止用 EEPROM 错误位代替保护授权判定。

### ST-02 · P2 · 升级版本标记失败被吞掉，参数与标记非原子

触发：参数提交成功、随后 control epoch 保存失败或两次提交之间掉电。`param_upgrade_mark_epoch` 丢弃返回值，启动流程继续；下一次启动再次执行重置，期间通信修改的参数或累计状态可能再次被覆盖。Config 内重置也用了两个完整记录；State/Event 跨域重置有更大的事务窗口。证据：[param.c:200](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/param.c#L200)、[bms_config_store.c:235](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_config_store.c#L235)。

最小建议：Config 内候选参数+applied revision 同一 record 提交；State 自己记录 SOC/runtime revision，Event 自己记录清空 revision；返回明确失败，不把“参数写成功”误报为“升级已完成”。

### ST-03 · P2 · State 自动保存失败未对外报告，也无重试间隔

触发：SOC/DSG/cycle 变化且写失败。`bms_state_store_update_and_log_if_changed` 丢弃保存返回值，内存缓存仍旧；主循环下一次继续尝试，可能反复验证/擦写，SOC 变化不能可靠持久化却没有在该路径抬存储错误。证据：[bms_state_store.c:138](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_state_store.c#L138)、[app.c:951](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L951)。

最小建议：记录 pending 最新 State、限速重试、上报错误；成功后再清 dirty。关机同步 flush 与正常延迟合并分别处理。不得每圈立即重试耗尽 Flash 或阻塞采样。

### 待验证风险与功能缺口（不冒充已复现电气故障）

- 硬件保护独立 OTA 更新缺失、SOC reset 非完整状态 reset：见第 5 节；不能回答为“已全部实现”。
- Flash 擦除同步阻塞且 SDK 写函数关中断后等待 busy；官方最大擦除 500 ms 大于当前 400 ms SOC 有效间隔，多个域同轮写入还会叠加。需实测保护采样/BLE/OTA 丢包与 I2C WDT；不能因为平均磨损小就认为时序安全。平台 `begin()` 总返回成功，Flash session 函数只控制锁恢复，不是独立的 OTA/BLE 安全窗口调度器。
- record load 返回 0 同时表示无有效记录与读失败；平台 read 接口又固定返回成功。当前开发期默认值回退可接受，但不能据此称为完整 Flash 故障诊断。
- 持续异常事件/反复关机失败（允许每 5 s 尝试并写 sleep event）会放大 Event 磨损；均匀寿命公式不覆盖反复失败。
- `param_migrate_temperature_protection_v1` 和旧 cold-KV 部署迁移注释仍存在，与本次“不做旧版兼容迁移”方向不符。它不是 Flash V1 引擎读旧格式，但确实是保留的参数兼容修补逻辑；后续删掉并用明确开发期默认/版本策略替代，避免零阈值含义被启动副作用改写。证据：[param.c:85](https://github.com/CS19970929/telink-new-sdk-b85/blob/170b28e0862aba3df2026beaa203348555a39093/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/param.c#L85)。

## 7. 最小整改次序（本轮未擅自执行参数重置/重分区）

1. **先做独立参数版本与失败事务。** 保留四域分区和统一 journal；Config 内增加软件保护、AFE 硬件保护、SOC config、system 独立 applied revision。每类版本不同则构造对应新默认，其余内容保留；一次 Config 提交参数及标记。每次 OTA 只更新需要的版本，而非跟随 firmware version 全清。
2. **State/Event 版本归各自记录。** SOC state reset 明确是否清 learned capacity/flags；SOC、runtime、Event 清空标记与数据同域提交。开发期允许 bump schema 后整域恢复默认，不设计旧结构迁移器。正式开始独立 revision 后，同 schema 的参数更新仍须遵守原子提交。
3. **删除旧兼容修补；区分真参数与镜像。** SOC nominal capacity、SOC 算法配置和 assembly identity 只能各有一个权威来源，不能保留“Flash 可写但算法不消费”的假接口。
4. **处理寿命与调度。** State 合并保存/限速重试，Event 重复事件合并/限频；低电压/Flash session/ BLE 时间窗口调度必须以 SDK 和实测为依据。必要时选合适时机预擦，而非先扩分区。
5. **验证门禁。** 新版本/同版本重复启动、选定域更新与其他域不变、各字节/擦除/commit 失败、标记失败、掉电重启、无旧格式路径、24S/20S × 四种保护模式完整构建；再上板做掉电、Flash 最坏延迟和 OTA 压力测试。

## 8. 本轮电流边界变更

按用户最终确认，`abs(current_ma) <= 200` 的充/放电报告都置零，SOC 不积分；**仍是静置候选**，必须有有效/新鲜 AFE 电压、压差/稳定条件和持续时间，不能从无效帧构造“零电流”。比较发生在原始 mA 转成 0.1 A 之前；201 mA 在现有协议中可能显示 0.2 A，这是原协议分辨率，不是把原始 200 mA 放行。

保留原始 snapshot mA 作诊断及 suspend 500 mA 判断，不伪造 ADC 样本，不改 DVC 硬件阈值。共享 floor 在 `conf.h`；SOC 即使配置更低 deadband 也不能绕过产品不可靠区间。测试覆盖正负 199/200/201、零、极值饱和、200 mA 可积累静置资格及无效帧清除资格。

本轮 Flash 交付是本报告、开发约束和新增 journal 验证；ST-01..03 及独立硬件参数升级仍待后续整改，不因电流修复或 CI 通过而关闭。
