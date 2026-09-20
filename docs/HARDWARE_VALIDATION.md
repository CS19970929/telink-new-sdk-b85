# D013 实板验证与发布阻断项

本文件只保留 D013 当前尚需硬件证据的项目。产品源码事实见 `D013_PRODUCT_REFERENCE.md`。

> 当前首要阻断：**未在已提供资料中找到 D013 专属原理图/BOM。** 在原理图补齐前，所有 `D011_*` GPIO 继承只能算 CODE，不算 D013 板级事实。

## 1. P0：先建立硬件真值

- [ ] 提供/定位 D013 原理图与 BOM，并绑定文件版本/SHA。
- [ ] 逐网确认 MCU=TLSR8251F512ET32、AFE=SH3673510、4S cell wiring、100µΩ current shunt 的实际硬件事实。
- [ ] 逐一确认当前源码继承的 PD4/PD7/PA0/PA1/PA7/PB1/PB4/PB5/PB6/PB7/PC0/PC1/PC2/PC3/PC4/PD2/PD3 是否真的与 D013 PCB 对应。
- [ ] 确认 D013 是否存在 D011 同类 heater/fuse/RS485/CMNT-WK 网络；未确认前不得触发 PB5 等潜在危险输出。
- [ ] 确认 TS1..TS4 的实际 NTC 数量、阻值、物理位置；当前10K与 heater/MOS分工只是 D011 派生代码。

## 2. Product identity cleanup 前置验证

- [ ] 确定 D013 正确 product ID、硬件版本字符串、蓝牙名、序列号前缀、工厂容量。
- [ ] 签核 D013 OV/UV、充放电 OC、SC、温度、SOC OCV/端点。
- [ ] 在确定真实值前，当前 `FD_BMS_TYPE=D11`、`D011` 版本字符串、`BT_D011`、CapacityFactory=116、ODC1/ODC2 历史值不得宣传为 D013 产品配置。

## 3. AFE/SPI

- [ ] 用 D013 实板确认 PB6/PB7/PD7/PD2 SPI 连接、Mode3、500kHz、CRC/ACK、RESET、ALARM。
- [ ] SCONF1=0x00、SCONF2=0x50、SCONF3=0x44、SCONF4=0x64、SCONF5初始0x3C、SCONF7=0x04、0x47=0x57、0x48=0xFF 的启动 readback。
- [ ] 运行时 AFE HW profile 对 SCONF5 OCC_EN / SCONF6 protection bits 的实际覆盖。
- [ ] WDT code0（约32.34s）、FLAG2/WDT_FLG 和 Powerdown 的真实行为。

## 4. 4S / 100µΩ 测量与保护

- [ ] 4S 有效通道、未使用 SH cell channels 的实际连接和软件排除。
- [ ] 100µΩ shunt 的物料、并联结构、Kelvin采样、零点、方向、增益、热漂。
- [ ] OV/UV/OCD1/OCD2/OCC/SC requested -> code -> effective -> readback -> MOS 波形。
- [ ] 确认 SH current range/量化全部按100µΩ计算，没有遗留250µΩ/D011范围限制。

## 5. 软件/硬件保护独立性

- [ ] 修改 `g_tParam.protect` 软件三级参数时 AFE HW profile 不变。
- [ ] 修改 AFE HW 35-word profile 时软件参数不变。
- [ ] 非法参数、掉电、persist/apply/readback失败的 rollback；rollback失败明确为 `CONFIG_INCONSISTENT`。

## 6. IO/通信/低功耗

- [ ] 当前源码 `MODBUS_RS485_ENABLE=0` 的 direct UART 与真实 D013 硬件接口一致。
- [ ] 开关、charger/load detect、通信唤醒、LED、加热（若存在）全部以 D013 原理图重建，不复用 D011 结论。
- [ ] Sleep/Wake、AFE fail-safe、MOS恢复、BLE/Modbus、Flash/OTA 端到端验证。

## 7. 证据格式

每项测试必须记录 D013 原理图/BOM版本、板号、固件 commit、AFE型号/版本、Rsense、requested/effective、仪器、环境、波形/日志和结论。在原理图未补齐前，不允许把当前编译成功描述为 D013 硬件验收完成。
## 2026-09-21 低功耗失败闭环

- [ ] 按 [SH低功耗修复说明](SH_LOW_POWER_FAILURE_HANDLING.md) 验证逐级休眠失败、SLEEP ACK丢失、SPI持续失联功耗、OTA/UART互锁、PAD竞争及BLE下200ms采样周期；Host通过不关闭此项。
