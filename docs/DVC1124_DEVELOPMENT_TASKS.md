# DVC1124-2 开发任务顺序

> 工作分支：`feature/dvc1124-22-bms`
>
> 约束：后续开发直接提交到该分支，不再创建临时开发分支。
>
> 权威依据：DVC1124-2 Reference Manual V1.2、HS-D008 原理图/BOM、已验证实板行为、仓库 `AGENTS.md`。

## 0. 用户目标整理

当前目标不是“把 DVC1124 跑起来”就结束，而是把当前 HS-D008 + TLSR8251 + DVC1124-2 项目整理成可读、可维护、可通过 BLE/UART 配置、后续可迁移到其它 MCU/AFE 的长期代码基础。

必须满足：

1. DVC1124 寄存器描述与 V1.2 参考手册一致；寄存器地址、bit、R/W/RC/W0C/自清零语义、reset、换算公式集中管理。
2. 不允许再用难以阅读的裸 `0x49/0x7F/0x28` 配置表达功能；看到代码应能直接知道每一位功能。
3. 不使用 C bit-field 映射硬件寄存器；统一使用 address/mask/shift/enum。
4. COV/CUV/OCD/OCC 等保护参数必须可配置，并保留 requested/encoded/effective 三层概念；不能静默量化。
5. 现有 BMS 保护参数 `g_tParam.protect` 继续作为 COV/CUV/OCD/OCC requested source of truth，不能建立一套互相漂移的第二保护参数。
6. DVC 专属参数（GP、CADC/VADC、WDT、Timed Wake、Body Diode、SCD、DPC、Core OT 等）可持久化，并在 AFE reset / MCU 重启后恢复。
7. BLE 和 UART 必须共用同一套 AFE 配置服务，不能维护两份参数逻辑。
8. 需要支持完整 AFE 诊断读取；允许写的字段必须受访问权限、字段合法值、RMW、readback 约束。
9. Raw register 只能作为 Factory/Debug 能力，不能绕过语义参数和保护参数模型。
10. 运行控制（FET、Balance、Open Wire、Calibration、Sleep/Shutdown）与持久配置必须明确分离。
11. 所有新增配置都要考虑 Flash 生命周期、掉电一致性、OTA Flash 布局、旧版本升级和 schema 演进。
12. 所有修改最终必须通过固定 TC32 工具链、host-only contract tests、静态分析、MAP/size 和 HS-D008 实板验证。

## 1. 当前开发进度

已完成或基本完成：

- [x] `dvc1124_reg.h`：建立 V1.2 寄存器真值层，关键寄存器和 bit 已集中定义。
- [x] 纠正旧 Demo 中 R86、OC1/OC2 delay `+1`、0x6D、GP N/A 模式等已知问题。
- [x] `dvc1124_project_config.h`：把 GP/ADC/charge-pump 等默认配置改为具名语义配置。
- [x] `dvc1124_config_store.*`：增加独立 AFE KV 持久化区。
- [x] 512K Flash 分配 `0x5F000..0x62FFF` 给 DVC AFE config KV，并与 OTA/SMP/cold_kv 分离。
- [x] `dvc1124_config_service.*`：建立 transport-neutral AFE semantic config service。
- [x] BLE SPP 和 UART 共用 `modbus_on_frame()`，共享 `0x2800` 语义窗口。
- [x] 增加 `0x2900..0x2990` raw register mirror。
- [x] Raw write 已做 Factory 模式门禁。
- [x] Modbus 单字段 AFE 写入失败已能返回异常，而不是无条件 echo 成功。
- [x] COV/CUV/OCD/OCC requested 继续写现有 BMS cold KV。
- [x] Effective COV/CUV/OCD/OCC/SCD 可从实时 DVC 寄存器反解。
- [x] 新增 `tests_dvc1124_config_quick_check.py` 作为 host-only contract test。
- [x] 更新 512K Flash map 文档。

本轮审核后已经直接开始修复：

- [x] `modbus_rtu.c` 改为 `extern g_stCellInfoReport`，消除与 `app.c` 的重复定义风险；commit `08dbedee`。
- [x] `DVC1124_WriteRegisterFieldSafe()` 改为编码前检查 `field_max`，禁止超范围值被 mask 后静默截断；commit `63782aae`。
- [x] `dvc1124_bms.c` 删除本地 alarm 地址/bit 定义，统一使用 `dvc1124_reg.h`；commit `435f6b8b`。
- [x] host contract test 已覆盖共享 report owner、field overflow 校验和 BMS alarm canonical names；commit `cbf35ab5`。

## 2. 当前代码审核结论

