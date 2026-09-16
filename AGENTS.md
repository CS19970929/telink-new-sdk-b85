# AGENTS.md — D008 / TLSR8251 / DVC1124

本分支实际产品是 **HS-D008 + TLSR8251F512ET32 + DVC1124-2**。历史分支名包含 `sh3673510-d013`，不得据此推断硬件。

## 开发前必读

1. `docs/D008_PRODUCT_REFERENCE.md`：当前唯一 D008 IO/AFE/Product Profile 真值入口。
2. `docs/HARDWARE_VALIDATION.md`：当前唯一实板未决清单。
3. `docs/ARCHITECTURE.md`：软件边界/参数所有权。
4. `docs/SOFTWARE_PROTECTION.md` 与 `docs/AFE_HARDWARE_PROTECTION_V2.md`：软/硬件保护分层。
5. `references/vendor/dvc11xx_demo_v1.3/README.md`：DVC11XX DemoCode V1.3 的审查结论与低边 FET 示例索引，仅作二级参考。

## 权威依据

- 板级连接：当前已归档用户提供的 `references/hardware/d008/HS-D008-24S100A-V1.pdf`（2026-09-17 核对，SHA-256 见同目录 README）。历史 `HS-D008-24S100A-V1(2).pdf` 与本原件是否一致未确认；实际装配仍以对应 BOM 为准。
- DVC寄存器/bit/量化/时序：DVC1124-2 Reference Manual V1.2。
- 厂商 Demo：`references/vendor/dvc11xx_demo_v1.3/`，只用于调用方式、时序和交叉验证，**不得覆盖 DVC1124-2 官方参考手册**。
- 当前软件行为：本分支源码。

`references/vendor/` 仅存放参考资料，不得加入 `bms_tools/source_order.txt`、固件 Makefile/链接输入或产品运行代码；参考资料的存在不得改变现有固件行为。

资料不足时明确写 `TODO_VERIFY_HW`；禁止用 SH36735xx、BQ769xx、旧 SH367309 或“常见BMS做法”猜 DVC 寄存器和安全参数。Demo 与官方 DVC1124-2 参考手册冲突时，以官方参考手册为准；Demo 自身存在型号默认值和示例代码不一致项，禁止直接复制到量产逻辑。

## 当前源码边界

- `dvc1124_reg.h`：DVC1124 V1.2 芯片真值。
- `dvc1124_project_config.h`：D008 DVC **最终固定板级/Fail-safe 编译期配置**，不是 Flash 默认值。
- `d008_product_profile.h`：24S LFP / 20S NMC 物理 profile，只负责装配串数和化学体系/SOC身份。
- `dvc1124.c`：I2C、寄存器、量化、采样、Balance/Open-Wire、AFE硬件保护应用。
- `dvc1124_bms.c`：BMS/FET/故障适配。
- `dvc1124_config_store.c`：历史文件名保留以维持固定 source order；当前只负责 DVC backend 生命周期和编译期固定配置重申，**不得重新加入 DVC operating-config Flash KV**。
- `dvc1124_config_service.*`：固定配置/原始寄存器诊断只读；不得成为第二配置 owner。
- `bms_sw_protection.*`：统一软件三级保护。
- `bms_afe_hw_profile.*`：独立 AFE 硬件保护参数及其 Flash 持久化。

应用层不得直接复制 DVC 寄存器 magic value。涉及 safety register 的写入必须按 mask/shift、范围、量化、readback 审核。

## DVC 固定配置与 Flash 所有权（强制）

D008 已明确采用以下单一所有权模型，后续不得恢复旧的“宏只是默认值、再由 DVC operating-config Flash 覆盖”的架构：

- **编译期固定配置**：DVC 型号/地址、cell count/Rsense、GP 模式、low-side topology、high-side mask、Charge Pump、CC1/VADC、V3P3、Timed Wake、interrupt mask、DPC、current-wake policy、Body-Diode/DBDM/CBDM、DVC I2C watchdog、I2C timeout close CHG/DSG、Core-OT 固定策略等。
- **Flash 可配置的软件保护**：`g_tParam.protect`，First/Second/Third/Recover/Filter。
- **Flash 可配置的 AFE 硬件保护**：`bms_afe_hw_profile_t`，COV/CUV/OCD/OCC/SCD 及 delay/recover/enable mask。
- 其他 SOC、容量、事件等各自按既有模块持久化，不属于 DVC operating-config。

固定 DVC 配置必须在每次 AFE reset/init 后由固件重新下发；历史 `0x5F000...` DVC operating-config KV 即使残留，也不得读取、恢复或覆盖当前宏。

通信规则：

- `0x2800` DVC semantic window 中的固定字段是 **read-only diagnostics**，写入必须返回 READ_ONLY/Modbus illegal address。
- `0x2900` raw DVC mirror 仅用于只读诊断；不得允许 factory raw write 绕过配置所有权或保护事务。
- 修改 AFE 硬件保护必须走 `bms_afe_hw_profile` 的完整事务；修改软件保护走既有软件参数事务。

