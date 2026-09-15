# AGENTS.md — D011 / TLSR8251 / SH3673510

本分支产品是 **HS-D011-10S50A-V1 + TLSR8251F512ET32 + SH3673510，10S**。禁止套用 D008/DVC1124 的 IO、I2C、寄存器和产品参数。

## 开发前必读

1. `docs/D011_PRODUCT_REFERENCE.md`：当前唯一 D011 IO/AFE 配置真值入口。
2. `docs/HARDWARE_VALIDATION.md`：当前唯一实板未决清单。
3. `docs/ARCHITECTURE.md`：通用软件边界。
4. `docs/SOFTWARE_PROTECTION.md`、`docs/AFE_HARDWARE_PROTECTION_V2.md`。

## 权威依据

- 板级连接：用户提供的 `hs-d011-10s50a-v1.pdf` / 实际 BOM。
- AFE寄存器/协议：`SH36735XX CV1.0A`。
- 当前软件行为：本分支源码。

SH3673510/3514/3517/3520 共用手册寄存器/协议模型，仓库系列驱动仍名 `sh3673520_*`；不得由文件名把板上器件改写成 SH3673520。

## 当前源码边界

- `sh3673510_project_config.h`：D011 10S/250µΩ、IO、AFE静态产品配置。
- `sh3673520_reg.h`：SH36735xx CV1.0A寄存器/协议真值。
- `sh3673520*.c`：SPI事务。
- `sh3673510_control.c`：AFE配置、硬件保护量化、FET/温度控制。
- `sh3673510_bms.c`：BMS适配、恢复和通信fail-safe。
- `bms_sw_protection.*`：软件三级保护。
- `bms_afe_hw_profile.*`：独立 AFE HW profile。

## 保护参数规则

软件 `g_tParam.protect` 与 AFE hardware profile 已独立。不得恢复“修改软件三级参数同时重写AFE硬件参数”的旧行为。SC/OCD/OCC等最终产品阈值必须有产品/实板依据。

## IO安全规则

D011 PB5 `HT-RF-EN` 当前认定为不可逆加热保险丝触发路径，普通运行必须保持安全低电平；没有完整硬件状态机和测试不得拉高。RN3/RN4 原理图标10M，但用户确认实装10K；文档/代码必须明确区分原图与实际BOM确认，不能静默改写原图事实。

PC1 `RESET` 的宏名含 `OUT` 不等于当前运行方向已经硬件签核；供电、RS485/CMNT-WK、sleep/wake 仍需按验证清单测试。

## 构建

保持 Telink SDK、TC32工具链和 ABI。安全相关修改至少通过 SH driver/integration/protection/temp、unified software protection、independent AFE HW profile、SOC/Flash contracts，以及 TC32 clean rebuild/check-fw/MAP/verify/cppcheck。CI不能替代实板验证。