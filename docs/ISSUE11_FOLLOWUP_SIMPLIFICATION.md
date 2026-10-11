# Issue #11 后续：模块内部计数与调用收敛

## 范围与基线

用户在首轮完成后明确要求继续减少模块内部变量、函数、判断和代码量。本轮直接在 `codex-bms-monorepo` 上实施，基线 `29daac9e1c5de27a58fb6acb1aa3ba98868053c7`，最终生产代码 `0a55fd2d`。上轮 [执行报告](ISSUE11_SIMPLIFICATION.md) 和 [源码导航快照](ISSUE11_SOURCE_INVENTORY.json) 继续保留其原固定提交身份，不改写为本轮证据。

四产品实际 `sources.txt`、产品配置和 SDK 保持不变。本轮仅修改六个公共 core C 文件及相关测试、文档。用户原有 D008 均衡/温度/低压休眠参数修改保持不变且不提交；前后资源均使用相同 development、`-O2`、dirty=1 条件。

树外原始证据：`C:/Users/Administrator/AppData/Local/CodexTemp/bms-issue11-followup/`。未生成固件 BIN、烧录或执行 OTA。

## 已实施精简

| 模块与源码对象 | 删除或合并 | 等价依据及保留约束 |
|---|---|---|
| `bms_sw_protection.c:bms_sw_filter_t` | `trip_count/recover_count` 合并为 `count`，36 个保护实例各少一个 u16 | `active=0` 只累计触发，`active=1` 只累计恢复；切换阶段时归零。触发仍按超限累积、正常样本减一，恢复仍要求连续样本；两个算法没有合并 |
| `bms_sw_protection_reset_current_recovery` | 只清活动故障的共享计数 | 原接口只撤销恢复窗口；未激活时旧 recover_count 本就为零，必须保留已经累计的触发样本。新增公共接口行为场景覆盖充、放电两方向 |
| `bms_soc.c:soc_runtime_t` | 两组三元素告警计数合并为 `soc_low_count[3]` | SOC Low 的触发和恢复均要求连续样本；用已有 active 选择阈值，转为活动时才记录历史。动态阈值、失效样本和禁用项仍清对应计数 |
| SOC 私有调用 | 删除 `soc_cycle_to_u16/get_soc_display/soc_discharge_sag_hold_active/soc_eta_reset` | 只有类型转换、字段读取或固定对象转发，直接调用原所有者；保留循环数限幅、独立显示状态、压降保持和 ETA 状态。测试通过公开诊断读取显示值 |
| SOC 采样入口 | 删除 GAP 分支重复时间戳赋值和有效路径重复 valid=1 | 时间戳此前已赋值，invalidate 不修改它；正常路径只有 input_valid 已成立才到达。前帧电流、方向转换、余数和电压资格保留 |
| `bms_event_log.c` | 删除 `bms_event_log_report_store_error`；append 删除启动 bool 参数及恒成功返回分支 | 错误直接交原所有者；启动语义唯一对应 `BMS_START_UP`。三个业务入口均先成功 init，append 只更新 RAM，落盘失败仍由 snapshot 返回 |
| Event 初始化/读窗口 | 业务入口直接调用自带 ready 快返的 init；环形读索引由 while 改为一次 if | init 的诊断/IO 在 ready 快返之后；合法位置与 reg 都为 0..99，计算索引范围为 0..198，最多减 100 一次。空记录、重复合并、失败保留及休眠重试不变 |
| `bms_state_store_write_all` | 删除局部 `next` 持久结构副本，直接更新 pending 后保存 | 原先也是保存前发布 pending、保存成功后才更新已提交缓存。通信设置入口 `set_soc_cycle` 仍保留候选，因为它在失败时不应发布 pending |
| `btname_modbus.c` | 删除 `btname_set_default_suffix/btname_load_suffix_from_store`；删除 sanitize 未消费的长度返回值 | 初始化一个入口完成缓存读取、字符清洗、空值回退。getter 失败不写输出；有界复制、长度、保存失败不应用名称等行为不变 |
| `modbus_rtu.c` | 删除唯一调用的 `reg_requires_param_save` 与局部 `protect_changed` | 单写入口直接判断原范围 `0x2100..0x2140`；候选复制、校验、保存、异常码和广播处理均不变 |

本轮生产 C 文件增加 72 行、删除 146 行，**净减少 74 行**；删除 **8 个私有函数**，不删除生产文件、不增加模块或公共接口。软件保护与 SOC 共减少 **39 个 u16 计数槽（源码存储量 78 byte）**；最终 RAM span 受目标布局和对齐影响，以资源报告为准。另删除一份局部 State 候选结构、一个 Modbus 判断变量及 Event 启动参数。测试净增加 17 行。

## 明确保留的状态

- `active` 与阶段计数不能再合并为一个有符号“聪明编码”；活动故障在传感器失效、计数清零时仍需保持。
- SOC 当前输入快照与前一个已接受电流不同，重复时间、方向切换和无效样本路径仍需区分；没有删除前帧电流或时间资格。
- Event 的 dirty、last_failed、event_latched、merge_ready 表示不同事实；RAM 追加成功不等于 Flash 成功。
- State pending 与已提交缓存不同；强制保存和通信设置的失败发布语义不同，不能合并两个入口。
- 软件 Third OC 的物理恢复证据、新鲜样本、AFE 重初始化保持策略，以及温度失效锁存均保持。
- 阈值、寄存器、单位、CFG2/schema 3、协议地址、产品能力及低功耗政策未修改。

## 验证

