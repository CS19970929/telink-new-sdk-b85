# D008 开发状态与未完成清单

更新日期：2026-09-14。目标：**HS-D008 / TLSR8251F512ET32 / DVC1124-2 / 默认 24S LFP，兼顾 20S NMC 装配**。

硬件/寄存器审核入口：[HS-D008 / DVC1124 对齐 D013 框架与寄存器配置审计](HS-D008_DVC1124_D013_Framework_Audit_2026-09-14.md)。

> 当前结论：D008 已从旧的“DVC 驱动直接挂业务”演进为 D013 同类的 compile-time AFE boundary，并加入通信失效 output-inhibit、连续有效快照恢复资格、有限频率重新初始化和 SOC profile 数据层。自动化构建通过只能证明源码/构建契约，没有替代 HS-D008 实板保护与功率回路验证。

## 1. 分支与基线

- D008 重构基线：`refactor/bms-template-phase1` @ `dc230c36291956e69bf7c20f641a121c55d7b105`。
- D013 框架参考：`feature/sh3673510-d013-bms`；只参考架构、安全恢复、SOC profile 和测试方法，不复制 D011/SH3673510 硬件配置。
- 当前 D008 对齐分支：`feature/sh3673510-d013-bmsdvc`。

分支名保留用户指定名称，但**本分支产品硬件仍是 D008 + DVC1124-2**。

## 2. 已落地的软件框架

| 项目 | 当前实现 | 边界 |
|---|---|---|
| AFE boundary | `bms_afe_backend.h` 编译期选择 DVC1124；业务层使用 `bms_afe.h` | 不做运行时 AFE factory/ops table |
| AFE safety guard | 记录 CHG/DSG 请求；invalid snapshot 立即 inhibit；连续 3 个完整有效 snapshot 才恢复资格 | I2C 物理失联时“关 FET 命令”只能 best effort，硬件 WDT 尚未签核 |
| 自动重新初始化 | 连续 3 次无效采样触发 backend init；5 s cooldown；重新初始化后仍需新 snapshot 重新资格审查 | 当前 DVC reset 路径主要使用 CST reset；PD7 真正 power-cycle 仍需实板验证 |
| DVC register truth | `dvc1124_reg.h` 集中管理地址/mask/shift/access/side effect | 不允许业务层复制 magic value |
| 板级默认 | `dvc1124_project_config.h` | 与产品保护阈值分开 |
| FET 拓扑 | D008 GP5=low-side CHG、GP6=low-side DSG；高边 CHG/DSG 默认屏蔽 | 高边脚实际 NC 的结论来自 D008 原理图 |
| 保护配置 | requested 与实际量化值分离；写入后 readback | SCD/WDT/Body Diode 仍未签核 |
| OC latch recovery | 去除 CHG_IN/SW 等错误板级 GPIO 假设；按实际测量电流和恢复阈值判断 | SCD 不使用“关断后零电流”直接清除 |
| 温度断线 | 配置的 Battery/MOS NTC 无效时置 `BMS_ERROR_TEMP_BREAK` | NTC R-T 曲线仍需对应实物料号确认 |
| Flash | protection/system/SOC/runtime/event/DVC config 分区独立 | 掉电/rollback-fail 仍需故障注入 |
| SOC profile | LFP/NMC OCV/profile 数据从算法抽离；化学体系/profile ID 为 additive cold-KV key | 默认旧设备保持 AUTO 兼容；D008 24S/20S 产品值仍需最终签核 |
| CI | Host contract + TC32 clean rebuild + check-fw + MAP/manifest/verify + cppcheck | CI 成功不等于实板功能通过 |

## 3. 本轮明确修正的 DVC 默认值

根据 HS-D008 原理图和 DVC1124-2 Reference Manual V1.2：

```text
HSFM = 1
```

D008 实际使用 GP5/GP6 低边 CHG/DSG，高边 CHG/DSG 路径未使用，因此屏蔽高边驱动。

```text
CAES = 0
CWT  = 0
```

当前产品没有已签核的休眠电流唤醒阈值，避免“唤醒引擎开启、阈值关闭”的半配置状态。

```text
INT_MASK = 0xFF
```

0x79 是中断**屏蔽位**；0 表示允许事件产生 1 ms 脉冲。D008 当前不消费 DVC GP INT，因此默认全部屏蔽。

