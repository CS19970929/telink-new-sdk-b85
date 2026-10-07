> 历史记录：仅代表本文标注的日期和固定提交。当前实现见 [文档导航](../README.md)，当前验证以本轮报告为准。

# 四产品当前配置与 common 分支替换边界

本页存储与资源表是注明基线的历史快照；2026-10-06 后续业务布局、schema 3、老化删除及 SOC/PM 以 [STORAGE](../STORAGE.md) 为准。

核对日期：2026-10-05。源码基线：`b3b5e9d0882bdde2358881753b539b8432cc72d9`，分支 `codex-bms-monorepo`。
本报告记录注明基线的代码默认值，不代表设备读回值或量产签核值；末尾后续验证节另有独立提交。当前交接操作见 [配置指南](../CONFIGURATION_AND_BUILD_GUIDE.md)。后续修改参数后应重新核对，不能把历史资源表当当前构建结果。

## 1. 可以如何使用当前分支

四个产品可以统一从本分支开展后续固件开发；公共代码只维护 `bms/core`、`bms/app`，芯片代码在 `bms/afe`，产品输入在 `bms/products/<product>`，Telink 平台在 `bms/platform/telink`。
旧 common 分支保留作历史和已有交付版本，不再复制、双向同步公共源码。功能分支可以从 monorepo 创建，完成四产品验证后合回；“统一开发”不要求所有人直接提交同一长寿命分支。

**当前不能承诺直接烧录即等价替换四个 common 分支的所有版本，更不能称为四款产品均已量产验收。** 原因包括：

- D008/D011/D013 的原始导入提交对象没有发布，现有证据不足以证明与各 common 最新版本逐项等价。可下载的导入结果为 `fd50730`，详见 `bms/products/baselines.json`。
- CFG2 schema 2 明确拒绝旧格式和其他产品的记录，不迁移旧开发板保护、容量、SOC 或校准配置；首次启动可能采用下面的默认值。有效同产品新格式记录按各类更新编号决定保留或写入默认值，见 [OTA_PARAMETERS.md](../OTA_PARAMETERS.md)。
- 四产品容量和保护阈值仍有继承默认，D013 缺专属原理图/BOM，实板保护、休眠、掉电和 OTA 尚未逐产品签核。
- D008 20S NMC 的 profile 只改变串数和 SOC 化学体系，保护阈值仍与 LFP 默认相同；D008 默认 SC enable 位为 0。这两项必须按真实产品要求决定，不能由工具擅自填写电流或电压。
- 旧工作树未提交修改、实验分支和 common 分支的其他提交不会自动进入本分支。切换开发前保留旧 release/commit，并逐项处理仍需保留的差异。

2026-10-05 查询到的 common 分支头：

| 产品 | 分支 | 远端 commit |
|---|---|---|
| D008 | `refactor/d008-common-bms-features` | `0b8385c560b8a93ab49328ecd883a56e64d7043c` |
| D011 | `refactor/d011-common-bms-features` | `e53404254e048fac7891524764b749096d49ecd6` |
| D013 | `refactor/d013-common-bms-features` | `9bdc0c4a255924d2a21f24da4f94c51ad981c507` |
| D014 | `refactor/d014-common-bms-features` | `25ced678dd31601cd96834c9f63901bd58e6c621` |

这些是核对锚点，不是“所有差异已经合并”的声明。Windows 上位机继续以 `feature/windows-afe-hw-protection-editor-v2/bms-tool-windows/` 为维护来源。

## 2. 产品、能力与身份

四产品均按 TLSR8251、32 KiB SRAM、`MCU_STARTUP_8251` 构建。