### P0 / 必须立即修复

1. **`g_stCellInfoReport` 重复定义**：已修复。`app.c` 保持唯一实体，`modbus_rtu.c` 只 `extern`。
2. **寄存器真值仍有重复源**：`dvc1124_bms.c` 的 alarm 重复定义已清理；`dvc1124.c` 仍保留大量 `DVC_REG_* / DVC_ALARM_* / DVC_CPVS_*` 局部宏，必须继续全部收口到 `dvc1124_reg.h`。
3. **字段范围检查逻辑漏洞**：已修复。旧实现先 `FIELD_PREP` 再检查，无法发现截断；现已使用 `mask >> shift` 得到 field max 后再编码。
4. **RC 寄存器读副作用未隔离**：0x01 的 VADF/CC1F/CC2F 为 read-clear；0x76 的 COTF 为 read-clear。当前 raw read / config capture 可能清除事件。必须建立 destructive-read policy，不能把所有 `0x00..0x90` 当普通无副作用读取。
5. **Core OT 配置路径会读取 0x76**：通用 RMW/readback 会触发 COTF read-clear。需要专用写策略或软件 sticky capture，不能让“修改阈值”无声吞掉过温历史事件。
6. **BMS requested protection 的提交顺序还不安全**：当前 `dvc_cfg_write_bms_protection()` 先把 candidate 写入 cold KV，再更新 `g_tParam` 并置 `AFE_PARAM_WRITE_Flag`，AFE 是随后异步应用。若后续 AFE 写入/readback 失败，Flash requested 已经改变而 live AFE 仍可能保持旧值，同时通信端已经得到成功响应。保护参数最终必须改成 validate -> AFE apply/readback -> persist，或显式 pending/commit 状态机。

### P1 / 配置完整性与通信一致性问题

7. **R52/R53/R54 只实现了部分语义字段**：DPC、DWM、CWM 已抽象，但 PDWM、PDDM、PCWM、PCCM、PCDM、DDM、DPDM、DBDM、CO1M、CO2M、CSM、CDM、CCM、CPCM、CBDM 尚未完整暴露。用户要求“一眼看懂每一位”和“所有公开配置可读写”，需要补齐。
8. **0x6A..0x6C cell/measurement mask 语义不完整**：当前主要由 cell_count 自动生成，但 PKM/LDM/CTM/V1P8M 等公开字段还未形成独立语义接口。需区分“产品 cell_count 派生字段”和“可调测量 mask”。
9. **OC1/OC2 delay 的产品模型不完整**：DVC 硬件有独立 OC1/OC2 delay，但现有 `g_tParam` 每个方向只有一个 filter，因此当前两个语义地址实际映射同一个 requested 值。通信层必须明确暴露这个约束，后续决定是否新增 DVC-specific hardware delay 参数，而不是伪装成独立值。
10. **单字段事务已实现，多字段事务未实现**：当前对 DVC 0x10 multi-write 主动拒绝，避免半更新；下一步应实现 staged batch + validate + apply + readback + persist + rollback。
11. **Raw write 语义覆盖不完整**：raw-to-candidate 只接受部分寄存器；与 `DVC1124_RegPersistentConfigMask()` 的“可持久配置位”范围不完全一致。需要定义清晰的 raw allowlist，而不是两个规则漂移。
12. **DVC semantic/raw 读失败没有完整映射成 Modbus read exception**：部分失败目前通过 `0xFFFF` 数据表现，容易让上位机把 I2C/非法地址错误误当真实值。应让读接口返回 status + value，并在 Modbus 层生成明确 exception。
13. **AFE config KV 保存失败后的 rollback 结果未再次验证/升级故障**：当前会尝试 `Apply(before)`，但 rollback 自身失败时仍主要返回 store error。需要把“持久化失败 + AFE 回滚失败”提升成明确 AFE config inconsistent 状态。

### P1 / BMS 保护与 MOS 策略

14. **当前故障恢复仍偏旧项目逻辑**：OCC latch 清除依赖 `CHG_IN`，OCD/SCD 清除依赖 `SW`；尚未完整实现“充电过流后出现放电电流允许 CHG MOS、放电过流后出现充电电流允许 DSG MOS”的方向恢复策略。
15. **必须避免软件和 DVC 硬件反复抢 MOS**：相反电流恢复逻辑需要与 DVC 0x53/0x54 mask、Body Diode、OC latch、当前实际 FET 状态一起设计，不能仅在 `mos_update()` 强制 reopen。
16. **AFE 通信故障 fail-safe 需明确**：读失败、配置恢复失败、readback 失败时，MOS 是否保持、关闭还是限制，需要形成统一状态机并纳入日志。

