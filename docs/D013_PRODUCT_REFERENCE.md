# D013 产品硬件与固件配置基线

> 适用分支：`codex-bms-monorepo`；2026-10-10 更新 TS4 MOS 温度配置与影响审查。
>
> **重要证据限制：当前可访问的用户资料中没有找到 D013 专属原理图。** 因此本文把“当前源码事实”和“原理图已验证事实”严格分开。D013 的 GPIO 映射目前只能写成 **CODE（D011 派生）**，不能写成 D013 硬件事实。获得 D013 原理图前，禁止把 D011 引脚表直接视为 D013 已核对连接。

> 2026-10-06 资料入口：[AFE 使用指南](AFE_REFERENCE_GUIDE.md)、[项目内原资料库](../references/afe/README.md)、[固定提交审查](afe-audit/20261006-0aaa8429/README.md)。本轮审查 SHA 为 `0aaa8429`；报告问题尚未实施修复，原始型号/BOM/实板缺口不因导入手册而关闭。

## 1. 证据优先级

1. D013 专属原理图/BOM：**当前缺失，待用户补充/重新定位**。
2. SH36735XX CV1.0A：AFE 寄存器、SPI、保护和状态机事实。
3. 当前 D013 分支源码：当前实际编译配置。
4. D011 原理图只能解释代码来源，不能证明 D013 板连接。

2026-10-10 用户明确指定 SH3673510 TS4 测量 MOS 温度，并确认实装 NTC 为 10kΩ / B3435。
该连接/物料按用户确认记录；其他板级 IO 仍为“源码继承、未原理图确认”，整板批准标志保持 0。

## 2. 当前源码产品身份

`bms/products/d013/bms_product.h`（编码公式在共享 `sh3673510_project_config.h`） 当前明确写入以下产品 profile：

| 项目 | 当前源码事实 | 硬件证据状态 |
|---|---|---|
| MCU | TLSR8251F512ET32 | 未取得 D013 原理图复核 |
| AFE | SH3673510 | 未取得 D013 原理图复核 |
| cell count | 默认 10；构建可覆盖，须核对实际编译参数 | CODE；不是实板确认 |
| shunt | 100 µΩ | CODE；注释为 2 mV : 20 A -> 0.1 mΩ |
| SPI group | PB6 MISO / PB7 MOSI / PD7 SCLK / PD2 CS | CODE，沿用 D011 配置 |
| NTC nominal | 10K；TS4 用作 MOS NTC | TS4 10kΩ / B3435 为用户确认；TS1/TS2 物料仍待核 |
| Modbus transport | direct UART，`BMS_PRODUCT_RS485_ENABLE=0` | CODE |

### 2.1 产品身份与能力

硬件版本为 `D013`，默认序列号 `D013-UNSET`，BLE 为 `BT_D013` / `BT_D013_FACTORY`。
历史 numeric wire ID 保持 2，内部存储 tag 为 13；本次不改变外部协议数值。
`BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH=116` 等继承默认仍待产品签核，不因身份更名变成已验证参数。

当前 profile 的 heater、balance 和 heater NTC 仍不支持。
`SH3673510_PRODUCT_MOS_NTC_SUPPORTED=1`，通过共享索引 3 将 TS4 作为必需 MOS 温度。
`mos_temp_required=1`，TS4 无效会触发温度断线并阻断充放电；有效时参与独立 MOS OTP。
TS1/TS2 仍为必需电池温度，任一无效仍阻断输出；TS4 不计入电池温度 min/max。
用户确认不等于测温精度、过温动作或 Gate 实板验收；完整影响见第 11 节。

## 3. MCU IO：当前源码映射（未获 D013 原理图验证）

`bms/products/d013/bms_product.h`（编码公式在共享 `sh3673510_project_config.h`） 使用中性 `BMS_BOARD_*` 宏名；引脚值继承 D011，仍待 D013 原理图确认。以下只表示 D013 分支当前会按这些 GPIO 编译：

