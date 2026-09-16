# D008 AFE 代码、配置与文档核对报告

> 修复基线：`3b36fb85bc7761987db77a3bed3e83930097e399`
> 交付版本：包含本文的提交
> 产品：HS-D008 + TLSR8251F512ET32 + DVC1124-2
> 证据日期：2026-09-17

## 1. 证据与边界

核对顺序为：HS-D008-24S100A-V1 原理图、DVC1124-2 Reference Manual
V1.2、当前分支源码、厂商 Demo、项目文档。源码说明实际软件行为；原理图说明
连接，不证明 BOM 全部装配；编译和 Host contract 不证明 MOS Gate、电流或保护
时序已经通过实板验证。

本文不把 R6 `CHGF/DSGF` 当成物理 MOS 反馈。状态始终分为：软件 Requested、
R81 AFE command、R6 driver flag、外部 Gate/Vgs/电流 physical feedback。

## 2. 板级连接核对

| 项目 | 当前代码/配置 | 原理图/资料结论 | 结果 |
|---|---|---|---|
| AFE | `DVC1124_MODEL_22` | DVC1124-2 | 一致 |
| I2C | PC0=SDA、PC1=SCL、100 kHz | MCU 与 AFE 同名网络 | 一致 |
| AFE enable | PD7=`MCU-AFE-EN`，active high | PD7 驱动 AFE interface enable | 一致 |
| 地址 | write `0x40` / read `0x41` transfer address | 固定地址产品配置 | 一致 |
| Rsense | `200 µΩ` | 10 × 2 mΩ 并联 | 一致 |
| GP1 | heater NTC | GP1 温度网络 | 代码职责已冻结；温箱待验证 |
| GP2/GP3 | battery NTC #1/#2 | GP2/GP3 温度接口 | 网络存在；各 BOM 是否实装待确认 |
| GP4 | power MOS NTC | GP4 温度网络 | 代码职责已冻结；温箱待验证 |
| GP5/GP6 | CHG_LS / DSG_LS | 外部低边驱动链 | 一致；实际 Gate/Vgs 待测 |

## 3. 固定 DVC 配置核对

固定 operating/board policy 的唯一 owner 是 `dvc1124_project_config.h`，每次
AFE reset/init 后由 `dvc1124_config_store.c` 重申。历史 DVC operating-config
Flash 记录不再读取。

| 项目 | 当前 production 值 | 寄存器/证据 | 结论 |
|---|---:|---|---|
| Cell count | 24S LFP；可编译 20S NMC | R106-R108 measurement mask | 20S 屏蔽 Cell21..24，期望 `F0 00 00` |
| GP mode | GP1..4 NTC，GP5/6 low-side FET | R116=`0x49`、R117=`0x7F` | 一致 |
| High-side FET | masked | R85 `HSFM` policy | 与低边拓扑一致 |
| CADC / CC1 | work enable；4 ms；sleep wake 32 ms | R85/R86 | 一致 |
| VADC | enable、CC2 sync、每周期、1.54 ms | R109-R112 | 一致 |
| Charge pump | 10 V | R109 CPVS code 5 | 一致 |
| V3P3 | sleep/work enable；timeout restart off | R119 | 一致 |
| Current wake | engine off、threshold 0 µV | R85/R101 | 一致 |
| Timed wake | off | R120 | 一致 |
| Interrupt | mask `0xFF` | R121 | 当前未消费 DVC interrupt |
| DSG pull-down | DPC=16 | R82 | reset-equivalent named policy |
| Body diode | 80 µV | R102 BDPT=2 | common-port 策略；量产裕量待实板 |
| I2C watchdog | 4 s | R119 IWT=100b | production 固定 fail-safe |
| Timeout close | CHG=1、DSG=1 | R53 DWM=0、R54 CWM=0 | `0=允许动作`，配置一致 |
| Core OT | disabled | R118 | 未签核前保持关闭 |

R89-R100 的 COV/CUV/OCD/OCC/SCD 阈值和 delay 不属于上表固定值；其 Requested
由 `bms_afe_hw_profile` 独立持久化，应用后读取 Effective 并校验量化结果。

## 4. 生命周期与安全状态核对

| 场景 | 当前要求 | 实现结论 |
|---|---|---|
| Power-on/reset | 输出 inhibit，AFE reset，HW profile + 固定配置，3 帧资格 | 一致 |
| I2C 连续失败 | 一次 best-effort off，随后静默 5 s 等 DVC 4 s WDT | 一致 |
| 主动 Shutdown | 清均衡、FET hard-off、进入零 I2C hold | 由 common guard 统一管理 |
| Shutdown Wake | 完整 init/config/readback，旧 snapshot 作废 | 已禁止“只恢复通信”路径 |
| Sleep | inhibit、清均衡/FET，再进入 backend sleep | Shutdown hold 时禁止额外 I2C |
| 无效软件保护参数 | 保留 Flash、置存储错误、CHG/DSG inhibit | 只有有效完整记录持久化后解除 |
| 单侧保护 | blocked 侧 AUTO_DIODE，对侧 ON | R81 一次写入，不周期重写 |
| 双侧/通信/open-wire inhibit | CHG/DSG hard-off | 一致 |

## 5. 配置所有权与接口核对

| 数据 | 唯一 owner | 外部接口 |
|---|---|---|
| 固定 DVC operating/board policy | 编译期 `dvc1124_project_config.h` | `0x2800` semantic 只读 |
| DVC raw mirror | 实际寄存器 | `0x2900` 只读 |
| 软件三级保护 | `g_tParam.protect` | `0x2100..0x2140`，完整校验后持久化 |
| AFE HW protection | `bms_afe_hw_profile` | `0x42` 会话 + 35-word 原子事务 |
| Requested FET | common AFE guard | 不等同 R6 或物理 Gate |
| AFE driver flag | DVC R6 | 只由有效采样更新协议状态 |

Windows 上位机唯一真源为分支
`feature/windows-afe-hw-protection-editor-v2` 的 `bms-tool-windows/`；当前产品
分支中的历史 Assistant 客户端说明不再有效。

## 6. 本次修复与仍需实板验证

本次修复覆盖：受控 Shutdown/Wake、无效保护参数 fail-safe、所有启用级别的
Recover 回差、20S/CI 契约、上位机文档真源及不可达 Modbus 分支。

仍需实板签核：

1. GP2/GP3 在各 BOM 的实际装配及四路 NTC 开短路/温箱曲线；
2. GP5/GP6、外部 driver、MOS Vgs、PACK/LOAD 和电流的对应关系；
3. 真正停止 I2C 后约 4 s 的 CHG/DSG 关闭波形；
4. Shutdown hold 无 I2C、Wake 完整配置及三帧后恢复顺序；
5. 20S Cell21..24 mask、24S 全通道和 open-wire/balance；
6. COV/CUV/OCD/OCC/SCD requested/effective、量化、触发、恢复和 rollback；
7. 80 µV Body-Diode 阈值在噪声、温度和实际 MOS 下的量产裕量。