| 项目 | D008 | D011 | D013 | D014 |
|---|---|---|---|---|
| AFE | DVC1124-2 / MODEL_22 | SH3673510 | SH3673510，硬件待核 | SH3673510 |
| 串数 | 16S LFP / 20S NMC / 24S LFP | 10S | 4S | 8S |
| 开发默认 | 16S LFP | 固定产品输入 | 固定产品输入 | 固定产品输入 |
| Rsense | 200 µΩ | 250 µΩ | 100 µΩ | 667 µΩ |
| 容量原始值，0.1 Ah | 78 | 116 | 116 | 116 |
| 当前默认容量 | 7.8 Ah | 11.6 Ah | 11.6 Ah，继承值 | 11.6 Ah，继承值 |
| SOC chemistry/profile | 所选 LFP/NMC generic | AUTO/AUTO | AUTO/AUTO | AUTO/AUTO |
| 加热能力 | 支持 | 支持 | 禁用 | 禁用 |
| 均衡能力 | 支持 | 支持 | 禁用 | 支持 |
| 电池温度 | GP2、GP3，均必需 | TS1、TS2，均必需 | TS1、TS2，均必需 | TS1、TS2，均必需 |
| Heater 温度 | GP1 | TS3 | 不支持 | TS3 NC |
| MOS 温度 | GP4，必需 | TS4，必需 | 当前不支持 | TS4，必需 |
| AFE 外部温度硬件保护 | 无此 profile 能力 | 仅 TS1、TS2 | 仅 TS1、TS2 | 仅 TS1、TS2 |
| MCU 主通信 | UART / SIF 总线复用 | RS485 Modbus | 直连 UART Modbus | 隔离 RS485 Modbus |
| wire product ID | 12 | 2 | 2 | 2 |
| 内部存储 product tag | 8 | 11 | 13 | 14 |
| 软件版本字符串 | V8.8 | V1.0 | V1.0 | V1.6 |
| 默认序列号 | D008-20260930 | D011-UNSET | D013-UNSET | D014-20260925 |
| 默认 BLE 名 | BT_FD190126F03200046_007 | BT_D011 | BT_D013 | BT_D014 |
| 工厂 BLE 名 | BT_FD260228F03200046_666 | BT_D011_FACTORY | BT_D013_FACTORY | BT_D014_FACTORY |

SH 的 AUTO 依据当前软件 COV=3750 mV 选择 generic LFP；不是自动识别真实电芯。D008 选择 20S NMC 不改变容量、均衡起点或保护默认值。
D013 的 capability 表达源码支持范围，不能据此证明实物没有 TS4 或均衡硬件。
用户参数中的 `heater_enable=1`、`balance_enable=1` 仍受产品 capability 门禁，不能启用 D013/D014 不支持的输出。

## 3. 完整软件保护默认值

来源：`bms/core/param.h` 的 `E2P_PROTECT_DEFAULT_PRT`，产品差异来自各 `bms_product_conf.h`。
First/Second 是报警，Third 才进入软件充/放电阻断；Recover 用于 Third，First/Second 在自身越限消失后清除。
同一组三级使用同一个持久化 Filter；历史 `*_filter1/2` 宏并未进入这份参数记录。

| 保护组 | First | Second | Third | Recover | 参数延时 | 适用 |
|---|---:|---:|---:|---:|---:|---|
| 单体过压 | 3750 mV | 3750 mV | 3750 mV | 3500 mV | 1000 ms | 全部 |
| 单体欠压 | 3000 mV | 3000 mV | 2200 mV | 3100 mV | 1000 ms | D008 |
| 单体欠压 | 3000 mV | 3000 mV | 3000 mV | 3100 mV | 10000 ms | D011/D013/D014 |
| 总压过压 | 3.50×N V | 3.60×N V | 3.65×N V | 3.50×N V | 1000 ms | N=所选串数 |
| 总压欠压 | 3.00×N V | 3.00×N V | 2.90×N V | 3.00×N V | 1000 ms | 全部 |
| 充电过流 | 10 A | 15 A | 20 A | 10 A | 100 ms | 全部 |
| 放电过流 | 10 A | 15 A | 20 A | 10 A | 100 ms | 全部 |
| 充电高温 | 40°C | 50°C | 55°C | 50°C | 1000 ms | 全部 |
| 充电低温 | 5°C | 3°C | 0°C | 3°C | 1000 ms | 全部，包括支持加热的产品 |
| 放电高温 | 50°C | 50°C | 60°C | 50°C | 1000 ms | 全部 |
| 放电低温 | −10°C | −15°C | −20°C | −10°C | 1000 ms | 全部 |
| MOS 高温 | 75°C | 85°C | 95°C | 80°C | 1000 ms | D008/D011/D014；D013 不执行 |
| 单体压差 | 600 mV | 800 mV | 1000 mV | 800 mV | 1000 ms | 产生等级故障，不在充放电直接阻断列表中 |
| SOC Low 参数 | 20% | 10% | 5% | 11% | 1000 ms | 由 SOC 模块执行低电量故障；不直接作为 MOS 关断策略；第一级恢复阈值钳位为 21% |

软件保护每 200 ms 采样，延时向上取整到采样数，所以过流参数 100 ms 实际至少需要一个 200 ms 周期；不能称为 100 ms 硬件响应。故障恢复也按相应过滤周期判定。电池温度的方向保护还受可靠充/放电电流及已有保护保持条件影响；MOS 高温独立于方向。必需 NTC 无效触发 TEMP_BREAK，阻断双向输出。

