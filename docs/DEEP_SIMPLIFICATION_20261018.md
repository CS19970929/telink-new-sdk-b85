# 2026-10-18 全工程复杂度审查与精简

本页记录 `refactor/full-project-simplification-20261018` 分支的一轮全工程审查与精简。
基线为 `codex-bms-deep-simplification-20261016` 的
`bb772cf061a37f9ba2bdbff34d77bf9481f18244`，在独立 worktree 中进行，未改动其他任务的工作区。
**本轮没有烧录、没有执行 OTA、没有生成交付镜像**；四产品结论只到 host 检查与 TC32 link/resources。

## 0. 结论先说

- 四产品、全部生产源文件（`bms/` 下 117 个 `.c/.h`）都做了逐文件与调用关系审查，
  分 core / app / AFE / platform / products 五个层次，含 AFE 两个后端与全部产品差异。
- 生产代码净减少 **157 行**（25,619 → 25,462），删除 2 个结构字段、1 个枚举、
  1 个无人读取的换算；同时把 3 处 72 行级的近似复制收成单一实现。
- 该分支此前已经过 10 轮以上减法（`docs/history/BMS_SIMPLIFICATION.md`、
  `docs/DEEP_SIMPLIFICATION_20261016.md` 等），剩余可证明的结构性冗余确实很窄：
  四个独立审计分别给出的安全净减上限是 DVC ≈130 行、SH ≈150 行、存储 ≈40 行、SOC ≈40 行、
  guard/diag ≈38 行。本轮取走了其中"低风险 + 可被现有 host 覆盖"的部分，其余列在第 6 节。
- 基线本身不是全绿的：`bb772cf0` 上完整回归 **132 组 15 失败**，全部是测试脚手架问题
  （最近两个未执行测试的提交留下的），生产行为没有失败证据。第 1 节记录了修复与证据。

## 1. 基线验证（修复前）

`python tests/run_host_regression.py`（本机 MinGW32 cc）在 `bb772cf0` 上得到 15 组失败，
分成三类，均为**测试侧**：

| 失败族 | 根因 | 修复 |
|---|---|---|
| `d008_recovery_host_check`（1 组） | `cc5c7a45` 新增的断言要求"恢复窗口正好 2000 ms 时就清锁存"，与同函数内既有的严格毫秒边界断言矛盾；实现按 `>` 不提前满足 | 等待循环补到越过 2000 ms 再注入清锁存失败（`d684f83c`） |
| `sh_recovery_counters` / `sh_safety_chain` / `sh_register_scenarios`（11 组） | 共享夹具 `sh_recovery_evidence.c` 的 `pm_get_32k_tick` 桩在只编译后端的场景下无人引用，`-Werror` 判失败 | 桩标记 `__attribute__((unused))`（`d684f83c`） |
| `sw_temperature_groups_host_check`（4 组） | `9313ec09` 新增的"重初始化保留锁存"场景把 `mos_otp_filter_10ms` 改成 60 后没有恢复，后面"单样本即阻断"的断言失效 | 该段结束处恢复默认滤波现场（`1ae8594b`） |

三处都只改测试脚手架，不改生产行为；修复后相关单项与四产品完整回归均通过（见第 5 节）。

## 2. 逐轮提交

| 轮 | 提交 | 范围 | 净行数 | 验证 |
|---|---|---|---:|---|
| 0 | `d684f83c` | 测试：D008 严格边界、SH 未使用桩 | ±0 | 上述两项单项检查 |
| 0b | `1ae8594b` | 测试：SW 温度分组滤波现场 | +2 | 四产品该检查 |
| 1 | `a137d6c8` | SH 寄存器事务：写/复位命令帧合并，读帧回显表，重试路径共享 | −123 | SH 十项 host + 四产品 link/resources |
| 2 | `7207ff1a` | SH 硬件保护阻断位由标志直接合成 | −9 | SH 四项 + link/resources |
| 3 | `d6193b00` | DVC：删除只写不读的快照字段与转发枚举 | −14 | DVC 九项 + link/resources |
| 4 | `7f51ca7d` | 诊断窗口地址判断只保留一份实现 | −9 | diagnostics/modbus/core + link/resources |

每轮独立提交、独立验证，没有把重构压成一个提交。

### 第 1 轮细节（收益最大）

`sh3673520.c` 里 `write_reg_once` 与 `reset_once` 各 72 行，帧结构都是
"命令 + 参数一 + 参数二 + CRC"，回显顺序都是"空闲、命令、参数一、参数二、ACK"，
只有字节来源不同。合并为 `sh3673520_command_once(cmd, a1, a2)` 后，任何回显/CRC/ACK
规则只维护一处；`sh36735xx_expect()` 收掉读路径里五次重复的"发送-判状态-判回显"；
读/写重试循环共用 `sh3673520_retry_stop()`，复位事务保留自己的 settle 延时和更严格的
不可重试条件。文件 795 → 672 行，D011/D013/D014 投影镜像各少 480 B 级。