以下功能继续保持 0/关闭，原因是**尚未完成硬件安全验证**，不是因为芯片不支持：

```text
SCD threshold/delay
DVC I2C watchdog
Current wake threshold
Body-diode auto recovery
Core over-temperature shutdown
```

## 4. 保护量化事实

D008 原理图十只 2 mOhm 分流器并联，等效约 200 uOhm。DVC1124 的硬件量化因此为：

```text
CC2 LSB      = 1.5625 mA
OC1 1 code   = 1.25 A
OC2 min/step = 20 A
SCD 1 code   = 50 A
CWT 1 code   = 50 mA
BDPT 1 code  = 0.2 A
```

历史 requested OC2=15 A 无法由硬件表示；若该请求仍存在，必须报告 effective=20 A 或在产品层拒绝，不能把 requested 当作硬件实际值。

## 5. 发布前 P0/P1 缺口

| ID | 优先级 | 缺口 | 完成判据 |
|---|---|---|---|
| D008-001 | P0 | SCD 仍关闭 | 基于 MOS/线束/保险丝/短路峰值完成阈值与延时实板签核 |
| D008-002 | P0 | I2C 完全失联时软件关 FET 只能 best effort | 验证 DVC WDT 或板级 power-cycle 的物理安全路径，并与 software inhibit 无竞态 |
| D008-003 | P0 | D008 24S LFP / 20S NMC 产品 profile 未最终定稿 | 串数、化学体系、容量、OV/UV、OC、温度、SOC profile、GP 装配整体签核 |
| D008-004 | P1 | 0x53/0x54 MOS mask 仍有 reset-default 依赖 | 把 WDT、反向恢复、预充/预放、Body Diode 策略全部显式配置并 readback |
| D008-005 | P1 | SCD release policy 尚未实现 | 外部负载移除判据 + debounce + W0C clear + readback + controlled retry 实板验证 |
| D008-006 | P1 | GP2/GP3 外部 NTC 为 BOM 选件 | 两种产品 profile 各自明确 OFF/NTC，未装通道不得参与保护 |
| D008-007 | P1 | NTC R-T 表为历史 10K 表 | 对 `SNC103B13435F0603E` 规格/温箱核对并覆盖开短路 |
| D008-008 | P1 | Balance 只有寄存器控制基础 | <60 s 条件化续期、温升、采样干扰、停止条件实测 |
| D008-009 | P1 | Open-Wire 只有 trigger API | 完整 trigger/wait/read/evaluate/restore 状态机 |
| D008-010 | P1 | Flash apply-success/persist-fail/rollback-fail 无统一 inconsistent 状态 | 进入 fail-safe inhibit，记录事件，重启后完整恢复 |

## 6. 实板最低验证矩阵

- **I2C/AFE**：正常、NACK、CRC 错、SDA/SCL 异常、AFE 掉电、重复 reset/reinit；确认错误有界、不会自动重开 MOS。
- **电芯**：24S、20S mask、首/中/末通道、max/min、共模二次校准。
- **电流**：0 A、正负小电流、大电流、温漂、SOC 方向。
- **NTC**：正常温区、开路、短路；Battery/MOS 两路必须 fail-safe。
- **保护**：COV/CUV/OCD1/OCD2/OCC1/OCC2/SCD 的 requested/code/effective/实际延时与栅极动作。
- **MOS**：四种 CHG/DSG 请求组合、通信失败、保护触发、恢复、Sleep/Wake。
- **WDT**：停通信直至超时，再恢复第一笔通信，确认硬件与软件 inhibit 没有恢复竞态。
- **Balance/Open-Wire**：60 s auto-clear、续期、测量干扰和热。
- **Flash/OTA**：擦写中掉电、配置 rollback、OTA 10/50/99% 中断。

## 7. 发布原则

满足以下三个证据层次后才允许把状态从“代码完成”升级为“可发布”：

1. **Host contracts**：寄存器真值、框架边界、Flash/SOC 合约通过；
2. **固定 TC32 production build**：clean rebuild、BIN check、MAP/manifest/verify、cppcheck 通过；
3. **HS-D008 实板记录**：保护、MOS、通信故障、低功耗、Flash/OTA 的测试记录绑定到具体 commit。

缺任何一层，都不能把自动化构建成功等同于量产安全完成。
