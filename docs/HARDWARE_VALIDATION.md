# D014 实板验证与发布阻断项

本文件只记录 **HS-D014-8S15A / TLSR8251 / SH3673510** 仍需要实板或 BOM 证据关闭的项目。板级静态事实见 `D014_PRODUCT_REFERENCE.md`。

## 1. P0：首次上板

- [ ] VC1..VC8 逐通道电压正确；VC9..VC20 未使用通道不会进入 min/max、保护、SOC、balance、open-wire。
- [ ] 3×2mΩ 并联分流路径实测；确认等效阻值、Kelvin 采样方向、零点、增益和温漂。当前软件模型为 667µΩ。
- [ ] 充电/放电电流符号与 `u16Ichg/u16IDischg` 一致；SOC 积分方向正确。
- [ ] AFE SPI Mode 3 / 500kHz：初始化、连续采样、CRC/ACK、异常恢复。
- [ ] CHG/DSG 正常开关、上电默认安全态、通信失效 fail-safe。
- [ ] RS485：PA1方向控制、PC2 TX、PC3 RX、PD4 CMNT-EN；最后停止位发完后再切回接收。

## 2. AFE 保护

- [ ] COV/CUV requested -> code -> effective -> readback -> 实际 MOS 波形。
- [ ] OCD1/OCD2/OCC requested -> code -> effective 使用 667µΩ，而不是 D011 的 250µΩ。
- [ ] SC 阈值/延时/LOADOFF 恢复；持续短路不能出现 clear -> reopen -> short 循环。
- [ ] WDT、FLAG2/WDT_FLG、Powerdown 行为按 SH36735xx 手册实测。
- [ ] 软件三级保护与 AFE HW profile 独立修改、独立持久化、rollback 行为回归。

## 3. 温度

- [ ] TS1/RN6 10K-3435 温度点、开路、短路。
- [ ] TS2/RN5 10K-3435 温度点、开路、短路。
- [ ] 确认 TS3 的确为 NC；固件 heater 必须保持 disabled。
- [ ] 核对 TS4/RN4 BOM：图纸标 `TS4-MOS`，RN4=10M。确认实装器件后再决定是否启用 MOS NTC 软件保护。
- [ ] 在 TS4 未签核前确认软件不会把该通道当作可信 MOS 温度。

## 4. Balance / Open-Wire

- [ ] B1..B8 balance mask 与物理 cell 一一对应。
- [ ] 同时均衡限制、温升、采样扰动、起停压差、充电会话条件。
- [ ] Open-Wire 对 8S 有效通道正确，不误判未使用 VC9..VC20。
- [ ] 断线或异常压差时 balance 不得误开。

## 5. 低功耗 / 唤醒

- [ ] PA0/DI1/SW1 有效电平和去抖。
- [ ] PB1/INT-WK-MCU、PC0/ALARM、PC1/RESET 的有效电平与重复唤醒。
- [ ] PD3/CMNT-WK 的有效电平；通信进行中不得错误进入 deep sleep。
- [ ] SH3673510 Sleep/Wake 与 MCU deep sleep 无竞态，wake 失败保持 fail-safe。
- [ ] BLE connected/advertising/idle 三种状态的 suspend/deep-sleep 电流。

## 6. 产品参数发布签核

- [ ] 额定容量；当前 `CapacityFactory=116` 仅为继承迁移默认，不得直接作为 D014 量产值。
- [ ] 软件 OV/UV/OC/温度/压差 First/Second/Third/Recover/Filter。
- [ ] AFE HW OC/SC/温度 requested/effective。
- [ ] D014 独立产品 numeric ID 是否需要从历史 D11 wire/storage ID 分离，并同步 Windows 上位机。
- [ ] BLE 名称、硬件版本、序列号策略。

## 7. 发布证据

每项测试记录至少包含：板号/BOM、固件 commit、AFE 批次、分流实测、参数 requested/effective、仪器、环境、波形/日志、结论。Host contract、TC32 编译、MAP 和 cppcheck 通过不等于实板安全验收。