## 3. 前后指标

口径：`bms/` 下全部 `.c/.h`（含 products 与两个 AFE 后端），不含 SDK、tests、bms_tools、docs。
行数为文件原始行；函数/字段/宏由固定脚本统计（`@@` 见第 7 节复现命令）。

| 指标 | 重构前 | 重构后 |
|---|---:|---:|
| 生产 C/H 文件 | 117（45 C / 72 H） | 117（45 C / 72 H） |
| 生产代码行数（原始） | 25,619 | 25,462 |
| 生产代码行数（去注释口径） | 24,508 | 24,344 |
| 函数定义 | 711 | 713 |
| 文件级可变对象 | 225 | 225 |
| 结构体字段 | 507 | 505 |
| 函数式宏 | 2,086 | 2,086 |

函数数 +2 的构成：新增 `sh36735xx_expect`、`sh3673520_command_once`、`sh3673520_retry_stop`
三个单一职责原语，删除 `DVC1124_RegReadEffect` 转发。用 3 个原语换掉 200 行级重复，
是本轮唯一"函数数增加"的地方，已按"净理解成本"复核。

### 资源（development 配置，`link` + `resources`）

| 产品 | 投影镜像前→后（B） | Flash 剩余 | RAM 地址跨度前→后 |
|---|---:|---:|---:|
| D008 16S LFP | 115,092 → 114,996（−96） | 11,980 | 22,296 → 22,292（−4） |
| D011 10S LFP | 112,820 → 112,324（−496） | 14,652 | 21,872 → 21,872 |
| D013 10S LFP | 109,428 → 108,932（−496） | 18,044 | 21,788 → 21,788 |
| D014 8S LFP | 111,876 → 111,380（−496） | 15,596 | 21,868 → 21,868 |

RAM 列是地址跨度，含 RAM code、cache 与 IRQ 栈，不等于实测最坏栈。D008 的 −96 B 来自
DVC 快照字段删除；SH 三产品的 −496 B 主要来自寄存器事务合并。

## 4. 删除、合并与简化清单

- **SH 寄存器事务**：2 个 72 行的 near-duplicate 收成 1 个公共命令帧；读帧用显式回显表；
  3 处重试循环骨架收成 1 个；`-123` 行。
- **SH 硬件保护状态**：`publish_hw_status` 的 4 个局部掩码（只被一次 `||` 使用）删除，
  充电/放电阻断位由标志位直接合成；`-9` 行。
- **DVC 快照**：删除 `cell_count`（与 `s_cfg.cell_count` 重复）、`vtop_mv`（全仓只写不读，
  且每轮多一次换算）两个字段；`-2` 字段。
- **DVC 读副作用判断**：`DVC1124_RegReadEffect` 只服务下一行，枚举 `dvc1124_reg_read_effect_t`
  也只服务这两个函数，合并为直接比较；`-1` 函数、`-1` 枚举。
- **诊断窗口地址判断**：服务诊断与量产 stub 两份逐字相同的 `bms_diag_overlaps` 合并为一份，
  避免两支的拒绝范围悄悄分叉；`-9` 行。
- **测试脚手架**：修掉基线 15 组失败（第 1 节），未改任何生产断言。

## 5. 验证

| 项目 | 结果 |
|---|---|
| 四产品 `sources --check` | 通过（102/101/101/101 项，摘要未变） |
| 四产品 `link --jobs 4` | 0 error、0 warning |
| 四产品 `resources` | 全部 PASS，数字见第 3 节 |
| 生产配置（`--production`）link/resources | 见第 5.1 节 |
| 完整 host 回归 | 132 组，结果见第 5.2 节 |
| 分组 host 检查 | SH 10 项、DVC 9 项、diagnostics/modbus/core/production-policy 各产品通过 |
| 静态分析 / ASan / UBSan / CMake CTest | 本轮未执行（用户未要求，且改动不含存储格式与算法数值） |
| 实板、Flash 掉电、低功耗、物理保护、OTA | 未执行，不构成验收 |

### 5.1 生产配置（`--production --d008-profile 16s-lfp link/resources`）

| 产品 | 投影镜像前→后（B） | Flash 剩余 | RAM 地址跨度前→后 |
|---|---:|---:|---:|
| D008 16S LFP | 110,484 → 110,356（−128） | 16,620 | 22,296 → 22,292（−4） |
| D011 10S LFP | 108,020 → 107,508（−512） | 19,468 | 21,616 → 21,616 |
| D013 10S LFP | 104,596 → 104,084（−512） | 22,892 | 21,532 → 21,532 |
| D014 8S LFP | 107,060 → 106,548（−512） | 20,428 | 21,612 → 21,612 |