当前 production Fail-safe 固定值：

- `DVC1124_I2C_WATCHDOG_SECONDS = 4`；
- `DVC1124_I2C_TIMEOUT_CLOSE_CHG = 1`；
- `DVC1124_I2C_TIMEOUT_CLOSE_DSG = 1`；
- `DVC1124_BODY_DIODE_THRESHOLD_UV = 80`；
- R53/R54 mask 语义始终牢记：`0=允许该来源动作`，`1=屏蔽`。

注意：`DVC1124_I2C_CMD_TIMEOUT_US=5000` 是 MCU 单次 I2C BUSY 等待上限，不是 DVC 4s 硬件 watchdog。真实 dead-bus 后软件 I2C 关 MOS 只能 best-effort，最终 fail-safe 依赖 DVC 在失联前已正确配置的硬件 watchdog/mask。

## D008 低边 FET 状态与控制规则

- `GP5` / `GP6` 指 **DVC1124 的 GP5/GP6 引脚**，不是 TLSR8251 MCU GPIO。
- D008 使用 `DVC GP5=CHG_LS`、`DVC GP6=DSG_LS`，高边输出被 mask；前提是 D008 原理图/BOM 确认这些 DVC 引脚实际连接到低边 CHG/DSG 驱动链。
- MCU **不需要、也不应新增两个 GPIO 直接控制 GP5/GP6**。正常控制路径必须是：`TLSR8251 -> I2C -> DVC 0x51 CHGC/DSGC -> DVC 内部驱动逻辑 -> GP5/GP6 low-side output -> 外部 MOS 驱动链`。
- `0x75 GP5M/GP6M=111` 只负责把 DVC 的 CHG/DSG 驱动逻辑路由到 GP5/GP6 低边输出；MCU 日常开关 MOS 仍通过 I2C 修改 `0x51 CHGC/DSGC`，不直接操作 GP5/GP6。
- `0x51 CHGC/DSGC` 是驱动命令；`0x06 CHGF/DSGF` 是 AFE 驱动输出标志。
- Vendor Demo 的 GP5/GP6 示例把 DVC GP5/GP6 **额外接回 MCU GPIO**，只是为了独立读取 LOW SIDE 输出脚状态，不是低边 MOS 控制所必需的连接，也不得据此要求 D008 增加 MCU GPIO 控制。
- 软件至少区分 Requested、AFE Command、AFE Driver Flag、Physical Feedback 四层。当前 D008 若无已确认的 GP5/GP6/Gate/Vgs 反馈，Physical Feedback 必须标记为 unavailable/unknown。
- `CHGF/DSGF` 不得直接命名为物理 MOS 实际状态；它们只能表示 DVC AFE driver/output flag。
- **协议中的 `b1Status_MOS_CHG/DSG` 是 AFE feedback-only 字段**：D008 只能由有效 AFE 采样中的 `0x06 CHGF/DSGF` 更新；`bms_afe_set_fets()`、`mos_update()`、保护逻辑、通信控制等软件请求路径禁止直接赋值或伪造这两个状态位。
- FET 请求状态必须单独保存（当前公共 guard 的 `requested_charge_on/requested_discharge_on`），不得用 `b1Status_MOS_CHG/DSG` 充当目标缓存。请求成功只表示命令已提交；MOS/driver 状态必须等待后续 AFE 寄存器采样更新。
- 同一个 Requested 状态重复提交不得因为 AFE feedback 与目标不一致而反复写 `0x51`；AFE 保护主动关闭输出时必须允许 `Requested=ON`、`AFE Driver=OFF` 同时存在，以便诊断真实保护动作。
- 单侧保护的 common-port 续流必须一次性写最终 `AUTO_DIODE + opposite ON` 模式；禁止每 200ms 先硬关再切 AUTO_DIODE，也禁止稳态重复写相同 R81 模式。
- 若未来需要验证 GP5/GP6 物理输出或 MOS Gate/Vgs，必须先确认原理图已有反馈路径或新增硬件反馈；禁止仅凭通信寄存器伪造 physical feedback。

## 保护路径编译开关