| GPIO | 当前源码宏 | 当前源码用途 | D013 原理图状态 |
|---|---|---|---|
| PD4 | `BMS_BOARD_CMNT_EN_PIN` | communication enable | **未验证** |
| PD7 | `BMS_BOARD_AFE_SCLK_PIN` | AFE SPI SCLK | **未验证** |
| PA0 | `BMS_BOARD_SWITCH_PIN` | switch input | **未验证** |
| PA1 | `BMS_BOARD_RS485_EN_PIN` | RS485 direction | **未验证；且 D013 conf 当前 `BMS_PRODUCT_RS485_ENABLE=0`** |
| PA7 | `BMS_BOARD_SWS_PIN` | SWS debug | **未验证** |
| PB1 | `BMS_BOARD_INT_WK_MCU_PIN` | interrupt/wake input | **未验证** |
| PB4 | `BMS_BOARD_HEATER_CHG_PIN` | heater control | **未验证** |
| PB5 | `BMS_BOARD_HEATER_FUSE_TRIGGER_PIN` | fuse trigger，safe level 0 | **未验证；不得在 D013 上假设存在同一不可逆硬件** |
| PB6 | `BMS_BOARD_AFE_MISO_PIN` | SPI MISO | **未验证** |
| PB7 | `BMS_BOARD_AFE_MOSI_PIN` | SPI MOSI | **未验证** |
| PC0 | `BMS_BOARD_AFE_ALARM_PIN` | AFE ALARM | **未验证** |
| PC1 | `BMS_BOARD_AFE_RESET_OUT_PIN` | AFE RESET network | **未验证** |
| PC2 | `BMS_BOARD_SCI1_TX_PIN` | UART TX | **未验证** |
| PC3 | `BMS_BOARD_SCI1_RX_PIN` | UART RX | **未验证** |
| PC4 | `BMS_BOARD_DEBUG_LED_PIN` | debug LED | **未验证** |
| PD3 | `BMS_BOARD_CMNT_WK_PIN` | communication wake | **未验证** |
| PD2 | `BMS_BOARD_AFE_CS_PIN` | SPI CS | **未验证** |

### 3.1 结论

在 D013 原理图缺失的当前状态下：

- 不能声称上述 GPIO 与 D013 PCB 一致；
- 不能声称 PB5 是 D013 的保险丝熔断输出；
- TS4 MOS 用途和 10K-3435 为用户确认；具体贴装位置、热耦合和布线仍待资料/实测；TS3 未启用；
- 不能声称 D013 存在 D011 的隔离 RS485、C-3V3、CMNT-WK 电路；
- 任何 D013 实板测试前应先补 D013 原理图并逐网核对。

## 4. SH3673510 AFE：当前源码静态配置

AFE 寄存器模型来自 SH36735XX CV1.0A；当前 D013 与 D011 共用 `sh3673520_reg.h` 和 `sh3673510_control.c`。这在芯片系列层面有手册依据；板级配置仍需 D013 原理图确认。

解析当前 `bms/products/d013/bms_product.h`（编码公式在共享 `sh3673510_project_config.h`）：

| 寄存器 | D013 当前静态值 | 关键字段 |
|---|---:|---|
| SCONF1 0x40 | `0x00` | Normal |
| SCONF2 0x41 | `0x10` | PD_EN=0、PD_CTL=0、PUMP_EN=1；PDSGMOS/DSGMOS/CHGMOS boot=0 |
| SCONF3 0x42 | `0x44` | CGR_WK=1、LD_WK=OFF、CRLD_EN=CPLUS、OWD_EN/TRG=0 |
| SCONF4 0x43 | 默认 `0x6A` | PDSGT code3=490 ms，CN=10；构建覆盖后随有效串数变化 |
| SCONF5 0x44 | 初始 `0x3C` | MOS_EN=1、OCC_EN=1、CADC_EN=1、WDT_EN=1、WDT code0 |
| SCONF7 0x46 | `0x04` | RLD=0、CADCT=4S、CDV=4 |
| OWV/ALARMH 0x47 | `0x57` | OWV code5、LOADOFF/VADC/CADC interrupt=1 |
| ALARML 0x48 | `0xFF` | WK/WDT/OWD/TEMP/OCC/OCD/UV/OV interrupts enabled |

D013 与 D011 的关键静态 AFE 差异在当前源码中主要是：

- 默认 `CN=10`（与 D011 相同）；串数不是当前默认差异；
- `Rsense=100 µΩ`（D011 为 250 µΩ）；
- 产品配置选择 direct UART。

所有板级输入现在位于 D013 产品头文件，共享后端只组合寄存器。