"前"是同一台机器上对 `bb772cf0`（干净主工作区）执行同一组命令的结果。
D008 20S NMC / 24S LFP 的补充结果见第 8 节。

### 5.2 完整 host 回归

`python tests/run_host_regression.py`，输出目录在源码树外、运行期间不改源码与 HEAD：

| 运行 | 结果 |
|---|---|
| 基线 `bb772cf0`（未修复） | 132 组，**15 失败**，全部测试脚手架（第 1 节） |
| 本轮最终 HEAD | 132 组，**0 失败** |

失败数为 0 的运行同时通过了仓库的验证完整性检查（源码内容与 HEAD 在运行期间未改变）。

## 6. 看起来复杂但必须保留的部分

四个独立审计逐条核对了"能否删"，以下属于**不能删的真实复杂度**，本轮未动：

- **AFE guard**：`output_enabled`/`comm_inhibit`/`config_inhibit`/`comm_fault_latched`/
  `bus_silenced`/`test_shutdown_hold`/`cycle_failed` 的迁移集合不同；1 秒通信确认、
  两次明确错误、watchdog 期总线静默、`apply_requested()` 的唯一授权仲裁不能合并。
- **DVC**：启动零点两次 270 ms 新样本检查、I2C watchdog 4 秒/5 秒等待、45 秒均衡续期、
  读写重试与 CRC、RC/W0C 自清除边界、两阶段配置基线与失败路径写入。
- **SH**：C+/LOAD 互斥与 200 ms 稳定等待、短路"清除后下一帧确认"、flag clear 失败锁存、
  VADC/CADC 分组恢复、1.2 秒启动温度资格、5 次重试 + 端口恢复、20 秒均衡续期。
- **软件保护**：触发"越限累积/正常递减"与恢复"连续满足"是两种滤波；SC/OCD/OCC 的
  物理恢复证据不能由关 MOS 后零电流代替。
- **存储**：schema 4 布局、记录头/CRC/双 commit、扇区轮换、三次写失败预算、
  `0xFA57` 出厂标记与三域提交顺序、显式 little-endian 编码。
- **SOC**：200 ms 应用时间积分、重复时间戳不推进、余数与方向切换、OCV 单向与置信区间、
  满/空端点、Estimate/Display 分离、六个独立确认计数器、diag wire 200–255 布局。

## 7. 尚未完成、但已定位的下一步

审计给出了可直接落地的方案，本轮因"改动面/验证成本/协议裁决"没有实施，
按收益排序留待后续：

1. **`bms_config_store.c` 默认保护表与 D008 重复**（约 −100 行）：`D008_SW_` 与 `BMS_SW_`
   两套 65 字段初始化器、16 行 pack 检查、16 行 chemistry 覆盖逐行相同。
   若在 `bms/products/bms_battery_defaults.h` 增加 `BMS_SW_FIELD(name)` 前缀宏，
   可只保留一份；该头不在 `parameter_inputs_hash` 的输入清单里，OTA 标识不受影响，
   但需要同步 `d014_defaults_host_check.py`、`storage_host_check.py` 的切片口径。
2. **OTA 更新策略的整份默认缓存**（约 −412 B 启动栈）：`bms_config_apply_update_policy`
   在栈上构造完整 `bms_config_cache_t`，实际每组只用 1–2 个字段；改为按组直接取默认即可，
   需覆盖 16 种 Config 选择组合。
3. **`storage_record` 的失败状态与 `has_latest`**（约 −17 行、−1 字段）：
   `UNCOMMITTED/INCOMPATIBLE/CORRUPT` 在 `bms/` 内没有区分消费者；
   `has_latest` 与 `latest_addr != INVALID` 等价。
4. **SOC `empty_anchor_latched`**（约 −23 行、−1 字段）：只写不读，14 处引用全在
   `if (!自己) 写自己`。
5. **诊断 word 24/25 双写（需要协议裁决，不是精简）**：`bms_diag_params()` 与
   `bms_diag_set_boot_result()` 都写 word24，启动顺序让 AFE 结果被 params 位覆盖；
   同一 word 两种编码。先由协议/上位机所有者确认语义，再收敛为一个所有者。
6. **`bms_afe_test_wake()`**：guard 中唯一 HOST_TEST 专用块，全仓无调用者；
   需台架所有者确认后连同契约测试与文档一起删。

## 8. 最终结果与边界