- D008 使用 `DVC1124_SW_PROTECT_ENABLE` 与 `DVC1124_HW_PROTECT_ENABLE`，默认必须为 `1/1`。
- `1/1`：正常产品模式，软件三级保护 + DVC AFE 硬件保护 + 固定硬件 Fail-safe 策略有效。
- `1/0`：软件保护台架模式；必须真实关闭 DVC COV/CUV/OCD1/OCD2/OCC1/OCC2/SCD、I2C WDT、Body-Diode、current-wake、Core-OT 及相关自主关断源，不能仅忽略 alarm flag。
- `0/1`：DVC 硬件保护台架模式；软件保护状态机必须停止并清除软件管理的保护状态，AFE 硬件告警/恢复和固定 Fail-safe 策略仍工作。
- `0/0`：采样/通信调试模式；阈值保护和 DVC 自主安全动作关闭。I2C、单体/总压/电流/温度采样以及 `0x06 CHGF/DSGF` AFE 状态反馈仍必须正常。
- `HW=0` 不得删除或改写 Flash 中的 Requested AFE Hardware Profile；Effective 必须反映实际硬件已关闭。重新用 `HW=1` 编译后继续使用原 Requested 参数。
- `SW=0` / `HW=0` 都只允许开发、认证或台架隔离测试，**不得作为量产配置**。任何保护/固定安全配置修改必须验证默认 `1/1` 与 `1/0、0/1、0/0` 三种非量产组合至少能通过 TC32 clean build。

## 保护参数规则

- `g_tParam.protect` 只属于软件 First/Second/Third/Recover/Filter。
- AFE hardware profile 独立，不得由软件参数写入副作用修改。
- COV/CUV/OCD/OCC/SCD 等可调硬件保护阈值/delay/recover 只能由 `bms_afe_hw_profile` 持久化。
- GP2/GP3 BOM、NTC R-T、尚未签核的保护阈值不得为了“功能完整”擅自猜值。
- DVC WDT/timeout-close 与 Body-Diode 当前已成为明确的 D008 固件固定策略；若要修改，改 `dvc1124_project_config.h` 并重新做实板验证，不得改成 Flash 可写参数。

## IO规则

修改 GPIO 前必须同时核对 `docs/D008_PRODUCT_REFERENCE.md`、归档原理图和调用代码。PC0/PC1 是 DVC I2C；PD7 是 `MCU-AFE-EN`；D011 的 SPI/RS485/HT-RF-EN 等网络不得移植到 D008。

2026-09-17 用户确认的 D008 产品约束：

- D008 没有独立开关。PA0 的实际网络是 `ACC-MCU`，C 符号为 `ACC_MCU_PIN`。已移除历史 key 控制；目前不新增 ACC 业务逻辑。
- `CHG_IN_PIN` / PB1 的 `CHG-IN` 实际是负载检测电路，暂不实现负载检测业务逻辑，不得继续由名称认定它是充电器检测或充电方向依据。
- 产品深度休眠目标：AFE shutdown 成功并停止 I2C 后，最后拉低 `MCU_LDO_PIN` / PC4，给整个 MCU 断电；电路先恢复 MCU 供电，MCU 再经 I2C 唤醒 AFE并重新初始化/验证。不得用 AFE sleep + SDK DEEPSLEEP_MODE 冒充已完成该流程。
- suspend 与断电分开：MCU 仍供电时，任一方向有效、新鲜电流 ≥500 mA 退出 suspend；ACC/负载新策略暂不加入。500 mA 不等于 SOC 静置阈值，suspend SOC 校准要求见 `docs/SOC.md`，不得用无效样本/未知休眠时长补积分或静置计时。
- 当前实现与验证范围见 `docs/D008_POWER_SOC_IMPLEMENTATION.md`。主机测试/远程编译不能关闭 `TODO_VERIFY_HW`；禁止将 PB1 重新用作加热的充电源资格。

## 构建

保持 Telink SDK `tc_ble_single_sdk V3.4.2.8_Patch_0001` 和固定 TC32 工具链/ABI。修改源码顺序时显式更新 `bms_tools/source_order.txt`。任何安全相关修改至少通过 source-order、Host contracts、TC32 clean rebuild/check-fw/MAP/verify/cppcheck；这些仍不能替代实板验证。

## Windows 上位机单一真源（强制）

- D008/D011/D013 当前实际使用的上位机**唯一真源**是本仓库分支 `feature/windows-afe-hw-protection-editor-v2` 下的 `bms-tool-windows/`。
- 客户版为 `bms-tool-windows/BmsTool.Windows/`；内部完整测试版为 `bms-tool-windows/BmsFactoryTest.Windows/`。公共功能变更必须同步维护两版。
- 本产品分支历史 `tools/BMSAssistant/`、`tools/BMSAssistantQt/`、`tools/BMSAssistantAndroid/` 均为废弃客户端，不得再作为实现、协议或测试依据，也不得恢复。
- 收到“上位机、Windows 工具、事件日志、参数编辑、AFE 编辑器”等任务时，应先切到上述 Windows 上位机分支修改 `bms-tool-windows/`，不得在产品固件分支里另造客户端。
- **默认只改上位机。** 除非用户明确要求修改固件，或已证明现有固件协议无法完成需求并得到用户同意，否则不得为了适配 UI/读取逻辑而修改固件协议、寄存器地址、Flash 布局或持久化架构。
- 上位机任务遵循最小改动原则：先复用现有固件协议和寄存器；不要因为客户端读取问题扩展为固件重构、跨平台客户端同步或新协议设计。