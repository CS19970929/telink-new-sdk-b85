# HS-D008 / DVC1124 BMS
## 对齐 D013 框架的重构方案与 DVC 寄存器配置审计

**文档版本：V1.0**  
**日期：2026-09-14**

---

> **2026-09-14 云端实施更新**：用户指定分支 `feature/sh3673510-d013-bmsdvc` 已从 D008 `refactor/bms-template-phase1` @ `dc230c36291956e69bf7c20f641a121c55d7b105` 创建。本文原始审核基线中“该分支不存在/尚未修改远端”的描述记录的是审核发生时的事实；当前实施状态以 GitHub 分支最新 commit、`docs/ARCHITECTURE.md` 与 CI 结果为准。

## 0. 审核基线与重要说明

本报告的目标不是简单把 D013 代码复制进 D008，而是把 **D013 已经形成的工程框架、安全状态机和文档/测试方法**迁移到 D008，同时保留并校核 D008 的 DVC1124 硬件事实。

用户给出的 GitHub URL 中分支 `feature/sh3673510-d013-bmsdvc` 在当前仓库中无法解析。为避免对错误分支写入，本次没有修改远端代码，审核采用以下可确认基线：

| 角色 | 分支 | Commit | 用途 |
|---|---|---|---|
| D008 当前较完整基线 | `refactor/bms-template-phase1` | `dc230c36291956e69bf7c20f641a121c55d7b105` | DVC 已完成 Phase-1 重构、CI/存储/协议框架 |
| D008 早期 DVC 基线 | `feature/dvc1124-22-bms` | `d32432a344a59ca8cebd07655979722092b76c71` | 用于追溯旧兼容层和原始 DVC 接入 |
| D013 参考框架 | `feature/sh3673510-d013-bms` | `4e8193735d046645177e4ba0ec90c22b19693e0e` | 用于对齐最新 AFE backend、安全仲裁、SOC/profile、文档和测试框架 |

Git 历史关系非常关键：**D013 当前分支以 D008 `refactor/bms-template-phase1` 的 HEAD 作为 merge-base，并在其上继续前进约 121 个提交。** 因此 D013 不是另起炉灶，而是 D008 Phase-1 框架的后续演进。D008 最合理的升级方式是“吸收 D013 的通用框架改进，同时恢复/保持 D008 的板级硬件与 DVC backend”，而不是从旧 D008 重新手工搭一套。

> 注意：D013 分支名虽然是 D013，但当前源码/文档中仍存在 `D011_*` 板级命名，README 甚至仍保留 D008 文案。因此本文只把它当作**参考框架**，绝不把 D011/SH3673510 的 GPIO、SPI、AFE 寄存器或温度通道直接套到 D008。

---

# 1. 结论摘要

### 1.1 总体结论

D008 的 DVC1124 驱动在**寄存器真值、CRC8、RMW、特殊访问、保护量化、requested/effective 分离、Flash KV 与 BLE/UART 统一配置入口**方面已经比较成熟。当前主要问题已经从“寄存器位是否写错”转移到：

1. **D008 产品 Profile 仍混有旧产品 D3PRO/通用参数**，没有真正形成 24S LFP 与 20S NMC 两套完整产品配置；
2. **D013 已有的 AFE 通信失败 output-inhibit、连续有效样本解锁、自动重初始化、安全恢复状态机没有完整移植到 DVC**；
3. DVC 部分寄存器虽然编码正确，但产品默认值不完整或不合理，特别是 **HSFM、SCD、I2C WDT、Current Wake、GP2/GP3、0x53/0x54 mask policy**；
4. 现有默认保护参数包含 `10A/15A/20A` 这类历史值，不能视为 HS-D008 24S100A 的量产参数；其中 `OC2=15A` 在 200uΩ 分流器下甚至**无法由 DVC1124 硬件表示**；
5. 短路、通信失联、持久化回滚失败、Sleep/Wake、均衡续期、Open-Wire 仍需实板闭环。

### 1.2 本次审计发现的最重要配置修改