| 产品/profile | 总压 OV：First / Second / Third / Recover，V | 总压 UV：First / Second / Third / Recover，V |
|---|---|---|
| D008 16S | 56 / 57.6 / 58.4 / 56 | 48 / 48 / 46.4 / 48 |
| D008 20S | 70 / 72 / 73 / 70 | 60 / 60 / 58 / 60 |
| D008 24S | 84 / 86.4 / 87.6 / 84 | 72 / 72 / 69.6 / 72 |
| D011 10S | 35 / 36 / 36.5 / 35 | 30 / 30 / 29 / 30 |
| D013 4S | 14 / 14.4 / 14.6 / 14 | 12 / 12 / 11.6 / 12 |
| D014 8S | 28 / 28.8 / 29.2 / 28 | 24 / 24 / 23.2 / 24 |

D008 的单体欠压 Third=2.2 V 不表示整包一定允许放至每节 2.2 V；总压欠压仍独立生效。

## 4. AFE 独立硬件保护默认值

这里是无有效 CFG2 时构造的 requested profile。软件保护运行时修改不会联动重写 AFE profile。
D008 的初始种子引用编译期软件默认并规范化，之后独立持久化；SH 的默认值直接来自三个产品的 `SH3673510_HW_DEFAULT_*`。

| 字段 | D008 requested | D011/D013/D014 requested |
|---|---|---|
| schema / model | 1 / 0x1124 | 1 / 0x3510 |
| enable mask | 0x003F | 0x00DF |
| COV / 延时 | 3750 mV / 1000 ms | 3750 mV / 1000 ms |
| COV 恢复 / 时间 | 3500 mV / 1000 ms | 3500 mV / 1000 ms |
| CUV / 延时 | 2200 mV / 1000 ms | 3000 mV / 10000 ms |
| CUV 恢复 / 时间 | 3100 mV / 1000 ms | 3100 mV / 10000 ms |
| OCD1 / 延时 | 10 A / 100 ms | 10 A / 100 ms |
| OCD2 / 延时 | 15 A / 100 ms | 15 A / 100 ms |
| OCD 恢复 / 时间 | 9.9 A / 0 ms | 10 A / 2000 ms |
| OCC1 / 延时 | 10 A / 100 ms | 10 A / 100 ms |
| OCC2 / 延时 | 15 A / 100 ms | 不支持；字段 0 / 0 |
| OCC 恢复 / 时间 | 9.9 A / 0 ms | 10 A / 100 ms |
| SC / 延时 / 恢复时间 | 0 / 0 / 0，默认禁用 | 30 A / 256 µs / 2000 ms |
| 充电高温 / 恢复 | 不支持；字段为 0 | 55°C / 50°C |
| 充电低温 / 恢复 | 不支持；字段为 0 | 0°C / 3°C |
| 放电高温 / 恢复 | 不支持；字段为 0 | 60°C / 50°C |
| 放电低温 / 恢复 | 不支持；字段为 0 | −20°C / −10°C |
| 温度恢复时间 | 字段 0 | 1000 ms |

恢复字段并不都是芯片原生寄存器；其中有固件恢复策略。尤其 SC/OCD/OCC 必须满足物理恢复窗口/AFE 状态，不能把“关 MOS 后测得零电流”或“恢复时间=0”解释为立即重新导通。
`HW_PROTECT_ENABLE=1` 只说明硬件保护路径启用，不保证 profile 的每一类保护 enable 位都为 1。

### 4.1 默认参数量化后的预期值

下表来自当前驱动换算。SH 电流、延时和寄存器写入值另用真实 `sh3673510_control_apply_protection()` 函数在内存寄存器桩上执行核对；这不是实板读回。

| AFE 保护 | D008 effective | D011 effective | D013 effective | D014 effective |
|---|---|---|---|---|
| COV | 3750 mV / 1000 ms | 3750 mV / 2030 ms | 同 D011 | 同 D011 |
| CUV | 2200 mV / 1000 ms | 3000 mV / 10010 ms | 同 D011 | 同 D011 |
| OCD1 | 10 A / 96 ms | 20 A / 140 ms | 50 A / 140 ms | 15 A / 140 ms |
| OCD2 | 20 A / 100 ms | 40 A / 100 ms | 100 A / 100 ms | 30 A / 100 ms |
| OCC1 | 10 A / 96 ms | 11 A / 140 ms | 13.8 A / 140 ms | 10.4 A / 140 ms |
| OCC2 | 20 A / 100 ms | 不支持 | 不支持 | 不支持 |
| SC | 默认禁用 | 80 A / 256 µs | 200 A / 256 µs | 60 A / 256 µs |

