# D014 产品硬件与固件配置基线

> 适用分支：`refactor/d014-common-bms-features`  
> 依据：用户提供的 `HS-D014-8S15A` 原理图（2026-09-04）+ 当前 D011 SH3673510 公共实现。
>
> 原则：原理图只能证明板级连接/器件标注；容量、最终保护阈值、BOM 实装差异和实测时序不能从“8S15A”文件名反推。

## 1. 当前产品事实

| 项目 | D014 当前实现 |
|---|---|
| MCU | TLSR8251F512ET32 |
| AFE | SH3673510 |
| 电芯 | 8S；B0..B8，对应 VC0..VC8 |
| 电流分流 | RS1/RS2/RS3 均 2mΩ，并联名义值 0.6667mΩ；软件整数模型 667µΩ |
| AFE SPI | PB6=MISO、PB7=MOSI、PD7=SCLK、PD2=CS-M；沿用已验证 Mode 3 / 500kHz 驱动 |
| 主通信 | 隔离 RS485；PA1=485-EN，PC2=SCI1-TX，PC3=SCI1-RX，PD4=CMNT-EN，PD3=CMNT-WK |
| 开关/唤醒 | PA0=DI1/SW1，PB1=INT-WK-MCU，PC0=ALARM，PC1=RESET |
| LED | PC4=DB-LED1 |
| 均衡 | B1..B8 均有均衡驱动，软件允许 8S balance |
| 加热 | D014 原理图没有 D011 的 PB4/HT-CHG、PB5/HT-RF-EN 证据，TS3 标为 NC；固件强制关闭 heater |
| 温度 | TS1/TS2 为 10K-3435；TS3 不使用；TS4 标注 MOS，但 RN4 图纸值为 10M，未完成 BOM/实板确认 |

## 2. 8S 与未使用通道

D014 只把 VC1..VC8 作为有效 cell。SH3673510 的上部未使用 cell 输入不得参与：

- min/max cell；
- 软件 OV/UV/压差；
- SOC/OCV；
- 均衡 mask；
- Open-Wire 有效 cell 判断。

`SH3673510_D011_CELL_COUNT` 仍是从 D011 公共代码继承的兼容宏名，但值已固定为 8。后续公共框架可再统一重命名，不应在本次硬件移植中扩大改动面。

## 3. 电流采样模型

原理图给出三只 2mΩ 分流电阻并联：

```text
Rshunt = 2mΩ / 3 = 0.666666...mΩ = 666.666...µΩ
```

当前 SH36735xx 电流换算接口使用整数 µΩ，因此 D014 配置为 `667u`。仅由取整造成的模型误差约 +0.05%，远小于实际分流器容差、铜阻、Kelvin 走线和 AFE 增益/零偏误差；量产仍必须做零点、增益、方向和温漂实测。

AFE OCD/OCC/SC 的 requested -> code -> effective 电流也使用同一 667µΩ 模型，不得沿用 D011 的 250µΩ effective 电流结果。

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

项目配置新增 `D014_*` 正式宏。为了最小风险复用 D011 已验证 SH3673510 实现，暂保留 `D011_*` 兼容别名；D014 新代码不得继续新增 D011 板名依赖。

## 5. Heater / TS3 / TS4

D014 与 D011 的关键差异之一是 heater 证据不成立：

- 原理图 TS3 明确标为 `TS3-NC`；
- 当前 D014 图中 PB4/PB5 没有 D011 的 `HT-CHG` / `HT-RF-EN` 网络；
- 因此 `SH3673510_PRODUCT_HEATER_SUPPORTED=0`；
- board/control/app 三层均不得初始化或驱动 D011 heater/fuse GPIO。

TS4 虽标为 `TS4-MOS`，但 RN4 图纸值为 10M，而现有温度换算按 10K NTC 表。未拿到 D014 BOM/实测前：

- `SH3673510_PRODUCT_MOS_NTC_SUPPORTED=0`；
- MOS temperature 不作为可信软件保护输入；
- AFE common hardware temperature protection仍只使用已确认的 TS1/TS2。

若后续确认 RN4 实装为 10K-3435，只需在产品 profile 中显式开启 MOS NTC，并补 contract + 实板温度点校验。

## 6. Balance

D014 图中有 B1..B8 的均衡驱动，因此 `SH3673510_PRODUCT_BALANCE_SUPPORTED=1`。

公共 balance 策略继续负责：

- 有效 cell 范围；
- 起停压差；
- 充电会话条件；
- 温度条件；
- Open-Wire/采样可信度互锁；
- AFE balance mask。

实板发布前仍需验证相邻通道、同时均衡数量、温升、采样干扰和停止行为。

## 7. AFE 静态/动态保护

除产品参数外，D014 复用 D011 最新 SH3673510 驱动、保护仲裁和 fail-safe：

- SCONF4 的 CN 由 10 改为 8；
- current/OCD/OCC/SC 全部按 667µΩ重新量化；
- 软件三级保护与 AFE hardware profile 继续独立持久化；
- AFE profile 继续执行 validate -> persist -> apply -> readback/effective -> verify/rollback；
- SC recovery、OCD/OCC physical recovery、communication-loss fail-safe 不因 D014 移植而降级。

## 8. 当前仍未签核的产品参数

原理图没有给出以下产品定义，因此本分支不把它们伪装成 D014 硬件事实：

1. 额定容量；当前 `CapacityFactory=116` 只是从 D011 继承的迁移默认值。
2. 最终软件 First/Second/Third OV/UV/OC/温度/压差阈值。
3. 最终 AFE HW requested/effective OC/SC/温度阈值。
4. TS4/RN4 的实际 BOM。
5. RS485/CMNT-WK 实际唤醒有效电平和通信电源时序。
6. D014 独立协议产品 ID。当前 `D14` 暂时别名到历史 D11 数值 ID，以避免在未同步上位机时破坏 wire/storage compatibility；硬件字符串和 BLE 名称已经改为 D014。

这些项必须在量产签核前关闭，但不阻塞当前 8S 板级移植、编译和基础联调。

当前 D014 的 AFE hardware profile 首次初始化使用 `sh3673510_project_config.h` 中独立的 `SH3673510_HW_DEFAULT_*` 值，不再复制软件保护的 First/Second/Third 表。`OCD1/OCC1` requested 与 recover 均为 100（0.1A）；D014 的 667µΩ 分流模型经 AFE 量化后，effective 阈值分别为 150 和 104（0.1A），恢复判断仍要求电流严格低于 effective 阈值。这些默认值只用于开发联调，最终保护阈值仍需实板签核。

## 9. 代码入口

- `vendor/ble_sample/sh3673510_project_config.h`：D014 8S / 667µΩ / GPIO / feature capability。
- `vendor/ble_sample/conf.h`：产品身份、RS485、容量等编译期默认。
- `vendor/ble_sample/sh3673520*.c`：SH36735xx SPI/register driver。
- `vendor/ble_sample/sh3673510_control.c`：静态配置、硬件保护量化、FET/balance。
- `vendor/ble_sample/sh3673510_bms.c`：采样、保护恢复、measurement publish。
- `vendor/ble_sample/bms_sw_protection.*`：软件三级保护。
- `vendor/ble_sample/bms_afe_hw_profile.*`：独立 AFE hardware protection profile。
- `vendor/ble_sample/bms_features.*`：balance/open-wire/heater 公共策略。
- `tests/sh3673510_d014_integration_check.py`：D014 板级 contract。
- `tests/d014_afe_profile_default_host_check.py`：执行 D014 AFE 默认 profile 构建与校验。
