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

## 当前开发期存储策略

- 本分支处于持续开发阶段，当前不要求兼容旧 Config schema / 旧 Heater / Balance 参数布局。
- Heater / Balance 参数结构变化时允许显式提升 schema 并恢复新默认；不要为未发布旧格式增加迁移器。
- 当前格式仍必须保证掉电一致性、参数校验、错误传播与安全默认。

## Windows 上位机单一真源（强制）

- D008/D011/D013 当前实际使用的上位机**唯一真源**是本仓库分支 `feature/windows-afe-hw-protection-editor-v2` 下的 `bms-tool-windows/`。
- 客户版为 `bms-tool-windows/BmsTool.Windows/`；内部完整测试版为 `bms-tool-windows/BmsFactoryTest.Windows/`。公共功能变更必须同步维护两版。
- 本产品分支历史 `tools/BMSAssistant/`、`tools/BMSAssistantQt/`、`tools/BMSAssistantAndroid/` 均为废弃客户端，不得再作为实现、协议或测试依据，也不得恢复。
- 收到“上位机、Windows 工具、事件日志、参数编辑、AFE 编辑器”等任务时，应先切到上述 Windows 上位机分支修改 `bms-tool-windows/`，不得在产品固件分支里另造客户端。
- **默认只改上位机。** 除非用户明确要求修改固件，或已证明现有固件协议无法完成需求并得到用户同意，否则不得为了适配 UI/读取逻辑而修改固件协议、寄存器地址、Flash 布局或持久化架构。
- 上位机任务遵循最小改动原则：先复用现有固件协议和寄存器；不要因为客户端读取问题扩展为固件重构、跨平台客户端同步或新协议设计。