电流 effective 是固件按 0.1 A 编码的名义值，包含取整；物理动作还受分流器与 AFE 误差影响。
SH 的硬件温度通过 NTC 表和分压 code 量化；公共 effective profile 当前没有把四个温度字段反算成量化后的实际温度，不应把显示的 requested °C 当成独立测得的硬件温度阈值。

## 5. AFE 固定工作配置

### 5.1 D008 / DVC1124

固定配置来自 `bms/products/d008/dvc1124_project_config.h`，AFE reset/init 后重新应用；历史 operating-config Flash 不拥有这些值。

| 项目 | 当前配置 |
|---|---|
| 接口 | I2C PC0 SDA / PC1 SCL，100 kHz；地址 0x40 写 / 0x41 读（7-bit 0x20） |
| GP1..GP4 | NTC：heater、battery1、battery2、MOS |
| GP5 / GP6 | 低边 CHG / DSG；GP123=0x49，GP456=0x7F |
| 高边 FET | 屏蔽；正常请求 CHG/DSG 均 ON |
| 单侧保护 | 对应方向 AUTO_DIODE；共同故障/失联/休眠 hard OFF |
| CADC / CC1 | CADC work=1；CC1 work 4 ms，sleep wake 32 ms |
| VADC | 启用，与 CC2 同步，每 1 CC2，采样 1.54 ms；cell unsigned，额外 cell measurement mask=0 |
| Charge Pump | 10 V |
| V3P3 sleep / work | 1 / 1；timeout restart=0 |
| current wake / timed wake | 均关闭，current wake threshold=0 |
| interrupt mask | 0xFF |
| DSG pull-down | DPC=16 |
| Core OT | 关闭（与外部 NTC 软件保护不同） |
| body diode | 80 µV，200 µΩ 下名义 0.4 A |
| I2C 硬件 WDT | 4 s，timeout close CHG=1、DSG=1 |
| MCU I2C 超时 / 重试 | 5000 µs / 3 次 |
| reset settle | 300 ms |
| 启动零点学习 | 开启；间隔 270 ms；最大绝对电流 1500 mA；样本极差上限 200 mA |

### 5.2 D011 / D013 / D014 / SH3673510

SPI Mode 3，500 kHz；PB6 MISO、PB7 MOSI、PD7 SCLK、PD2 CS。NTC 模型 10K；D011 的 TS3/TS4、D014 的 TS4 实装与图纸阻值差异仍需 BOM 留证；D014 TS3 为 NC，不使用。

| 静态寄存器 | D011 | D013 | D014 | 含义 |
|---|---|---|---|---|
| SCONF1 0x40 | 0x00 | 0x00 | 0x00 | Normal |
| SCONF2 0x41 | 0x50 | 0x50 | 0x50 | PD_EN/PUMP_EN；启动 CHG/DSG/pre-discharge OFF |
| SCONF3 0x42 | 0x44 | 0x44 | 0x44 | charger wake 开，load wake 关，C+ 检测；静态 OWD 关 |
| SCONF4 0x43 | 0x6A | 0x64 | 0x68 | PDSGT 490 ms；CN=10/4/8 |
| SCONF5 0x44 | 0x3C | 0x3C | 0x3C | MOS/OCC/CADC/WDT 开；WDT code0 |
| SCONF6 0x45 | 默认 profile 后 0x3F | 同左 | 同左 | TS1/TS2/SC/OCD/UV/OV；TS3/TS4 硬件位关 |
| SCONF7 0x46 | 0x04 | 0x04 | 0x04 | RLD=0，CADCT=4S，CDV=4 |
| OWV/ALARMH 0x47 | 0x57 | 0x57 | 0x57 | OWV code5=960 mV；LOADOFF/VADC/CADC interrupt |
| ALARML 0x48 | 0xFF | 0xFF | 0xFF | WK/WDT/OWD/TEMP/OCC/OCD/UV/OV interrupt |

SCONF5 的 OCC 位和 SCONF6 最终由已持久化的独立 AFE profile 控制；表中值只适用于默认 profile。SCONF2 的 MOS 位也会在运行时改变。
WDT code0 对应约 32.34 s，存在芯片旗标/后续清除窗口，不能承诺恰好 32 s MOS 必然关断。

默认 profile 写入的保护寄存器：

