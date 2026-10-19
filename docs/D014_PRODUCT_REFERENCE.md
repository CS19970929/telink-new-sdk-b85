# D014 产品硬件与固件配置基线

> 当前分支：`codex-bms-deep-simplification-20261016`；分流器模型于 2026-10-19 更新，其余硬件资料边界仍保留。
> 依据：用户提供的 `HS-D014-8S15A` 原理图（2026-09-04）+ 当前 monorepo 的 SH 公共实现。硬件原件未随仓库交接，本次未重新核验原件。
>
> 原则：原理图只能证明板级连接/器件标注；容量、最终保护阈值、BOM 实装差异和实测时序不能从“8S15A”文件名反推。

> 2026-10-06 资料入口：[AFE 使用指南](AFE_REFERENCE_GUIDE.md)、[项目内原资料库](../references/afe/README.md)、[固定提交审查](afe-audit/20261006-0aaa8429/README.md)。本轮审查 SHA 为 `0aaa8429`；报告问题尚未实施修复，原始型号/BOM/实板缺口不因导入手册而关闭。

## 1. 当前产品事实

| 项目 | D014 当前实现 |
|---|---|
| MCU | TLSR8251F512ET32 |
| AFE | SH3673510 |
| 电芯 | 8S；B0..B8，对应 VC0..VC8 |
| 电流分流 | RS1/RS2/RS3 均 2mΩ，计算使用精确 2000/3µΩ；旧协议整数显示 667µΩ |
| AFE SPI | PB6=MISO、PB7=MOSI、PD7=SCLK、PD2=CS-M；源码配置 Mode 3 / 500kHz，波形仍需实测 |
| 主通信 | 隔离 RS485；PA1=485-EN，PC2=SCI1-TX，PC3=SCI1-RX，PD4=CMNT-EN，PD3=CMNT-WK |
| 开关/唤醒 | PA0=DI1/SW1，PB1=INT-WK-MCU，PC0=ALARM，PC1=RESET |
| LED | PC4=DB-LED1 |
| 均衡 | B1..B8 均有均衡驱动，软件允许 8S balance |
| 加热 | D014 原理图没有 D011 的 PB4/HT-CHG、PB5/HT-RF-EN 证据，TS3 标为 NC；固件强制关闭 heater |
| 温度 | TS1/TS2 为电池 10K-3435；TS3 不使用；用户确认 TS4 实装为 MOS 10K-3435。图纸 RN4 标 10M，需核对图纸/BOM 差异 |

## 2. 8S 与未使用通道

D014 只把 VC1..VC8 作为有效 cell。SH3673510 的上部未使用 cell 输入不得参与：

- min/max cell；
- 软件 OV/UV/压差；
- SOC/OCV；
- 均衡 mask；
- 无效串位不参与普通电压统计与保护。

`SH3673510_BOARD_CELL_COUNT` 是中性板级宏，由 D014 产品头文件给出 8；不是 D011 硬件别名。

## 3. 电流采样模型

原理图给出三只 2mΩ 分流电阻并联：

```text
Rshunt = 2mΩ / 3 = 0.666666...mΩ = 666.666...µΩ
```

当前 SH36735xx 电流换算和保护量化保留 `2000/3 µΩ` 精确比例，`667u` 仅作旧协议元数据。
取整的电阻模型误差约 +0.05%，但在 OCD/OCC 寄存器选档边界可能进入下一档，不能只按
0.05% 判断保护影响。源码参数窗口、原厂 p37..38 依据及 15A OCD2 跨档例子见
[配置与寄存器编辑器](AFE_CONFIGURATION_EDITOR.md)。量产仍须检查分流器容差、铜阻、
Kelvin 走线、AFE 增益/零偏及零点、方向、温漂和实际保护动作。

AFE OCD/OCC/SC 的 requested -> code -> effective 与采样使用同一精确比例；
SC 基于实际 OCD2 电压编码选择倍数，不使用已取整的 effective 电流再次放大。

## 4. GPIO 真值

| TLSR8251 GPIO | D014 网络 | 用途 |
|---|---|---|
| PD4 | CMNT-EN | 隔离通信电源控制 |
| PD7 | SCLK | AFE SPI SCK |
| PA0 | DI1 / SW1 | 开关输入 |
| PA1 | 485-EN | RS485 DE//RE |
| PA7 | SWS-A7 | 下载/调试 |
| PB1 | INT-WK-MCU | 外部唤醒 |
| PB6 | MISO | AFE SPI MISO |
| PB7 | MOSI | AFE SPI MOSI |
| PC0 | ALARM | AFE ALARM |
| PC1 | RESET | AFE RESET 网络 |
| PC2 | SCI1-TX | RS485 UART TX |
| PC3 | SCI1-RX | RS485 UART RX |
| PC4 | DB-LED1 | 调试 LED |
| PD3 | CMNT-WK | 通信唤醒 |
| PD2 | CS-M | AFE SPI CS |