## 5. 运行时硬件保护配置

SCONF6 和 SCONF5/OCC 的最终 enable 状态来自独立 AFE hardware profile：

- COV -> OV_EN；
- CUV -> UV_EN；
- OCD1/OCD2 -> OCD_EN；
- SC -> SC_EN；
- TEMP -> TS1_EN + TS2_EN；
- OCC1 -> SCONF5 OCC_EN。

TS4 测量与 `SCONF6.TS4_EN` 是不同功能。TS4 硬件保护位保持 0：芯片共用电池
OTC/OTD/UTC/UTD 阈值，无法表达独立 MOS 75/85/95°C 软件策略；本次只启用已有软件路径。

因此文档不把 SCONF6 写成永远固定 `0x3F`。若当前 persisted `enable_mask` 全开到 COV/CUV/OCD/SC/TEMP，则运行时会得到相应 0x3F 组合；实际设备必须以 profile + readback 为准。

## 6. SH3673510 硬件保护量化（当前代码）

D013 与 D011 使用同一量化实现，但因为 Rsense 不同，**同一个寄存器 sense-voltage code 对应的电流不同**：

| 项目 | 芯片/代码量化 |
|---|---|
| OV/UV | 5 mV/code；delay 使用手册离散档位 |
| OCD1 | (code+1)×5 mV sense；delay使用 8 档表 |
| OCD2 | (code+1)×10 mV sense；delay=(code+1)×25 ms |
| OCC | (code+1)×1.375 mV sense；delay使用 8 档表 |
| SC | effective OCD2 × {2,3,4,6}；delay={2,4,8,16,32,64,128,256} µs |
| HW Temp | 10K NTC table -> divider code |

当前 D013 `Rsense=100 µΩ` 来自源码 profile，因此 effective current 计算必须使用 100 µΩ；不得继续套 D011 的 250 µΩ 电流范围/限制。

## 7. WDT / Powerdown / Sleep

- 当前静态配置 `WDT_EN=1`，WDT code0；SH36735XX CV1.0A 对应约 32.34 s。
- 手册说明 WDT overflow 后还有 FLAG2/WDT_FLG 清除窗口，条件满足才进入 Powerdown；不能简化成“32 s 必然断电”。
- SLEEP `SCONF1=0xAA` 会关闭 CADC、WDT、保护、FET、charge pump、balance；唤醒受 CGR_WK/LD_WK 配置影响。
- D013 当前代码配置 `CGR_WK=1`、`LD_WK=OFF`。

这些是 AFE 芯片/源码事实；D013 外部 charger/load 网络是否与 D011 相同仍需原理图确认。

## 8. 软件保护与 AFE 硬件保护

- 软件保护统一使用 `g_bms_protection_params`：First / Second / Third / Recover / Filter。
- AFE 硬件保护使用独立 `bms_afe_hw_profile_t`，没有三级概念。
- 两套参数独立持久化、独立修改；普通软件参数写入不得改硬件 profile。
- AFE 参数使用 requested/effective 分离、完整 35-word 原子写、固件 validate/persist/apply/readback/rollback；rollback 失败报告 `CONFIG_INCONSISTENT`。

## 9. 当前 D013 必须优先关闭的文档/硬件缺口

1. **补 D013 原理图/BOM**，逐一确认 MCU GPIO、SPI、RESET、ALARM、UART、开关、加热、唤醒、LED。
2. 明确 D013 实际 AFE 型号和封装，确认就是 SH3673510。
3. 用硬件资料确认实际串数及 cell wiring、未使用通道处理方式。
4. 确认 100 µΩ shunt 的实际物料/并联结构、Kelvin 取样、方向和功率。
5. TS4 已按用户确认的 MOS 10K-3435 启用；补厂家 R–T 表、位置/热耦合和温度点/开短路实测。TS1/TS2 物料及 TS3 实际连接仍待资料核对，TS3 capability 保持禁用。
6. 中性宏和 D013 身份已完成整理；引脚值仍需逐网验证，命名清理不等于硬件确认。
7. 签核 D013 容量、OV/UV、OC、SC、温度、SOC profile/端点；当前 D11 历史默认不能作为 D013 量产值。

## 10. 当前权威源码入口