- 完整 host 回归：**132/132 通过，0 失败**（含验证完整性检查）。
- 四产品 `sources --check` / `link` / `resources`：全部通过，0 error 0 warning。
- 生产配置 ELF：D008 16S LFP 110,356 B / 20S NMC 110,372 B / 24S LFP 110,388 B，
  D011 107,508 B、D013 104,084 B、D014 106,548 B；RAM 地址跨度 21,532–22,292 B，
  均在 TLSR8251 32768 B 与 126,976 B APP 槽内。20S/24S 未做基线对照构建，
  只记录最终值；D008 的差异来自两个后端共用代码，16S LFP 的 −128 B 是实测差值。
- 没有生成 BIN、没有烧录、没有执行 OTA；`link` 只产生 ELF/MAP/LST。
- 未执行：静态分析、ASan/UBSan、CMake CTest、实板采样/保护/MOS/均衡/休眠、
  Flash 掉电、客户 OTA。host 与 MAP 不能替代这些验收。
- 本轮改动不涉及外部参数协议、OTA 标识、内部 schema 4 存储布局、保护阈值、
  采样周期或任何产品宏数值。

## 8.1 剩余最值得做的三件事

1. `bms_config_store.c` 的 D008/通用默认表合并（约 −100 行，见第 7 节 1）。
2. OTA 更新策略的 412 B 栈缓存（开始前先确认目标栈余量）。
3. `storage_record` 失败状态与 `has_latest`、SOC `empty_anchor_latched` 两个小项
   （合计约 −40 行、−2 字段），都需要重跑存储/轨迹回归。

## 9. 文档同步检查

本轮同时做了文档与代码的同步抽查（脚本 + 人工核对）：

- 导航：根 `README.md` 与 `docs/README.md` 现在都覆盖全部顶层专题文档（缺链 0）。
  `docs/README.md` 增加了"补充索引"，把此前只在根 README 出现的近期专题归入主题分组。
- 代码符号：扫描当前文档中出现的 `bms_*` / `sh36*` / `dvc1124_*` / `DVC1124_*` 标识符与
  `bms/**` 路径，逐条对照源码。多数"缺失"是**删除记录**（历史文档按约定记录"删除了什么"）
  或工具名/环境变量，不是失同步；确认并修复的真实失同步只有两处：
  - `docs/D014_SAFETY_LOOP.md` 引用的 `bms_parameters_startup()` 已改名为 `bms_parameters_init()`；
  - `docs/D008_PRODUCT_REFERENCE.md` 仍列出不存在的 `bms_afe_test_enter_shutdown()`。
- 失效链接：`OPENWIRE_RESEARCH_AND_DESIGN.md`、`OPTIMIZATION_20261010.md` 里指向
  `sh3673510_feature_backend.c` 的链接（该文件后续并入 `sh3673510_bms.c`）改为文字说明。
- 树外绝对路径：`REFACTOR_PHASE3/4/5` 的"树外证据目录"改为"当轮树外目录 + 未随仓库交付"
  说明；`docs/afe-audit/**` 中 17 处执行机绝对路径改为 `树外证据包/` 占位，并在归档
  README 写明"仓库内不保存绝对路径，按文件名与哈希核对"。
- 结构整改（同轮完成）：
  - 新增 [当前规则](CURRENT_RULES.md) 作为"哪条规则仍然有效"的唯一入口，
    根 README、`docs/README.md` 与 ONBOARDING 都指向它；
  - 23 份点时间记录在标题下加统一"固定提交记录"提示；
  - `AFE_AUDIT_REMEDIATION` 的 13 个 `文件:行号` 锚点改为符号名锚点，其中
    `DVC1124_OpenWireBegin()`（断线检测已删除）与 `DVC1124_BmsApp_AFEGet()`（已合并）
    明确标为历史符号。
- 收口后：相对链接检查只剩 4 条 `%20` 编码的 PDF 链接——文件实际存在，是检查脚本
  未解码空格造成的假阳性；两个导航入口缺链均为 0。
- 未做：对 `docs/history/**` 逐句重核。它们按仓库约定是固定提交记录，不代表当前 HEAD。

## 10. 复现

```powershell
$env:PYTHONUTF8 = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
python bms_tools/bms.py --all-products sources --check
python bms_tools/bms.py --all-products link --jobs 4
python bms_tools/bms.py --all-products resources
$env:CC = 'C:/qp/qtools/MinGW32/bin/cc.exe'
$env:BMS_TEST_OUTPUT = "$env:LOCALAPPDATA/CodexTemp/bms-simplification-20261018/host-final"
python tests/run_host_regression.py
```

指标脚本口径：统计 `bms/` 下所有 `.c/.h`；函数为花括号匹配的顶层定义；
结构字段为 `typedef struct { ... }` 内的 `;` 数；可变对象为文件作用域定义。