完整主机回归 **114/114 通过**，0 失败；`full-host/report.json` 的 `partial=false`、`source_changed_during_run=false`、`integrity_errors=[]`。输入指纹为 `c8d13c5e4d12edaa51b72fb5e21d45859ae863d3bcebfefd3fc1be9e9dc0445c`，报告记录的每个输入文件 SHA-256 均已与最终工作区重新核对一致。

相关 **ASan/UBSan 30/30 通过**，0 失败，覆盖软件保护、温度组、SOC/电源、State/Event/Config 存储、Modbus、诊断、名称、D014 安全闭环及三款 SH 恢复。使用 WSL GCC 和 `-fsanitize=address,undefined -fno-sanitize-recover=all`，属于相关范围验证，`asan-host/report.json` 明确为 `partial=true`，不是全部 114 组 sanitizer 回归。其输入指纹与完整回归相同，源码未变化、无输入完整性错误。

树外 `final-evidence-check.py/.json` 记录两份报告逐文件 SHA-256 复核、原 D008 补丁全文比对、六配置前后资源/ELF/MAP 指纹与构建目录无 BIN 的最终检查，全部通过。

与首轮最终回归比较，仅四产品 protection_scenarios 的观察摘要变化：断言数从 508703 增至 508711，原因是新增恢复复位边界场景；不能把测试内容变化误报为业务行为变化。相同扩展夹具下的新旧实现等价结果如下。

- 原实现取自本轮基线。四产品分别直接编译原/新 `bms_sw_protection.c`，每次完成 **508,711 次边界断言**及 **60,000 帧混合输入**，输出摘要一致。混合序列包含动态 Filter、温度无效、诊断电压、非新鲜样本、恢复证据复位、清除、AFE 软件重初始化和分组禁用；见 `filter-equivalence.py/.c/.log`。
- SOC Low 从原/新源码抽取真实函数，执行 **1,000,000 步**输入/参数/计数失效序列，逐步比较三级状态、当前阶段计数和历史次数，全部一致；见 `soc-count-equivalence.py/.c/.log`。这是算法等价证据，非完整生产 TU 或实板证据。
- 第二批局部回归 **14/14 通过**，包含四产品 State/Event/Config 掉电场景、核心契约、Flash 检查，以及 D008 SOC/电源、名称保存；见 `local-check/report.json`。最终全量回归还包含随后增加的名称回退场景与 Modbus 内联变更。
- 独立 Standards、Spec 复审最终代码均无剩余阻断。复审只读，不冒充独立运行测试。

## 六配置资源对比

单位为 byte。基线是首轮结束时的相同源码、产品参数和工具链构建，本轮源码固定为 `0a55fd2d8a9390c5c57e39f4c0797964df8095ec`。两侧均为 development、`-O2`，包含同一份用户 D008 配置修改。

| 产品 / profile | Flash 前 → 后 | Flash 变化 | RAM span 前 → 后 | Flash 剩余 | RAM 增长预算 |
|---|---:|---:|---:|---:|---:|
| D008 / 16s-lfp | 124340 → 124100 | -240 | 25932 → 25852 | 2876 | 3844 |
| D008 / 20s-nmc | 124340 → 124100 | -240 | 25932 → 25852 | 2876 | 3844 |
| D008 / 24s-lfp | 124340 → 124100 | -240 | 25932 → 25852 | 2876 | 3844 |
| D011 | 123716 → 123508 | -208 | 25480 → 25400 | 3468 | 4296 |
| D013 | 122900 → 122692 | -208 | 25460 → 25380 | 4284 | 4316 |
| D014 | 123556 → 123332 | -224 | 25480 → 25400 | 3644 | 4296 |

六配置 RAM span 均减少 **80 byte**，包含布局和对齐效果。RAM 增长预算是在 32768 byte SRAM 中扣除 RAM span 及 3072 byte 主栈预留后的预算，并非实测最坏栈。Flash 边界仍为 126976 byte；六配置均保留 Flash 余量不足 8 KiB 的资源提示。

四产品 `sources --check` 通过，六配置 `link` 均为 0 errors / 0 warnings，`resources.json` 均记录 `image_generated=false`。前后证据分别位于树外 `bms-issue11/final-build/47196ba33c07/` 与 `bms-issue11-followup/build/47196ba33c07/` 下对应 profile/product 的 `gen/resources.json`，其中包含 ELF/MAP SHA-256。

四产品 cppcheck 完成：D008 分析 38 个生产 C TU，83 条 style；D011/D013/D014 各 37 个生产 C TU、79 条 style，均无 error/warning。D008 无头文件覆盖缺口；三个 SH 产品均报告 `bus_mux.h`、`sif_send.h` 两个不属于当前产品依赖的覆盖缺口，由 D008 路径覆盖。SDK 依赖仅解析并按既有范围规则排除其诊断；MISRA 未执行，不能表述为 MISRA 通过。详见 `static.log` 及各产品 `static/` 内原始报告。

## 中文提交

| 提交 | 内容 |
|---|---|
| `f01a262e` | 合并保护与 SOC 告警的互斥阶段计数，保留恢复复位边界 |
| `0a55fd2d` | 收回模块内转发函数与重复中间判断，同步行为测试 |

## 实板边界

本轮不新增硬件策略。仍需按 [硬件验证清单](HARDWARE_VALIDATION.md) 验证真实保护触发/恢复时间、MOS/Gate、温度开短路、通信失败、MCU reset、Flash 写入掉电、低功耗及最坏栈。主机序列、sanitizer、链接和 MAP 不替代这些证据；资源下降也不自动解除发布门禁。