- `bms/products/d013/bms_product.h`：当前 D013 编译身份/通信模式，保留既有 numeric wire ID，字符串已改为 D013。
- `bms/products/d013/bms_product.h`：默认 10S/100µΩ 和当前继承 IO/AFE 静态配置。
- `bms/afe/sh3673510/sh3673520_reg.h`：SH36735xx CV1.0A 寄存器/协议真值。
- `bms/afe/sh3673510/sh3673510_control.c`：硬件保护量化、静态配置、FET/温度处理。
- `bms/afe/sh3673510/sh3673510_bms.c`：BMS适配、保护恢复、AFE communication fail-safe。
- `bms/core/bms_sw_protection.*`：软件三级保护。
- `bms/core/bms_afe_hw_profile.*`：独立 AFE HW profile。

获得 D013 原理图之前，本文不会把任何 D011 原理图事实复制成 D013 硬件事实。

## 11. TS4 MOS 温度接入与影响审查（2026-10-10）

审查基线为 `307eb9d44e06584f192e2e6612f6fdc131cdf78b`，加本次 D013 能力变更。
唯一固件变更是 D013 产品能力由 0 改为 1，复用当前共享采样/保护，不修改其他产品、保护默认或 AFE 寄存器策略。

### 原厂依据和采样链

对照原件 [SH36735XX CV1.0A](../references/afe/AFE_完整开发参考库_20261005/sources/SH36735XX%20CV1.0A.pdf)
物理页 19（四路温度及换算公式）、20（温度转换时序）、34（SCONF6 温度保护开关）、38（共用 OTC/OTD 阈值）。
实际型号按用户指定 SH3673510；硅修订和整板图纸尚未核验，未混用旧 CV0.2C 电气保证。

`sources.txt` 选择共享 SH 后端；`SH3673520_ReadTemperatures()` 原本就连续读取四路外部温度及内部温度，
因此启用 D013 TS4 不增加 SPI 读取事务。`publish_measurements()` 在 VADC 完成后取 `external_raw[3]`，
由 `SH3673520_NtcRawToOhm()` 按 `raw * 10000 / (32768 - raw)` 得到 Ω，再用现有 10K 表插值，
发布到 `g_bms_report.temperature_x10[MOS_TEMP1]`。原始码必须为 0..32767，电阻必须为 500..300000Ω；
分母先校验、乘法上限 327670000，索引 3 位于四通道数组内。断线/短路等无效读数不伪装为温度。

现有表使用 100Ω 电阻分辨率，10kΩ 对应 25°C，1.1kΩ 对应 95°C，低阻端钳位到 105°C。
实装规格已确认，但厂家受控 R–T 曲线、物料误差和热耦合仍需实测；不能把查表结果当作温箱精度证明。

### 已有 MOS 保护与参数生效

| 项目 | 编译默认 | 现有行为 |
|---|---:|---|
| First | 75°C / 编码 1150 | 一级告警，不直接关 MOS |
| Second | 85°C / 编码 1250 | 二级告警，不直接关 MOS |
| Third | 95°C / 编码 1350 | `mos_otp` 同时阻断 CHG/DSG，无充放电电流前提 |
| Third Recover | 80°C / 编码 1200 | 连续满足 `<=80°C` 的滤波观察后解除 |
| Filter | 100 × 10ms | 200ms 应用周期下为 5 次观察；超限累积、正常递减，恢复要求连续 |
| TS4 无效 | `BMS_ERROR_TEMP_BREAK` | 有效采样中发现即阻断两方向，不等待上述 5 次；重新有效后清断线位 |

First/Second 离开各自阈值后按 Filter 清除；不是用 80°C 恢复值。
已建立 MOS OTP 后 TS4 暂时失效，公共算法保留 OTP 锁存并清触发/恢复计数；恢复到 80..95°C 区间不能清旧锁存。
该保证适用于持续运行中的传感器失效，不包括下文的完整 AFE 重初始化或 MCU reset。

阈值来自 `bms_config_store.c::s_default_protection`，实际运行以 `g_bms_protection_params` 为准。
MOS 能力是编译配置，装入新固件即启用，不依赖 OTA 参数组选中；已有 schema 3 Flash 的 MOS 阈值原样加载。
若原持久参数的 `mos_otp_third_x10=0`，现有算法将三级 MOS OTP 视为关闭，TS4 断线保护仍有效。
需要上述默认时须按 [OTA 参数](OTA_PARAMETERS.md) 显式选择 `sw`，该选择会更新整组软件保护；本次没有强制覆盖或生成更新包。