| 地址 / 寄存器 | D011 | D013 | D014 |
|---|---|---|---|
| 0x49 OVT/OVH | 0x42 | 0x42 | 0x42 |
| 0x4A OVL | 0xEE | 0xEE | 0xEE |
| 0x4B UVT/UVH | 0x72 | 0x72 | 0x72 |
| 0x4C UVL | 0x58 | 0x58 | 0x58 |
| 0x4D OCD1V/OCD1T | 0x00 | 0x00 | 0x01 |
| 0x4E OCD2V/OCD2T | 0x30 | 0x30 | 0x31 |
| 0x4F SCV/SCT | 0x07 | 0x07 | 0x07 |
| 0x50 OCCV/OCCT | 0x01 | 0x00 | 0x04 |
| 0x51 OTC | 0x85 | 0x85 | 0x85 |
| 0x52 OTD | 0x76 | 0x76 | 0x76 |
| 0x53 UTC | 0x77 | 0x77 | 0x77 |
| 0x54 UTD | 0xC0 | 0xC0 | 0xC0 |

## 6. GPIO、业务参数、SOC、低功耗

### GPIO 源码输入

| 用途 | D008 | D011 | D013 | D014 |
|---|---|---|---|---|
| AFE 总线 | PC0/PC1 I2C | PB6/PB7/PD7/PD2 SPI | 同 D011，硬件待核 | 同 D011 |
| 主 UART TX/RX | PC2/PC3 | PC2/PC3 | PC2/PC3 | PC2/PC3 |
| RS485 方向 | 不适用 | PA1 | 禁用，不驱动 RS485 | PA1 |
| 通信使能/唤醒 | 不适用 | PD4/PD3 | PD4/PD3，硬件待核 | PD4/PD3 |
| ACC/开关 | PA0 ACC | PA0 switch | PA0 switch，硬件待核 | PA0 switch |
| 负载/外部唤醒 | PB1 load detect | PB1 INT-WK | PB1 INT-WK，硬件待核 | PB1 INT-WK |
| AFE enable / ALARM / RESET | PD7 enable | PC0 ALARM / PC1 RESET | 同 D011，硬件待核 | 同 D011 |
| MCU-LDO | PC4 | 不适用 | 不适用 | 不适用 |
| 加热 | PA1 | PB4 | 不初始化此路径 | 不初始化此路径 |
| 熔断 | PD4 | PB5 恒安全 LOW | 不初始化此路径 | 不初始化此路径 |
| 调试 LED | 当前 LED_BLUE_PIN=PB4 | PC4，默认禁用 | PC4，默认禁用 | PC4，默认禁用 |
| SOC LED | PB4/PB5/PB7/PD3 | 无这组定义 | 无这组定义 | 无这组定义 |
| SWS | PA7 | PA7 | PA7 | PA7 |

D008 文档中原理图 BLUE 网络为 PB6，当前 `LED_BLUE_PIN` 实际定义为 PB4；此表如实记录源码，不将二者写成已一致。SH 的 CMNT_EN 初始化先写高再开启输出。D013 所有 GPIO 仍缺自身原理图复核。

### 业务默认

| 项目 | 当前值 / 实际边界 |
|---|---|
| 加热 enable / 起止温度 | 参数 1；起点 0°C、停止 +5°C；只有 D008/D011 支持。实际低温需求还包含电池温度 ≤ charge-UTP Third 的情况 |
| 加热前置条件 | 有效电池/加热 NTC、充电来源/会话、先阻断充电并确认零充电电流；硬故障、放电、开线隔离互锁 |
| D008 熔断策略 | heater OFF 后 GP1 仍 ≥95°C 连续 10 s，触发 PD4；不可逆硬件路径需单独验收 |
| D011 熔断策略 | PB5 保持 LOW，未启用 D008 熔断动作 |
| 均衡 enable / 电压 / 压差 | 参数 1 / 3400 mV / 启动 50 mV、停止 30 mV；D013 capability 阻断 |
| 均衡测量资格 | 连续可信 1000 ms；单体 1000..5000 mV、单次变化 ≤250 mV、可疑压差界限 1000 mV；还受充电/温度/开线互锁 |
| 开线检查 | 首次空闲 10 s、周期 300 s；实际执行受业务空闲/输出互锁约束 |
| 默认 SOC | 60%（没有有效 State 记录时） |
| SOC 积分 / 电流不可靠区 | 200 ms / ±200 mA |
| SOC OCV rest / error band | 600 s / 5% |
| 容量学习 / 学习前隐藏容量 | 0 / 1；隐藏只在学习启用且尚未学得容量时生效，默认仍显示容量 |
| 电流校准 | offset=0 mA，gain=1000000 ppm；其他用户校准字段初始为 0 |
| 低压休眠计时默认 | 单体 <3000 mV：24 h；<2800 mV：1 h；仍受代码中的电流/通信/业务条件约束 |
| D008 suspend 退出 | 双向可靠电流绝对值 ≥500 mA |
| D008 ACC | PA0 高稳定 200 ms 请求独立深睡眠，PC4 保持高；PA0 低唤醒并完整启动；整机断电是另一事务 |
| SH 固定 UART PM 门禁 | `SH3673510_FIXED_UART_BLOCKS_PM=1`，阻止常规 BLE suspend；不能据此宣称所有 deep-sleep 路径关闭或低功耗验收完成 |
| AFE 采样失联 | 首次撤销输出授权；连续 2 次失败进入 bus silence；DVC 等待 5 s、SH 等待 35 s 后受控恢复 |
| 恢复输出资格 | 连续 3 次新有效采样及其他门禁都通过；无效串位输出 61001，不参与保护/SOC/均衡 |