| 优先级 | 项目 | 当前 | 建议 |
|---|---|---|---|
| P0 | D008 产品参数身份 | `FD_BMS_TYPE=D3PRO` 后再局部 override D008 | 改成真正 D008 product profile，不继承旧产品容量/保护/名称 |
| P0 | SCD | 默认关闭 | 保持“未验证前关闭”，但列为发布阻断；用短路测试签核阈值/延时 |
| P0 | AFE 通信失联 | 置错误但没有 D013 等级的 output inhibit/release FSM | 移植 D013 的 fail-safe 模式到 DVC，必要时用 PD7 对 AFE 硬复位 |
| P1 | 0x55 HSFM | `0` = 允许高边驱动 | **建议改为 `1`**；D008 主 CHG/DSG 引脚原理图为 NC，产品实际用 GP5/GP6 低边驱动 |
| P1 | CAES/CWT | CAES=1，但 CWT=0 | 默认两者同时关闭；需要电流唤醒时一起配置并测试 |
| P1 | 0x79 INT mask | 0x00 | 0 表示不屏蔽，不是“关闭中断”；不用 INT 时建议显式 0xFF |
| P1 | GP2/GP3 | 默认 NTC | 由 BOM/profile 决定；未装外部 NTC 时建议 OFF |
| P1 | 0x53/0x54 | 大量位依赖 reset | 建立显式 MOS mask policy，不靠复位默认值决定安全行为 |
| P1 | I2C WDT | 关闭且 timeout 不关 FET | 由产品安全策略决定；若启用必须与软件 inhibit、CHG/DSG mask 联动 |
| P1 | OC2 量化 | requested 15A | 200uΩ 下硬件最小约20A，必须报告 effective=20A 或拒绝不合适的产品请求 |

---

# 2. 证据来源与可信度规则

本报告采用以下优先级：

1. **DVC1124-2 参考手册 V1.2**：寄存器地址、bit、reset、公式、RC/W0C/self-clear 的最高依据；
2. **DVC1124-2 数据手册 V1.1**：I2C 时序、地址模式、系统行为、休眠/唤醒等系统级依据；
3. **HS-D008-24S100A-V1 原理图**：板级连线、MCU、AFE、分流器、NTC、FET 输出方式的依据；
4. **D013 当前分支源码**：软件框架和 fail-safe 设计参考，不覆盖 DVC 手册/原理图；
5. **D008 当前源码**：待审计对象；
6. 历史代码、DemoCode、旧产品参数：只能作为辅助，不可覆盖前述资料。

本文状态标签：

- **✅ 正确**：手册 + 当前源码（必要时加原理图）一致；
- **⚠️ 编码正确/产品值未闭环**：寄存器写法正确，但默认值仍需 BOM/产品/实板签核；
- **❌ 错误或不匹配**：当前配置与板级事实/手册含义冲突；
- **⛔ 发布阻断**：功能可保持关闭，但如果产品安全目标依赖它，未验证不得量产；
- **TODO_VERIFY_HW**：资料不足，禁止 AI 猜值。

---

# 3. HS-D008 硬件基线

## 3.1 产品装配

HS-D008 原理图明确同时支持：

- **24 串磷酸铁锂**；
- **20 串三元锂**。

因此“20S NMC”不是只把 `cell_count=20`。必须整体切换 chemistry/profile，包括 COV/CUV、pack OV/UV、SOC OCV、满/空端点、温度策略、充电器/整车限压、GP2/GP3 装配和容量参数。

## 3.2 MCU / AFE / I2C

- MCU：TLSR8251F512ET32；
- AFE：DVC1124（原理图未给出 -22/-24 精确丝印）；
- MCU PC0 -> SDA；PC1 -> SCL；
- DVC SRP/SRN 用于电流采样，而不是级联地址硬线模式；
- 典型 I2C 采用 0x40 写 / 0x41 读（8-bit transfer address），100kHz，CRC8。