### 对其他模块的影响

| 范围 | 审查结论 |
|---|---|
| 最终 MOS 命令 | 软件 Third / TEMP_BREAK → 两方向 blocked → `sh3510_apply_requested_fets()` → SCONF2 两 FET 请求为 OFF；双阻断不会走单侧反向放行 |
| 电池温度 | TS1/TS2 min/max 和电池充放电高低温独立；MOS 过热不会冒充电池过热 |
| 协议/上位机 | 现有 MOS 温度槽及 Modbus `0xD127` 开始返回 TS4 温度，编码仍为 `(°C+40)*10`，无效为 0；既有诊断提供支持/有效位、raw、Ω、温度和阻断原因，无新增地址或格式。客户端实际显示未运行验收 |
| 故障记录 | 复用已有 MOS OTP 三级故障位与上升沿历史记录，无新增 Flash 布局；首次发生 MOS 告警可能新增正常历史事件 |
| SOC | 温度输入仍只用电池 min/max；MOS Third 通过既有温度故障掩码阻止静置 OCV 修正，电流积分仍按既有采样资格执行 |
| heater/balance/open-wire | D013 heater/balance 能力仍为 0，TS3 不启用；不修改电芯断线检测流程或物理恢复资格 |
| 低功耗/通信 | 不新增低功耗入口或唤醒源，保留固定 UART 门禁、watchdog bus silence、三次新样本资格；深睡期间不能依赖 MCU 软件温度保护持续运行 |
| 资源与并发 | 复用已有四通道数组和公共保护状态，无新增 ISR、动态分配、阻塞等待或持久化参数；只多编入已有 MOS 分支，实际代码尺寸未链接测量 |
| 其他产品 | 固件源只修改 D013 产品头，D008/D011/D014 的能力和公共实现不变 |

### 已有边界与未关闭风险

1. **软件保护不等于独立硬件 MOS OTP。** `TS4_EN=0` 保留，禁用 `SH3673510_SW_PROTECT_ENABLE` 的开发隔离构建也会清除 MOS OTP 和 TEMP_BREAK。生产构建原门禁拒绝软件保护关闭，本次未改变。
2. **响应不是保证 1 秒。** 原手册 Normal 温度转换周期约 0.98s，IDLE 更慢；软件按应用观察计数，VADC ready 不等于每次 TS4 都重新转换。1s Filter 是名义滤波，实际响应还受温度刷新、调度和热惯性影响；新代码没有增加独立 TS4 freshness 证据。
3. **AFE 完整重初始化会清 MOS 温度锁存。** 当前 `bms_sw_protection_init()` 只保留要求物理恢复证据的三级过流，MOS OTP 会重建。若先触发 95°C 保护，随后发生重初始化且温度在 80..95°C，旧锁存不再阻断；MCU reset 同样不保留。启动输出禁止和重新采样资格仍存在，但不等于保留 80°C 恢复门槛。这是公共层已有策略，未在本次产品能力修改中扩大为四产品重置策略变更。
4. **OFF 命令不等于物理 Gate 已关。** 保留 SCONF5.MOS_EN 和芯片自主行为；需要分别核对软件请求、AFE 状态、Gate/Vgs 及正反向电流。SPI 写失败仍交现有通信门禁和硬件 watchdog 处理。

### 本次检查与后续验收

本次仅完成源码/调用链/异常路径与 Git 差异审查；更新已有 `sh_register_scenarios_host_check.py`
的 D013 必需 MOS 输入期望并补 TS4 索引约束。已有 `sw_temperature_groups_host_check.py` 覆盖有效/断线、双向阻断、
过温恢复及失效时锁存保留。本次未运行这些用例、`sources --check`、静态分析、目标编译/链接、资源检查、BIN 或 OTA。

获准验证后先执行 D013 相关 host 与链接检查，再在实板核对：常温/75/85/95/80°C、TS4 开短路、
过温后断线再接回 80..95°C、零电流仍触发 MOS OTP、各持久参数/OTA 保留模式、重初始化/MCU reset，
以及 Modbus/客户端显示与 CHG/DSG Gate/Vgs。实板结果和产品阈值签核仍未完成。