### 通信和存储

Modbus slave address=1，UART 8N1；当前 divider=9、BWPC=13，16 MHz 下约 114285.7 baud，通常按 115200 端连接。四产品继续有 BLE 通道，D008 的 SIF/UART 复用由 bus mux 管理。
`0x2500` 为公共 AFE profile，`0x2A00` 为诊断，`0x2E00=0xD008` 是参数接口 magic，不是 D008 产品 ID。DVC `0x2800`/`0x2900` 固定配置诊断只读。AFE profile 写入需要授权并完整原子提交 35 words，包含 persist/apply/readback/rollback。

CFG2 schema 2 payload=322 bytes，内部 tag=8/11/13/14。State/Event 也有独立产品标识；旧参数不迁移。512 KiB Flash 布局：Event `0x40000`/8 sectors、State `0x53000`/8 sectors、Config `0x5B000`/4 sectors、Factory `0x5F000`/2 sectors，每 sector 4096 bytes。1/2 MiB 的替代布局由 `flash_store_cfg.h` 按实际容量选择。
恢复默认使用 `0x2E10`：1 软件保护、2 AFE profile、3 业务参数、6 工厂运行模式；AFE/工厂操作需要相应授权。旧 `0x1102=3` 不再承担此功能。

## 7. 构建、验证与发布

```sh
python bms_tools/bms.py --all-products sources --check
python tests/run_host_regression.py
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp link --jobs 4
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp resources
# D008 另外按装配选择 20s-nmc 或 24s-lfp；link 不生成 BIN
```

生产必须干净提交、有效 Build ID、软件/硬件保护开启；D008 必须显式选 profile。公共 core 使用 `-Os`，其他 SDK/AFE/平台保持 `-O2`，官方 TC32 ABI/启动/链接脚本未更换。Flash 余量硬门禁 8 KiB，主栈预算 3072 bytes。

| 生产配置 | 预计镜像 bytes（含 CRC） | Flash 余量 bytes | 扣主栈预算后 RAM 余量 bytes |
|---|---:|---:|---:|
| D008 16S | 118036 | 8940 | 4708 |
| D008 20S | 118036 | 8940 | 4700 |
| D008 24S | 118036 | 8940 | 4692 |
| D011 | 113268 | 13708 | 5524 |
| D013 | 112452 | 14524 | 5540 |
| D014 | 113108 | 13868 | 5524 |