**结论：D008 使用 fixed-address 模式是正确的。** DVC1124-22 与 -24 的唯一型号差别是“级联硬线地址占用的 GP 数量”，在 D008 这种 SRP/SRN 正常电流采样、固定地址拓扑下，软件 `MODEL_22` 不影响当前 I2C 地址行为；但产品文档仍应按 BOM/芯片丝印确认真实型号。

## 3.3 分流器

原理图包含 RS1..RS10 共 10 个 2mΩ 分流器并联：

```text
Rshunt = 2mΩ / 10 = 0.2mΩ = 200uΩ
```

因此 `DVC1124_DEFAULT_SHUNT_UOHM = 200` 与原理图一致。

由此直接得到：

```text
CC2 1 LSB = 0.3125uV / 200uΩ = 1.5625mA
OC1 1 code = 0.25mV / 0.2mΩ = 1.25A
OC2 1 threshold step = 4mV / 0.2mΩ = 20A
SCD 1 code = 10mV / 0.2mΩ = 50A
CWT 1 code = 10uV / 200uΩ = 50mA
BDPT 1 code = 40uV / 200uΩ = 0.2A
```

这组换算应作为 D008 单元测试 Golden Vector。

## 3.4 FET 输出拓扑

原理图中 DVC 的高边主 `CHG`、`DSG` 引脚标为 **NC**；项目实际使用：

```text
GP5 -> GP5-CHG -> low-side CHG
GP6 -> GP6-DSG -> low-side DSG
```

因此 DVC `0x75` 中 GP5/GP6 配成 `111` 是正确的；反过来，`0x55 HSFM=0` 允许高边驱动与 D008 的实际拓扑不一致。建议 D008 产品默认改成：

```c
DVC1124_DEFAULT_HIGH_SIDE_FET_MASK = 1u;
```

即显式屏蔽不用的高边 FET 驱动。

![HS-D008 DVC1124 区域：主 CHG/DSG 为 NC，GP5/GP6 为低边 CHG/DSG](assets/d008_dvc_crop.png)

## 3.5 GP / NTC

- 原理图存在 NTC1、NTC2，型号 `SNC103B13435F0603E`；
- 当前项目将 Battery NTC=GP4、MOS NTC=GP1；这一路径与现有板级整理一致；
- GP2/GP3 引到外部 CN4，当前软件默认两者都是 NTC，但原理图本身不能证明每个产品 BOM 都装了外部 NTC。

所以 GP2/GP3 必须进入产品 Profile：

```text
D008_24S_LFP:  GP2/GP3 = 按该 BOM
D008_20S_NMC:  GP2/GP3 = 按该 BOM
未安装         -> OFF / high-Z
已安装 NTC     -> NTC
```

禁止让通用 DVC driver 永久假设“所有 D008 都有 4 个外部 NTC”。

---

# 4. D013 框架与 D008 当前框架差异

D013 当前分支已经在 D008 Phase-1 基础上继续演进。需要迁移的是**通用框架能力**，而不是 D011/SH3673510 的硬件值。

| 能力 | D008 Phase-1 | D013 当前 | D008 目标 |
|---|---|---|---|
| AFE compile-time backend | `bms_afe.h` 已有，但 backend 选择不统一 | `bms_afe_backend.h` 明确 DVC/SH backend | 增加 backend header，默认 DVC |
| App 依赖方向 | 已基本只用 `bms_afe.h` | 同样保持 generic AFE boundary | 保持 |
| 板级配置 | `dvc1124_project_config.h` | `sh3673510_project_config.h` | 保持 DVC board config，增加 D008 product profile |
| 全局 BMS 状态 | `bms_state.*`/`bms_error.h` 已有 | 继续收口 | 同步 D013 改进 |
| AFE 通信失效 | 置 AFE error，缺完整 inhibit/release | output inhibit + valid-snapshot streak + reinit | **移植到 DVC** |
| MOS 请求 | 简单 target + fault clamp | request × output permit × comm health × directional fault | **按 D013 思路实现 DVC FET arbiter** |
| 短路恢复 | DVC alarm + 条件清除 | 独立 latch/release 状态机 | 做 DVC 版本，不能关断后零电流立即重开 |
| SOC profile | 主要由历史宏/算法内部常量组成 | `bms_soc_defs.h` + `bms_soc_profile.h` | 引入 24S LFP / 20S NMC profile |
| 产品身份 | 仍有 D3PRO 等大块历史条件 | 参考分支已逐步单产品化 | D008 独立 product profile |
| 文档 | DVC 文档较完整 | 有硬件 Reference + Development Status | D008 建同等证据化文档 |
| 测试 | DVC/Flash/CI contract | 又增加板型 integration/fail-safe tests | 增加 D008 integration + golden vectors |

