# AGENTS.md — D013 / TLSR8251 / SH3673510

本分支目标是 D013。当前源码 product profile 为 **SH3673510 / 4S / 100µΩ**，但当前可访问的用户资料中**没有 D013 专属原理图/BOM**。因此所有 `D011_*` GPIO、heater/fuse、NTC位置、通信供电网络只能视为代码继承，不能视为 D013 硬件事实。

## 开发前必读

1. `docs/D013_PRODUCT_REFERENCE.md`：当前源码事实与缺失证据的唯一产品说明。
2. `docs/HARDWARE_VALIDATION.md`：当前唯一实板/资料阻断清单。
3. `docs/ARCHITECTURE.md`。
4. `docs/SOFTWARE_PROTECTION.md`、`docs/AFE_HARDWARE_PROTECTION_V2.md`。

## 权威依据

- D013 板级连接：必须由 D013 原理图/BOM决定；当前缺失，不得用 D011 原理图替代。
- AFE寄存器/协议：`SH36735XX CV1.0A`。
- 当前软件行为：本分支源码。

## 当前源码事实

- `sh3673510_project_config.h`：当前 cell count=4、Rsense=100µΩ；SPI pin和大量宏沿用D011命名。
- `conf.h`：当前 `MODBUS_RS485_ENABLE=0`，但仍保留 `FD_BMS_TYPE=D11`、`D011`硬件版本、`BT_D011`设备名、D11容量/OC历史默认。
- `sh3673520_reg.h` / `sh3673520*.c`：SH36735xx系列寄存器/SPI。
- `sh3673510_control.c`：AFE配置与硬件保护量化。
- `bms_sw_protection.*`：统一软件三级保护。
- `bms_afe_hw_profile.*`：独立AFE硬件保护。

这些 D011 identity/IO 残留必须在拿到 D013 原理图和产品参数后有证据地清理；禁止仅做字符串替换后宣称硬件适配完成。

## 禁止事项

- 不得把 D011 的 PD4/PA1/PB5/PD3 等网络直接写成 D013 事实。
- 不得声称 D013 的 TS3/TS4 就是 heater-MOS / power-MOS NTC。
- 不得声称当前100µΩ、4S、SH3673510已经由D013原理图确认；目前它们是CODE事实。
- 不得把当前 `CapacityFactory=116`、`AFE_ODC1/2`、`BT_D011` 等历史值宣传为D013产品参数。

## 保护和构建

软件 `g_tParam.protect` 与 AFE hardware profile 独立。任何产品参数/IO改动先补 D013 原理图证据，再通过 SH driver/integration/protection/temp、software protection、AFE HW profile、SOC/Flash contracts 及 TC32 clean rebuild/check-fw/MAP/verify/cppcheck。CI不能替代硬件确认。

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