资源数字来自本轮实现 `a700bcf` 的同树本机提交 `b575189`，官方 Linux TC32 仅生成 ELF/MAP/LST；不同工具链以自己的 resources 为准。
本轮新增 OTA 分组控制后，D008 预计镜像比上一版增加 1312 bytes，SH 产品增加 1152 bytes；代码行减少不等于二进制必然缩小。
最小 Flash 余量为 D008 的 8940 bytes，超过 8192-byte 门禁 748 bytes；扣除主栈预算后的最小 RAM 余量为 4692 bytes。
本机 104 组主机回归与 CTest 3/3 通过。远端 8 个任务全部通过（六套生产 ELF、主机/CTest、Windows 四产品链接/资源/静态检查），见 [CI 37291934835](https://github.com/CS19970929/telink-new-sdk-b85/actions/runs/37291934835)。
Windows 使用独立 checkout；生产环境干净提交和资源门禁保持启用。

实际发布还需要按产品确定容量、化学体系、软/硬件阈值和 enable mask，核对设备参数读回及 AFE 寄存器，完成 MOS/温度/开线/通信故障/低功耗/掉电/OTA 实板验收。

## 8. 默认字段原始值

以下附表通过预处理各产品真实源码并执行默认构造函数提取，便于上位机逐字段比对。电流单位 0.1 A；温度编码 `(°C+40)×10`；软件 Filter 单位 10 ms；总压单位 0.01 V。D008 三个 profile 的 AFE 默认值相同。

### 8.1 软件保护 65 words

| 字段 | D008 16S | D008 20S | D008 24S | D011 | D013 | D014 |
|---|---:|---:|---:|---:|---:|---:|
| `u16VcellOvp_First` | 3750 | 3750 | 3750 | 3750 | 3750 | 3750 |
| `u16VcellOvp_Second` | 3750 | 3750 | 3750 | 3750 | 3750 | 3750 |
| `u16VcellOvp_Third` | 3750 | 3750 | 3750 | 3750 | 3750 | 3750 |
| `u16VcellOvp_Rcv` | 3500 | 3500 | 3500 | 3500 | 3500 | 3500 |
| `u16VcellOvp_Filter` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16VcellUvp_First` | 3000 | 3000 | 3000 | 3000 | 3000 | 3000 |
| `u16VcellUvp_Second` | 3000 | 3000 | 3000 | 3000 | 3000 | 3000 |
| `u16VcellUvp_Third` | 2200 | 2200 | 2200 | 3000 | 3000 | 3000 |
| `u16VcellUvp_Rcv` | 3100 | 3100 | 3100 | 3100 | 3100 | 3100 |
| `u16VcellUvp_Filter` | 100 | 100 | 100 | 1000 | 1000 | 1000 |
| `u16VbusOvp_First` | 5600 | 7000 | 8400 | 3500 | 1400 | 2800 |
| `u16VbusOvp_Second` | 5760 | 7200 | 8640 | 3600 | 1440 | 2880 |
| `u16VbusOvp_Third` | 5840 | 7300 | 8760 | 3650 | 1460 | 2920 |
| `u16VbusOvp_Rcv` | 5600 | 7000 | 8400 | 3500 | 1400 | 2800 |
| `u16VbusOvp_Filter` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16VbusUvp_First` | 4800 | 6000 | 7200 | 3000 | 1200 | 2400 |
| `u16VbusUvp_Second` | 4800 | 6000 | 7200 | 3000 | 1200 | 2400 |
| `u16VbusUvp_Third` | 4640 | 5800 | 6960 | 2900 | 1160 | 2320 |
| `u16VbusUvp_Rcv` | 4800 | 6000 | 7200 | 3000 | 1200 | 2400 |
| `u16VbusUvp_Filter` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16IchgOcp_First` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16IchgOcp_Second` | 150 | 150 | 150 | 150 | 150 | 150 |
| `u16IchgOcp_Third` | 200 | 200 | 200 | 200 | 200 | 200 |
| `u16IchgOcp_Rcv` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16IchgOcp_Filter` | 10 | 10 | 10 | 10 | 10 | 10 |
| `u16IdsgOcp_First` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16IdsgOcp_Second` | 150 | 150 | 150 | 150 | 150 | 150 |
| `u16IdsgOcp_Third` | 200 | 200 | 200 | 200 | 200 | 200 |
| `u16IdsgOcp_Rcv` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16IdsgOcp_Filter` | 10 | 10 | 10 | 10 | 10 | 10 |
| `u16TChgOTp_First` | 800 | 800 | 800 | 800 | 800 | 800 |
| `u16TChgOTp_Second` | 900 | 900 | 900 | 900 | 900 | 900 |
| `u16TChgOTp_Third` | 950 | 950 | 950 | 950 | 950 | 950 |
| `u16TChgOTp_Rcv` | 900 | 900 | 900 | 900 | 900 | 900 |
| `u16TChgOTp_Filter` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16TchgUTp_First` | 450 | 450 | 450 | 450 | 450 | 450 |
| `u16TchgUTp_Second` | 430 | 430 | 430 | 430 | 430 | 430 |
| `u16TchgUTp_Third` | 400 | 400 | 400 | 400 | 400 | 400 |
| `u16TchgUTp_Rcv` | 430 | 430 | 430 | 430 | 430 | 430 |
| `u16TchgUTp_Filter` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16TdischgOTp_First` | 900 | 900 | 900 | 900 | 900 | 900 |
| `u16TdischgOTp_Second` | 900 | 900 | 900 | 900 | 900 | 900 |
| `u16TdischgOTp_Third` | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 |
| `u16TdischgOTp_Rcv` | 900 | 900 | 900 | 900 | 900 | 900 |
| `u16TdischgOTp_Filter` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16TdischgUTp_First` | 300 | 300 | 300 | 300 | 300 | 300 |
| `u16TdischgUTp_Second` | 250 | 250 | 250 | 250 | 250 | 250 |
| `u16TdischgUTp_Third` | 200 | 200 | 200 | 200 | 200 | 200 |
| `u16TdischgUTp_Rcv` | 300 | 300 | 300 | 300 | 300 | 300 |
| `u16TdischgUTp_Filter` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16TmosOTp_First` | 1150 | 1150 | 1150 | 1150 | 1150 | 1150 |
| `u16TmosOTp_Second` | 1250 | 1250 | 1250 | 1250 | 1250 | 1250 |
| `u16TmosOTp_Third` | 1350 | 1350 | 1350 | 1350 | 1350 | 1350 |
| `u16TmosOTp_Rcv` | 1200 | 1200 | 1200 | 1200 | 1200 | 1200 |
| `u16TmosOTp_Filter` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16VdeltaOvp_First` | 600 | 600 | 600 | 600 | 600 | 600 |
| `u16VdeltaOvp_Second` | 800 | 800 | 800 | 800 | 800 | 800 |
| `u16VdeltaOvp_Third` | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 |
| `u16VdeltaOvp_Rcv` | 800 | 800 | 800 | 800 | 800 | 800 |
| `u16VdeltaOvp_Filter` | 100 | 100 | 100 | 100 | 100 | 100 |
| `u16SocLow_First` | 20 | 20 | 20 | 20 | 20 | 20 |
| `u16SocLow_Second` | 10 | 10 | 10 | 10 | 10 | 10 |
| `u16SocLow_Third` | 5 | 5 | 5 | 5 | 5 | 5 |
| `u16SocLow_Rcv` | 11 | 11 | 11 | 11 | 11 | 11 |
| `u16SocLow_Filter` | 100 | 100 | 100 | 100 | 100 | 100 |

### 8.2 AFE profile 35 words

| 字段 | D008（全部 profile） | D011 | D013 | D014 |
|---|---:|---:|---:|---:|
| `schema_version` | 1 | 1 | 1 | 1 |
| `afe_model` | 4388 | 13584 | 13584 | 13584 |
| `cov_mv` | 3750 | 3750 | 3750 | 3750 |
| `cov_delay_ms` | 1000 | 1000 | 1000 | 1000 |
| `cov_recover_mv` | 3500 | 3500 | 3500 | 3500 |
| `cov_recover_ms` | 1000 | 1000 | 1000 | 1000 |
| `cuv_mv` | 2200 | 3000 | 3000 | 3000 |
| `cuv_delay_ms` | 1000 | 10000 | 10000 | 10000 |
| `cuv_recover_mv` | 3100 | 3100 | 3100 | 3100 |
| `cuv_recover_ms` | 1000 | 10000 | 10000 | 10000 |
| `ocd1_a10` | 100 | 100 | 100 | 100 |
| `ocd1_delay_ms` | 100 | 100 | 100 | 100 |
| `ocd2_a10` | 150 | 150 | 150 | 150 |
| `ocd2_delay_ms` | 100 | 100 | 100 | 100 |
| `ocd_recover_a10` | 99 | 100 | 100 | 100 |
| `ocd_recover_ms` | 0 | 2000 | 2000 | 2000 |
| `occ1_a10` | 100 | 100 | 100 | 100 |
| `occ1_delay_ms` | 100 | 100 | 100 | 100 |
| `occ2_a10` | 150 | 0 | 0 | 0 |
| `occ2_delay_ms` | 100 | 0 | 0 | 0 |
| `occ_recover_a10` | 99 | 100 | 100 | 100 |
| `occ_recover_ms` | 0 | 100 | 100 | 100 |
| `sc_a10` | 0 | 300 | 300 | 300 |
| `sc_delay_us` | 0 | 256 | 256 | 256 |
| `sc_recover_ms` | 0 | 2000 | 2000 | 2000 |
| `chg_ot_x10` | 0 | 950 | 950 | 950 |
| `chg_ot_recover_x10` | 0 | 900 | 900 | 900 |
| `chg_ut_x10` | 0 | 400 | 400 | 400 |
| `chg_ut_recover_x10` | 0 | 430 | 430 | 430 |
| `dsg_ot_x10` | 0 | 1000 | 1000 | 1000 |
| `dsg_ot_recover_x10` | 0 | 900 | 900 | 900 |
| `dsg_ut_x10` | 0 | 200 | 200 | 200 |
| `dsg_ut_recover_x10` | 0 | 300 | 300 | 300 |
| `temp_recover_ms` | 0 | 1000 | 1000 | 1000 |
| `enable_mask` | 63 | 223 | 223 | 223 |