产品输入使用 `BMS_BOARD_*` 与 `SH3673510_BOARD_*` 中性宏，由 D014 产品头文件提供；共享 AFE 代码不再持有 GPIO、NTC 角色与默认阈值。

## 5. Heater / TS3 / TS4

D014 与 D011 的关键差异之一是 heater 证据不成立：

- 原理图 TS3 明确标为 `TS3-NC`；
- 当前 D014 图中 PB4/PB5 没有 D011 的 `HT-CHG` / `HT-RF-EN` 网络；
- 因此 `SH3673510_PRODUCT_HEATER_SUPPORTED=0`；
- board/control/app 三层均不得初始化或驱动 D011 heater/fuse GPIO。

用户确认 TS4 实装为 10K-3435 MOS NTC，因此 `SH3673510_PRODUCT_MOS_NTC_SUPPORTED=1`。TS4 温度只参与独立 MOS 高温软件保护，不并入 TS1/TS2 的电池高低温范围；TS4 开短路会触发温度断线并关断 CHG/DSG。图纸 RN4=10M 与实装信息不一致，仍需温度点和开短路实测。

AFE 的 TS1/TS2/TS4 共用 OTC/UTC 阈值，当前 AFE 硬件温度保护只启用 TS1/TS2。TS4 的高温保护使用独立 `u16TmosOTp_*` 软件阈值，不能直接打开 TS4 硬件位并误用电池低温阈值。

## 6. Balance

D014 图中有 B1..B8 的均衡驱动，因此 `SH3673510_PRODUCT_BALANCE_SUPPORTED=1`。

公共 balance 策略继续负责：

- 有效 cell 范围；
- 起停压差；
- 当前无真实放电，允许冷启动静置均衡，不保存充电会话；
- 温度条件；
- 测量有效性与连续可信样本资格；
- AFE balance mask。

实板发布前仍需验证相邻通道、同时均衡数量、温升、采样干扰和停止行为。

## 7. AFE 静态/动态保护

D014 与 D011/D013 直接编译同一个 SH3673510 后端，通过产品输入区分：

- SCONF4 的 CN 由 10 改为 8；
- current/OCD/OCC/SC 全部按 2000/3µΩ 比例量化；
- 软件三级保护与 AFE hardware profile 继续独立持久化；
- AFE profile 继续执行 validate -> persist -> apply -> readback/effective -> verify/rollback；
- SC recovery、OCD/OCC physical recovery、communication-loss fail-safe 不因 D014 移植而降级。

## 8. 当前仍未签核的产品参数

原理图没有给出以下产品定义，因此本分支不把它们伪装成 D014 硬件事实：

1. 额定容量；当前源码值以 `d014_protection_config.h` 为准，开发输入不代替产品签核。
2. 最终软件 First/Second/Third OV/UV/OC/温度/压差阈值。
3. 最终 AFE HW requested/effective OC/SC/温度阈值。
4. TS4/RN4 的实际 BOM。
5. RS485/CMNT-WK 实际唤醒有效电平和通信电源时序。
6. D014 独立协议产品 ID。当前 `BMS_PRODUCT_WIRE_ID=2u` 保留历史 wire ID；内部存储 `BMS_PRODUCT_ID=14u` 已独立，不能把两种 ID 混为一谈。硬件字符串和 BLE 名称为 D014。

这些项必须在量产签核前关闭，但不阻塞当前 8S 板级移植、编译和基础联调。

当前 AFE 默认值来自 `bms/products/sh3673510_protection_config.h` 的独立
`SH3673510_HW_DEFAULT_*`，与软件三级参数分开。以 requested=100（0.1A）为例，
2000/3µΩ 模型下 OCD1/OCC1 effective 分别为 150 和 104（0.1A）；
恢复仍要求电流严格低于 effective。具体当前默认值读源码，不以此例代替参数签核。

## 9. 代码入口

- `bms/products/d014/bms_product.h`：D014 8S / 2000/3µΩ / GPIO / feature capability。
- `bms/products/d014/bms_product.h`：产品身份、RS485、容量等编译期默认。
- `bms/afe/sh3673510/sh3673520*.c`：SH36735xx SPI/register driver。
- `bms/afe/sh3673510/sh3673510_control.c`：静态配置、硬件保护量化、FET/balance。
- `bms/afe/sh3673510/sh3673510_bms.c`：采样、保护恢复、measurement publish。
- `bms/core/bms_sw_protection.*`：软件三级保护。
- `bms/core/bms_afe_hw_profile.*`：独立 AFE hardware protection profile。
- `bms/app/bms_features.*`：balance/heater 公共策略。
- `tests/d014_configuration_contract_check.py`：D014 板级 contract。
- `tests/d014_defaults_host_check.py`：执行 D014 AFE 默认 profile 构建与校验。
