# 四产品资源预算与验证约定（2026-09-21）

适用 D008 / D011 / D013 / D014 的 TLSR8251、TC32 固定 ABI 和既有 124 KiB APP 槽。
本次完善源于 2026-09-21 四项目最新代码、MAP、ELF 审计。它建立工程门禁和运行观测，不替代实板签核。

## 已落实的资源边界

- `boot.link` 和 `bms.py map` 同时要求 `__SRAM_SIZE == 0x848000`，静态 RAM 终点严格低于栈顶减 3072 B。
- 3 KiB 是主栈的初始工程预算。旧 600 B 小于已经识别的保存/复位调用链局部帧之和，不能继续作为安全依据。新的 3 KiB 也不是反汇编证明的最坏上界。
- IRQ 栈仍为独立 384 B，已经计入 BSS，不能从主栈余量推断 IRQ 栈安全。
- 以 `_ram_use_end_ - 0x840000` 计静态地址跨度，包含 RAM code、retention 对齐、SDK/cache 保留区、data、BSS、no-init。禁止用 `data+bss` 代替物理 SRAM 占用。
- 槽门禁检查正式 BIN，包含 Telink 16 B payload 对齐及 4 B CRC；MAP 缺符号、8258 启动、RAM 越界、BIN/MAP 长度不符、最终槽越界均失败。`verify` 同样调用该检查，随后校验 manifest、CRC、源输入和工具链证据。
- `map` 生成既有构建输出目录中的 `gen/resources.json`，含 Git/ELF/BIN/MAP hash、section 大小、占用、余量。`--baseline <旧 resources.json>` 输出 Flash/RAM 增量，`--output` 可指定正式证据位置。
- Flash 剩余 <8 KiB、预留栈后 RAM 可增长空间 <2 KiB 为工程告警。单次 >1 KiB Flash 或 >256 B RAM 增长要求资源 review；这些阈值不应机械阻止必要安全功能。
- 默认及保护组合构建均要求零编译 warning。未改变 TC32 ABI、O2、SDK 库、OTA/Flash 分区。

构建新增 `gen/compile-inputs.json`：记录编译源、SDK 头文件/汇编 include、宏、Git 身份、linker、脚本、SDK 库和工具可执行文件 hash；全部对象依赖该指纹。头文件/宏/工具变化会触发保守重编译，无变化不改指纹时间。manifest 保存输入与 ELF/MAP/LST/raw BIN hash，verify 拒绝陈旧源码或被替换产物。旧 manifest 缺少新证据时必须重建。

## 主栈与 IRQ 栈观测

启动汇编在 BSS/data 初始化后、首次调用 C `main` 前，分别以 `0xA5A5A5A5` 填充 `_bms_main_stack_bottom_..__SRAM_SIZE` 和 `bms_irq_stack_bottom..bms_irq_stack_top`。不覆盖活跃 C 帧、持久 no-init 或 SDK cache。

`bms_stack_monitor_poll()` 在主循环处理后执行，每次每个栈最多扫描 64 个连续 word，另检查底部 4 个 guard word。它不擦写 Flash、不修改活跃栈、不在 ISR 中扫描。

调试器读取 `g_bms_stack_watermark`：

| 字段 | 意义 |
|---|---|
| `valid_mask` | bit0 主栈、bit1 IRQ；对应位为 1 才完成过一轮扫描 |
| `main_free_min_bytes` | 主栈从底部起连续未被改写的最小观测字节数 |
| `irq_free_min_bytes` | IRQ 栈对应指标，单独计算 |
| `overflow_flags` | bit0 主栈、bit1 IRQ 底部 guard 被触碰，复位前保持 |

主栈观测使用量 = `__SRAM_SIZE - _bms_main_stack_bottom_ - main_free_min_bytes`；IRQ 对应 `384 - irq_free_min_bytes`。这是“被写过的深度”：仅调整 SP 而未写入的帧、碰巧等于填充值的数据、尚未运行的路径均可能使水位低估需求。必须结合反汇编和压力测试，不能称为完整内存自检或栈溢出隔离。guard 被破坏也不能保证系统仍可继续可靠运行。

本轮不扩展通信地址；水位先通过 ELF 符号/调试器采集。正常 suspend 保持观测历史；完整重启/普通 deep sleep 重新初始化。当前四产品 `PM_DEEPSLEEP_RETENTION_ENABLE=0`；将来开启 retention 必须重新审查启动恢复路径和水位生命周期。

## Common BMS Framework 的共享与差异

`tools/audit_product_branches.py --check` 检查四个最新产品分支的提交。开发中可显式传 `--worktree PRODUCT=PATH` 检查候选源码；报告会标记为工作树证据。

