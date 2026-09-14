# D008 开发状态与未完成清单

更新日期：2026-09-15。目标：**HS-D008 / TLSR8251F512ET32 / DVC1124-2 / 默认 24S LFP，兼顾 20S NMC 装配**。

硬件/寄存器审核入口：[HS-D008 / DVC1124 对齐 D013 框架与寄存器配置审计](HS-D008_DVC1124_D013_Framework_Audit_2026-09-14.md)。AFE 硬件保护参数独立架构见 [AFE Hardware Protection V2](AFE_HARDWARE_PROTECTION_V2.md)。

> 当前结论：D008 软件侧可在不猜测产品安全值的前提下完成的框架项已基本闭环，包括 compile-time AFE boundary、通信失效 inhibit/有界重初始化、独立 AFE Hardware Protection V2、显式 DVC mask policy、失败回滚/inconsistent 诊断、24S LFP / 20S NMC 物理 Profile、20S 全量 TC32 编译、Open-Wire 非阻塞状态机和条件化 Balance 续期。剩余项目主要是**产品参数签核和 HS-D008 实板验证**，不能把 CI 绿色等同于量产安全完成。

## 1. 分支与基线

- D008 重构基线：`refactor/bms-template-phase1` @ `dc230c36291956e69bf7c20f641a121c55d7b105`。
- D013 框架参考：`feature/sh3673510-d013-bms`；只参考架构、安全恢复、SOC profile 和测试方法，不复制 D011/SH3673510 硬件配置。
- **D008 canonical 分支：`feature/sh3673510-d013-bmsdvc`。** 该历史名称容易误解，但产品硬件仍然是 **D008 + DVC1124-2**，不是 SH3673510。
- **本轮实现/验证分支：`refactor/d008-afe-hw-protection-split`。** 完成验证后 canonical 分支快进到相同提交。

后续若做分支清理，建议把 D008 canonical 名称统一为 `refactor/d008-bms-phase2` 或新的 D008 专用正式分支；在完成历史兼容迁移前不要因为名称问题重写提交历史。

## 2. 已落地的软件框架

| 项目 | 当前实现 | 仍需验证/边界 |
|---|---|---|
| AFE boundary | `bms_afe_backend.h` 编译期选择 DVC1124；业务层使用 `bms_afe.h` | 不做运行时 AFE factory/ops table |
| AFE safety guard | invalid snapshot 立即 inhibit；连续 3 个完整有效 snapshot 才恢复资格 | I2C 物理失联时“关 FET 命令”只能 best effort |
| 自动重新初始化 | 连续 3 次无效采样触发 backend init；约 5 s cooldown；reinit 后仍需新 snapshot 重新资格审查 | PD7 真正 power-cycle 仍需实板验证 |
| DVC register truth | `dvc1124_reg.h` 集中管理地址/mask/shift/access/side effect | 不允许业务层复制 magic value |
| 板级默认 | `dvc1124_project_config.h` | 与产品保护阈值分开 |
| D008 assembly profile | 默认 24S LFP，可编译为 20S NMC；DVC cell count 跟随 compile-time product profile | 不自动推导产品容量/保护阈值 |
| 20S channel mask | 20S 编译屏蔽 Cell21..24；24S 不屏蔽上部通道 | 仍需 20S 实板逐通道验证 |
| Profile 兼容迁移 | chemistry/profile KV 为 AUTO/AUTO 时一次性迁移；显式配置不被启动覆盖 | 24S↔20S 换装仍必须走明确产品流程 |
| FET 拓扑 | GP5=low-side CHG、GP6=low-side DSG；高边路径固定屏蔽 | 高边脚 NC 结论来自 D008 原理图 |
| DVC 0x53/0x54 mask | 全字节显式编程；非 WDT 位保持 V1.2 reset-equivalent 策略，DWM/CWM 只跟随已持久化语义项 | 若未来启用 WDT/Body-Diode/其它自动恢复，需要重新做产品级 mask 审核 |
| Legacy DVC config normalization | 老配置强制恢复 D008 HSFM；CWT=0 时 CAES=0；其余无关字段保留 | 需要升级旧板实测一次迁移和断电恢复 |
| AFE Hardware Protection V2 | 软件保护和 AFE 硬件保护完全独立；35-word requested/effective；`0x42` 临时授权；完整块原子写 | 产品具体硬件保护阈值仍需签核 |
| 配置事务 | validate → persist → apply → verify；失败回滚；rollback 失败进入 inconsistent 诊断 | 仍需掉电/Flash 故障注入和重启恢复验证 |
| OC latch recovery | COV/CUV/OC 按实际测量值和恢复稳定时间清 latch；去除错误板级 GPIO 假设 | SCD release 单独保留，未猜测策略 |
| Open-Wire | Begin → wait → COW clear → fresh valid snapshot → raw result；期间暂停 Balance | “什么电压特征判定真正开线”的 evaluate 规则仍需手册/实板验证后签核 |
| Balance | 非零 mask 先 arm；仅有效采样、存在充电电流且无充/放保护 block 时应用；45 s 条件化续期；Open-Wire/故障时暂停 | 仍需热、采样扰动和 60 s auto-clear 实测 |
| 温度断线 | Battery/MOS NTC 无效时置 `BMS_ERROR_TEMP_BREAK` | NTC R-T 曲线仍需对应实物料号确认 |
| Flash | protection/system/SOC/runtime/event/DVC config 分区独立 | 掉电、rollback-fail、重启恢复仍需故障注入 |
| SOC profile | LFP/NMC OCV/profile 数据从算法抽离；化学体系/profile ID 独立 KV | 产品 OCV/端点/容量仍需电芯和产品签核 |
| CI | Host contracts + TC32 clean rebuild/check-fw/MAP/manifest/verify/cppcheck；同时执行 24S LFP 和完整 20S NMC TC32 build | 自动化不替代实板验证 |