### P2 / 架构与可维护性

17. `dvc1124.h` 中存在较大的 `static inline` 设备访问实现；长期更适合把实现移入 `.c`，头文件保留类型和 API。
18. `dvc1124.c` 仍直接依赖 `g_tParam`、`g_stCellInfoReport` 等业务对象，设备 driver 与 BMS adapter 边界还没有彻底完成。
19. 当前仍通过 `conf.h` 宏 alias 兼容 SH367309 旧 ABI；可继续作为迁移层，但最终需要减少宏劫持，收口成明确 AFE ops。
20. `dvc1124_config_catalog.json` 目前覆盖重点寄存器，不是完整 `0x00..0x90` 字段级 catalog；最终上位机 schema 需要补齐 access/side-effect/persistent/runtime/command 分类。
21. AFE KV schema 当前 fallback-to-default 可用，但还没有正式 migration 策略和 upgrade epoch。

## 3. 开发任务执行顺序

任务必须按顺序推进；前一个任务的验收未满足时，不进入依赖它的后续任务。

### TASK-001 P0：建立可编译的单一真值基线 — IN PROGRESS

目标：先消除明显 linker/重复定义/寄存器多真值问题，不改变业务行为。

- [x] `modbus_rtu.c` 的 `g_stCellInfoReport` 改为 `extern`。
- [ ] `dvc1124.c` 删除本地重复寄存器地址/bit 宏，全部切到 `dvc1124_reg.h`。
- [x] `dvc1124_bms.c` 删除重复 alarm 地址/bit 宏。
- [x] 修复 `DVC1124_WriteRegisterFieldSafe()` field overflow 校验。
- [x] 增加 host contract test 覆盖本轮已完成的 source-of-truth / ownership 修复。
- [ ] 完成 `dvc1124.c` canonical-name 清理后，再加一条测试禁止本地 `#define DVC_REG_* 0x..` / `#define DVC_ALARM_*` 回归。

验收：同一寄存器事实只有 `dvc1124_reg.h` 一个定义来源；旧行为寄存器写值不变。

### TASK-002 P0：访问属性与 destructive-read 安全层

目标：让 R/RW/RC/W0C/self-clear/command 行为在驱动中可执行，而不仅是注释。

- [ ] 给寄存器增加 access/side-effect metadata helper。
- [ ] 明确 0x01、0x76 等 destructive read。
- [ ] 普通 BLE/UART raw read 不允许无提示消费 RC 状态；提供显式 destructive diagnostic command 或 cached status。
- [ ] Core OT threshold 写入改为不会静默丢失 COTF 的专用路径。
- [ ] Alarm W0C、COW/CAMZ self-clear 继续使用独立 command API。

验收：配置读取不会意外清状态；任何 destructive read 都是显式行为。

### TASK-003 P1：补齐公开配置 bit 的语义模型

目标：手册中公开可配置字段一眼可读、可校验、可通过 service 访问。

- [ ] 完整 R52/R53/R54 semantic fields。
- [ ] 完整 0x6A..0x6C cell/measurement masks。
- [ ] CADC/CC1/VADC/CP/GP/WDT/timed wake/int mask 检查 completeness。
- [ ] 按 persistent/runtime/command/status 分类。
- [ ] 更新 `dvc1124_config_catalog.json`。

验收：V1.2 所有公开 RW 配置字段都有唯一语义名称、合法范围和访问策略。

### TASK-004 P1：保护参数模型收口

目标：所有硬件保护参数都可配置，同时不破坏现有 BMS source of truth。

- [ ] COV/CUV/OCD1/OCC1/OCD2/OCC2 requested/effective 保持现有参数源。
- [ ] 把 requested protection 写入改为“先应用/验证 live AFE，再持久化”，失败时不 ACK 成功。
- [ ] 明确 OC1/OC2 delay 共用 BMS filter 的限制。
- [ ] 决定并实现 DVC-specific OC1/OC2 hardware delay override（若启用必须独立持久化并有 enable/source 字段）。
- [ ] SCD 继续 DVC-specific store，默认 disabled，保留 `TODO_VERIFY_HW`。
- [ ] 增加 quantized/status flags 给 BLE/UART。

验收：上位机能看到 requested、actual、quantized/source；不存在两套含义不明的保护参数；通信成功响应代表 live AFE 与持久请求值已经一致。

### TASK-005 P1：配置事务与批量写

目标：实现真正的 staged transaction。