| 范围 | 门禁/所有权 |
|---|---|
| 四项目字节一致（LF 归一） | SOC、Record、State store、软件保护核心、stack monitor |
| SH 三项目一致 | features、AFE guard、Event schema 1、Flash 平台、SH NTC 常量表 |
| 四项目语义一致 | MAP/tool AST、SRAM/栈预算、Flash transaction 核心 |
| 明确保留差异 | DVC/SH 寄存器、IO、AFE 阈值量化、低功耗、物理能力、D008 schema 2 Event 的重复计数/epoch、diagnostic 接入方式 |

共享代码仍在各产品分支维护。修改共享模块的提交者负责在其他分支同步同一补丁和 host 回归；发布前执行四分支审计。无需为此引入 runtime factory、共享全局 scratch 或替换 AFE 驱动。

D013 的软件核心现与公共实现一致：First/Second 离开本级阈值后按 Filter 清除（等于阈值仍 active）；Third 使用 Recover 回差；电池温度新故障需要对应方向电流资格，既有故障按恢复条件清除；电池 NTC 和 MOS NTC 分别判有效。AFE/IO/装配配置不因此改变。D013 PC4 200 ms debug LED 默认关闭，仅允许显式台架开关。

D008 采用用户确认的 `D008_PRODUCT_PROFILE_24S_LFP` 默认值；16S LFP 定义恢复为 16，20S NMC 保留。profile ID、SOC chemistry 和报告串数一致，三种选择均有编译验证。实际装配仍须核对对应 BOM。

## Flash 写入与日志

SH 平台已接入与 D008 相同的 OTA/SDK 解锁会话排他、readback 失败 5 s 退避。六个既有 diagnostic counter 槽记录 program 调用、erase 调用、verify 失败、deferred write、最大 program/erase 32k tick；program 调用数不等于物理 page 编程次数，不能直接换算寿命。

SH Event 保留 schema 1、202 B payload、100 条 ring 和原寄存器布局。事件先接受到 RAM，每 60 s 最多一次周期 checkpoint；保存失败保留 RAM 记录、latch 和 dirty，5 s 后重试。初始化幂等，工厂清空只有落盘成功才发布，失败回退 ring、游标与边沿状态。

正常休眠前尝试刷新日志；Flash 故障或退避不能永久否决低压休眠。代价是异常断电、失败后进入 deep sleep、OTA reboot 或长时间存储不可用时可能丢失未落盘窗口；ring 超过 100 条仍覆盖最旧记录。若产品要求故障前最后一条必达，需要独立掉电/供电和写延迟约束，不能无界重试实现。

SH 三份逐字节相同的 NTC 表已合并为一个 `const sh3673510_ntc_10k[60]`，数值、插值、缩放和 AFE 量化不变；D008 的 NTC 模型不参与合并。

## 构建与发布验证

1. `python tests/run_host_regression.py`、`python bms_tools/bms.py sources --check`。
2. 使用 `EXTRA_DEFINES` 分别 clean rebuild SW/HW 的 1/1、1/0、0/1、0/0；每组运行 `check-fw`、`map`、`manifest`、`verify`。非 1/1 仅用于台架。
3. `python bms_tools/bms.py static --no-report -j 4`。报告 severity、coverage gap 和未执行的 MISRA，而不是只看进程返回码。
4. 发布必须从干净提交以 `EXTRA_DEFINES=-DBMS_PRODUCTION_BUILD=1` 重编译；默认关闭开发 GPIO/日志，拒绝 test hooks、dirty/空身份和保护关闭。存在用户未提交改动时，用对应提交的独立干净 worktree 验证，不临时伪造 dirty=0。
5. 保存 commit/profile/defines、ELF/MAP/BIN/manifest/resources、编译/host/static 日志并运行四分支共享审计。

## TODO_VERIFY_HW 与后续工作

当前必须在实板关闭的项目：24S 装配与采样身份；参数保存/工厂复位/故障风暴/OTA/中断密集场景下的主和 IRQ 栈最小余量；启动填充耗时；Flash 最坏擦除阻塞、200 ms 采样 gap、BLE/OTA 稳定性；D013 温度方向资格和各级触发/恢复；SH pending 日志的断电/睡眠取舍。每份结果记录准确 commit、BIN SHA256、板版本、电源条件和压力场景。

近期：D008 每个新功能提交都附增量预算；测量存储次数/最大阻塞、采样 gap、SOC 拒收、trace 覆盖。生产镜像仍接近 APP 槽上限，大功能不能仅凭物理 Flash 尚空闲来规划。

暂不处理：数十字节常量、正常 struct padding、薄 wrapper、已经被 gc-sections 丢弃的旧 backend/SDK 示例；不删诊断/readback，不强行共享通信 buffer，不启用 LTO/更换 ABI，不调整 OTA 分区。完整 CPU/RAM 自检另立专项，现有栈观测不冒充自检完成。