## 3. D008 物理 Product Profile

默认编译：

```c
#define D008_PRODUCT_PROFILE D008_PRODUCT_PROFILE_24S_LFP
```

可选：

```text
D008_PRODUCT_PROFILE_24S_LFP -> 24S + LFP + Generic LFP SOC profile
D008_PRODUCT_PROFILE_20S_NMC -> 20S + NMC + Generic NMC SOC profile
```

`DVC1124_DEFAULT_CELL_COUNT` 引用 `D008_PRODUCT_CELL_COUNT`，运行时 DVC cell count 也直接使用该 compile-time product profile，而不再由历史 `SeriesNum` 决定物理 AFE 通道。20S 编译的 DVC channel mask 明确屏蔽 Cell21..24。

CI run `34908980327` 已同时完成：默认 24S LFP 的 clean rebuild/check-fw/size/MAP/manifest/verify/cppcheck，以及把 product profile 切到 20S NMC 后的完整 TC32 clean rebuild/check-fw/size；不是只做 Python 文本合约检查。

兼容策略：历史设备新增 chemistry/profile KV 都为 AUTO 时才写入当前编译 Profile；工厂/用户已显式配置的 chemistry/profile 不被启动迁移覆盖。该机制只解决**物理串数 + 化学体系身份**，不会自动修改历史容量、OV/UV、OC、温度等产品参数。因此 20S NMC 编译通过仍不能等同于 20S NMC 产品参数已签核。

## 4. DVC 显式安全默认与 mask policy

根据 HS-D008 原理图和 DVC1124-2 Reference Manual V1.2，当前软件明确保持：

```text
HSFM = 1
CAES = 0
CWT  = 0
INT_MASK = 0xFF
```

D008 使用 GP5/GP6 低边 CHG/DSG，高边 CHG/DSG 路径未使用，因此高边路径在默认配置、持久化配置加载和语义写接口三处都被约束。历史配置若出现 `CAES=1 + CWT=0` 的半配置状态，会被 normalization 修正并在 validation 阶段拒绝重新写入。

0x53/0x54 不再依赖“芯片此刻碰巧是什么 reset value”。软件现在对完整 documented mask byte 明确写入 reset-equivalent 产品策略，并仅对已定义的 DWM/CWM watchdog timeout-close 语义位做覆盖。未来如果产品决定启用 DVC watchdog、Body-Diode 或其它自动恢复功能，需要重新签核对应 mask，不应在通用代码里自动打开。

以下功能继续保持关闭，原因是**尚未完成硬件安全验证**：

```text
SCD threshold/delay
DVC I2C watchdog
Current wake threshold
Body-diode auto recovery
Core over-temperature shutdown
```

## 5. 保护量化事实

D008 原理图十只 2 mOhm 分流器并联，等效约 200 uOhm。DVC1124 的硬件量化因此为：

```text
CC2 LSB      = 1.5625 mA
OC1 1 code   = 1.25 A
OC2 min/step = 20 A
SCD 1 code   = 50 A
CWT 1 code   = 50 mA
BDPT 1 code  = 0.2 A
```

历史 requested OC2=15 A 无法由硬件表示；软件必须区分 requested 与 effective，例如请求 15 A 时报告实际可表示档位，而不能把 requested 当作硬件真实阈值。

