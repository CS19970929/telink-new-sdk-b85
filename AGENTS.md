# AGENTS.md — D008 / TLSR8251 / DVC1124

本分支实际产品是 **HS-D008 + TLSR8251F512ET32 + DVC1124-2**。历史分支名包含 `sh3673510-d013`，不得据此推断硬件。

## 开发前必读

1. `docs/D008_PRODUCT_REFERENCE.md`：当前唯一 D008 IO/AFE/Product Profile 真值入口。
2. `docs/HARDWARE_VALIDATION.md`：当前唯一实板未决清单。
3. `docs/ARCHITECTURE.md`：软件边界/参数所有权。
4. `docs/SOFTWARE_PROTECTION.md` 与 `docs/AFE_HARDWARE_PROTECTION_V2.md`：软/硬件保护分层。
5. `references/vendor/dvc11xx_demo_v1.3/README.md`：DVC11XX DemoCode V1.3 的审查结论与低边 FET 示例索引，仅作二级参考。

## 权威依据

- 板级连接：用户提供的 `HS-D008-24S100A-V1(2).pdf` / 对应 BOM。
- DVC寄存器/bit/量化/时序：DVC1124-2 Reference Manual V1.2。
- 厂商 Demo：`references/vendor/dvc11xx_demo_v1.3/`，只用于调用方式、时序和交叉验证，**不得覆盖 DVC1124-2 官方参考手册**。
- 当前软件行为：本分支源码。

资料不足时明确写 `TODO_VERIFY_HW`；禁止用 SH36735xx、BQ769xx、旧 SH367309 或“常见BMS做法”猜 DVC 寄存器和安全参数。Demo 与官方 DVC1124-2 参考手册冲突时，以官方参考手册为准；Demo 自身存在型号默认值和示例代码不一致项，禁止直接复制到量产逻辑。

## 当前源码边界

- `dvc1124_reg.h`：DVC1124 V1.2 芯片真值。
- `dvc1124_project_config.h`：D008 DVC 板级默认。
- `d008_product_profile.h`：24S LFP / 20S NMC 物理 profile。
- `dvc1124.c`：I2C、寄存器、量化、采样、Balance/Open-Wire。
- `dvc1124_bms.c`：BMS/FET/故障适配。
- `bms_sw_protection.*`：统一软件三级保护。
- `bms_afe_hw_profile.*`：独立 AFE 硬件保护参数。

应用层不得直接复制 DVC 寄存器 magic value。涉及 safety register 的写入必须按 mask/shift、范围、量化、readback 审核。

## D008 低边 FET 状态规则

- D008 使用 `GP5=CHG_LS`、`GP6=DSG_LS`，高边输出被 mask。
- `0x51 CHGC/DSGC` 是驱动命令；`0x06 CHGF/DSGF` 是 AFE 驱动输出标志。
- Vendor Demo 的 GP5/GP6 示例把 GP5/GP6 **额外接回 MCU GPIO** 才读取“LOW SIDE CHG/DSG on”，因此 `CHGF/DSGF` 不得直接命名为物理 MOS 实际状态。
- 软件至少区分 Requested、AFE Command、AFE Driver Flag、Physical Feedback 四层。当前 D008 若无已确认的 GP5/GP6/Gate/Vgs 反馈，Physical Feedback 必须标记为 unavailable/unknown。
- `b1Status_MOS_CHG/DSG` 不得同时承担“目标命令”和“物理反馈”两种语义。

## 保护参数规则

- `g_tParam.protect` 只属于软件 First/Second/Third/Recover/Filter。
- AFE hardware profile 独立，不得由软件参数写入副作用修改。
- SCD、DVC WDT、Body-Diode、GP2/GP3 BOM、NTC R-T 在未完成硬件签核前不得为了“功能完整”擅自启用/猜值。

## IO规则

修改 GPIO 前必须同时核对 `D008_PRODUCT_REFERENCE.md`、原理图和调用代码。PC0/PC1 是 DVC I2C；PD7 是 `MCU-AFE-EN`；PB1 是低有效 `CHG-IN`；D011 的 SPI/RS485/HT-RF-EN 等网络不得移植到 D008。

## 构建

保持 Telink SDK `tc_ble_single_sdk V3.4.2.8_Patch_0001` 和固定 TC32 工具链/ABI。修改源码顺序时显式更新 `bms_tools/source_order.txt`。任何安全相关修改至少通过 source-order、Host contracts、TC32 clean rebuild/check-fw/MAP/verify/cppcheck；这些仍不能替代实板验证。