## 4.1 D013 可直接借鉴的 fail-safe 模式

D013 的 SH adapter 已经使用以下模式：

```text
通信/CRC/超时错误
    -> output_inhibit = 1
    -> valid_snapshot_streak = 0
    -> FET 关断/禁止重新开启
    -> 触发 AFE 重初始化（带 cooldown）

AFE 重配成功
    -> 连续若干完整有效快照
    -> 才解除 output_inhibit
```

D008 应采用同样的**行为框架**，但底层恢复动作换成 DVC 事实：

```text
I2C retry / bus reset
 -> 必要时 MCU-AFE-EN(PD7) power cycle
 -> CST reset registers
 -> 等待 settle
 -> probe CV + FRT
 -> apply board config
 -> apply protection
 -> restore DVC KV
 -> 连续 3 帧完整有效采样
 -> release inhibit
```

---

# 5. 推荐的 D008 最终软件分层

```text
app.c / BLE / UART / Modbus / SOC
                  |
                  v
             bms_afe.h
                  |
      compile-time backend select
                  |
                  v
          dvc1124_bms.c
    BMS映射 / fault / FET arbiter
                  |
                  v
             dvc1124.c
    I2C/CRC/寄存器/换算/采样
       |                    |
       v                    v
 dvc1124_special.c   dvc1124_config_service.*
 RC/W0C/self-clear    semantic transaction
                            |
                            v
                   dvc1124_config_store.*
                            |
                            v
                        flash_kv32
```

严格职责：

- `dvc1124_reg.h`：只放芯片真值；
- `dvc1124_project_config.h`：只放 D008 板级/装配默认；
- `d008_product_profile.h`（建议新增）：放 24S LFP / 20S NMC 的产品参数选择；
- `dvc1124.c`：最终不应直接写 `g_tParam` / `g_stCellInfoReport`；
- `dvc1124_bms.c`：负责单位映射、fault、FET、BMS 参数转 AFE config；
- `bms_state.*`：唯一 BMS report/error/fault-history 所有者；
- BLE/UART 不编码 DVC register bit，只调用 semantic service。

---

# 6. 产品 Profile：必须从“板级”与“化学体系”分离

当前 `conf.h` 仍以 `FD_BMS_TYPE=D3PRO` 开始，再局部覆盖 `SeriesNum` 和硬件版本。这会让 D008 无意继承旧产品容量、保护和通信身份。应删除这种“先选旧产品、再 override D008”的做法。

建议：

```c
#define BMS_PRODUCT_D008_24S_LFP  1
#define BMS_PRODUCT_D008_20S_NMC  2

#ifndef BMS_PRODUCT_PROFILE
#define BMS_PRODUCT_PROFILE BMS_PRODUCT_D008_24S_LFP
#endif
```

每个 profile 至少明确：

- chemistry；
- series count；
- capacity_factory（由产品数据，不由原理图猜）；
- COV/CUV 与 recovery；
- pack OV/UV；
- charge/discharge OC 软件三级；
- DVC hardware OC1/OC2 requested；
- temperature policy；
- SOC OCV 表与满/空端点；
- charger/vehicle limit；
- GP2/GP3 population；
- device name / hardware version / product ID。

**禁止仅把 `DVC1124_DEFAULT_CELL_COUNT` 24 改成 20 就称为完成 20S NMC。**

---