- [ ] `ConfigBegin/GetPending/SetPending/Validate/Commit/Rollback`。
- [ ] AFE apply 后 readback verify。
- [ ] persist 失败恢复 before，并验证 rollback 是否成功。
- [ ] rollback 失败进入明确的 config inconsistent/AFE fault 状态。
- [ ] Modbus 0x10 对 DVC semantic range 支持多字段原子写。
- [ ] BLE 自动复用同一事务逻辑。

验收：任一字段失败不会留下半更新 AFE 或半更新 Flash；若硬件已经无法恢复，系统明确进入故障状态而不是继续假装配置一致。

### TASK-006 P1：运行控制与配置分离

目标：FET/Balance/OpenWire/Calibration/Sleep/Shutdown 有独立 command/runtime service。

- [ ] FET command API。
- [ ] Balance mask + 60s refresh semantics。
- [ ] Open-wire command/result。
- [ ] CADC calibration command/result。
- [ ] Sleep/Shutdown command 增加安全前置条件。
- [ ] 不把 runtime command 存入 AFE config KV。

验收：persistent config 与 runtime command 地址/API 完全分离。

### TASK-007 P1：BMS 保护/MOS 方向恢复状态机

目标：实现用户要求的相反电流恢复，并与 DVC 硬件保护一致。

- [ ] 充电过流后检测到有效放电电流，允许 CHG 通道恢复。
- [ ] 放电过流后检测到有效充电电流，允许 DSG 通道恢复。
- [ ] 加入 current threshold / debounce / latch/retry 限制。
- [ ] 与 0x53/0x54、Body Diode、硬件 OC/SCD latch 协同。
- [ ] 禁止“硬件刚关 -> 软件立即开 -> 硬件再关”的循环。
- [ ] 故障日志记录原因和恢复方式。

验收：方向恢复有确定状态机、可测试条件和 fail-safe 行为。

### TASK-008 P1：AFE 通信故障与配置丢失恢复

- [ ] I2C/CRC 连续失败计数和恢复状态机。
- [ ] critical register periodic verify。
- [ ] 检测 AFE reset/config lost 后重新应用配置。
- [ ] 恢复失败时进入明确的安全输出策略。
- [ ] BLE/UART 暴露 AFE health/config generation/status。

### TASK-009 P2：Driver / BMS adapter 边界清理

- [ ] `dvc1124.c` 不再直接依赖 `g_tParam`。
- [ ] protection encoding 输入改为明确结构体。
- [ ] `dvc1124_bms.c` 负责 BMS 参数映射。
- [ ] 逐步减少 SH367309 compatibility macro。
- [ ] 为以后 STM32/Telink port 层预留 bus API，但不做过度拆分。

### TASK-010 P2：Catalog / 上位机协议完整化

- [ ] 完整 `0x00..0x90` machine-readable catalog。
- [ ] 每字段包括：access、side_effect、unit、range、enum、persistent、factory_only、requested/effective source。
- [ ] `register_catalog.json` 引用/合并 DVC schema。
- [ ] DVC semantic/raw read error 映射成明确协议错误，不再用 `0xFFFF` 伪装失败值。
- [ ] 为通用上位机生成 AFE 配置页所需 metadata。

### TASK-011 P0/P1：自动测试与构建闭环

- [ ] `tests_dvc1124_config_quick_check.py` 全绿。
- [ ] `tests_flash_quick_check.py` 覆盖 AFE KV。
- [ ] 增加保护量化纯函数测试向量。
- [ ] 增加 Modbus 语义读写异常测试。
- [ ] `python bms_tools/bms.py sources --check`。
- [ ] 固定 TC32 环境执行 `build/static/ci`。
- [ ] MAP/BIN <= OTA 124KB 限制。

### TASK-012 P0：HS-D008 实板验证

按顺序测试：I2C/CRC -> 测量 -> GP/NTC -> COV/CUV -> OC1/OC2 -> SCD -> FET -> Body Diode -> WDT -> Open Wire -> Sleep/Wake -> BLE/UART config -> 掉电恢复 -> 异常注入。

未完成实板验证的安全功能不得从 `TODO_VERIFY_HW` 升级为量产默认 enable。

## 4. 执行规则

- 每完成一个 task，更新本文件状态和实际 commit。
- P0/P1 修复优先于架构美化。
- 每次重构必须证明寄存器实际写值与预期一致。
- 不允许为了“代码更漂亮”重写已经验证的 I2C/CRC/采样核心路径。
- 不允许普通配置接口写 reserved/unnamed bit。
- 不允许把 raw register write 当作产品参数 API。
- 无固定 TC32 工具链验证时，只能标记“source review / host test passed”，不能标记“firmware build passed”。