## 6. 发布前仍未闭环的 P0/P1

| ID | 优先级 | 当前状态 | 剩余完成判据 |
|---|---|---|---|
| D008-001 | P0 | **SCD 仍关闭** | 基于 MOS/线束/保险丝/短路峰值完成阈值、延时和实际关断波形签核 |
| D008-002 | P0 | 软件 dead-bus inhibit 已有，但 I2C 完全失联时软件关 FET 只能 best effort | 验证 DVC WDT 或 PD7 AFE power-cycle 物理安全路径，并证明与软件 inhibit 无恢复竞态 |
| D008-003 | P0 | 24S LFP / 20S NMC Profile 与双版本 TC32 build 已实现 | 容量、OV/UV、OC、温度、SOC OCV/端点、GP2/GP3 BOM 分别签核；20S NMC 实板单独验证 |
| D008-004 | P1 | **软件完成**：0x53/0x54 full-byte 显式 policy/readback 基础已实现 | 如果未来启用 WDT/Body-Diode/其它自动恢复，做新 policy 的硬件回归验证 |
| D008-005 | P1 | SCD 故意不自动清除 | 依据负载移除判据实现 debounce + W0C clear + readback + controlled retry，并做实板短路恢复验证 |
| D008-006 | P1 | GP2/GP3 外部 NTC 为 BOM 选件 | 两种产品 BOM/Profile 各自明确 OFF/NTC，未装通道不得参与保护 |
| D008-007 | P1 | NTC 使用历史 10K 表 | 对 `SNC103B13435F0603E` 数据手册/温箱核对 R-T，并覆盖开路/短路 |
| D008-008 | P1 | **软件策略完成**：45 s 条件续期、充电/no-fault/valid-snapshot gating、Open-Wire 暂停 | 实测 60 s auto-clear、温升、采样扰动、停止/恢复条件 |
| D008-009 | P1 | **状态机完成**：trigger/wait/fresh-snapshot/raw-result/Balance suspend/restore path | 用 DVC 手册与开线实验确定 evaluate 判据，避免凭经验猜阈值 |
| D008-010 | P1 | **软件诊断完成**：rollback 失败进入 inconsistent 状态 | Flash/断电/AFE I/O 故障注入，验证事件记录、fail-safe 行为和重启恢复 |

其中 D008-004/008/009/010 不再属于“缺少软件框架”，而是进入硬件/故障注入验证阶段。

## 7. 实板最低验证矩阵

- **I2C/AFE**：正常、NACK、CRC 错、SDA/SCL 异常、AFE 掉电、重复 reset/reinit；确认错误有界、不会自动重开 MOS。
- **电芯**：24S、20S mask、首/中/末通道、max/min、Cell21..24 屏蔽、共模二次校准。
- **电流**：0 A、正负小电流、大电流、温漂、SOC 方向。
- **NTC**：Battery/MOS 正常温区、开路、短路；GP2/GP3 按实际 BOM 验证。
- **保护**：COV/CUV/OCD1/OCD2/OCC1/OCC2/SCD 的 requested/code/effective/实际延时与栅极动作。
- **MOS**：四种 CHG/DSG 请求组合、通信失败、保护触发、恢复、Sleep/Wake。
- **WDT/Power-cycle**：停通信直至超时或触发板级 AFE 重启，再恢复通信，确认硬件与软件 inhibit 没有竞态。
- **Balance**：45 s 软件续期、60 s 芯片 auto-clear、充电退出、保护出现、NTC/温升和测量干扰。
- **Open-Wire**：正常线、单通道开线、多位置开线；验证等待时间、raw snapshot、判定规则和 Balance 恢复。
- **Flash/OTA**：参数保存/AFE apply 各阶段故障、rollback 失败、擦写中掉电、OTA 10/50/99% 中断。

## 8. 发布原则

满足以下证据层次后才允许把状态从“代码完成”升级为“可发布”：

1. **Host contracts**：寄存器真值、框架边界、Flash/SOC/Profile/20S 合约通过；
2. **固定 TC32 production build**：默认 24S LFP 和 20S NMC 变体均完成 clean rebuild，默认版本额外完成 BIN check、MAP/manifest/verify、cppcheck；
3. **HS-D008 实板记录**：保护、MOS、通信故障、SCD、Balance/Open-Wire、低功耗、Flash/OTA 的测试记录绑定到具体 commit；
4. **产品参数签核**：24S LFP 和 20S NMC 的容量、电压、电流、温度、SOC、NTC/BOM 配置分别有明确产品值来源。

缺任何关键证据，都不能把自动化构建成功等同于量产安全完成。
