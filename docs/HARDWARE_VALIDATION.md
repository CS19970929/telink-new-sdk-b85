# D011 实板验证与发布阻断项

本文件只保留 **HS-D011-10S50A-V1 / TLSR8251 / SH3673510** 的未完成实板证据。硬件/源码配置见 `D011_PRODUCT_REFERENCE.md`。

## 1. P0/P1 发布阻断

- [ ] 10S VC1..VC10 逐通道电压、上部 VC11..VC20 处理、断线和噪声验证。
- [ ] 250 µΩ 电流路径的零点、方向、增益、温漂；确认 SOC 积分方向。
- [ ] OV/UV/OCD1/OCD2/OCC/SC 逐项 requested -> code -> effective -> readback -> 实际延时/MOS 栅极波形。
- [ ] SC 在持续短路下不得形成“clear -> 重开 -> 再短路”循环；必须验证 LOADOFF/负载移除、稳定时间和 readback。
- [ ] SH WDT code0（约32.34s）触发、FLAG2/WDT_FLG清除窗口和 Powerdown 行为按手册实测。
- [ ] C-3V3/`CMNT-EN` 供电、RS485 DE//RE、PD3 `CMNT-WK` 唤醒形成完整状态机。

## 2. GPIO/板级

- [ ] PD4 `CMNT-EN`：上电/关电/休眠的 active level、C-3V3 稳定时间。
- [ ] PA1 `485-EN`：最后停止位发送完后再释放方向，连续帧/异常帧下不截断。
- [ ] PB1 `INT-WK-MCU`、PC0 `ALARM`、PD3 `CMNT-WK`：有效电平、去抖、重复唤醒、通信进行中禁止休眠。
- [ ] PC1 `RESET`：确认 MCU 侧实际方向和 SH RESET 电气行为；不要根据宏名 `RESET_OUT` 直接认定。
- [ ] PB4 `HT-CHG` 加热输出及功率回路。
- [ ] PB5 `HT-RF-EN`：验证为不可逆保险丝触发路径；量产普通运行必须保持 LOW，只有独立签核状态机可允许触发。
- [ ] PC4 `DB-LED1` 极性和休眠默认态。

## 3. 温度/BOM

- [ ] TS1/TS2 10K-3435 温度精度、开路、短路。
- [ ] 对 RN3/RN4 做生产资料闭环：原理图标10M、用户确认实际装10K；以 BOM/实物料留证并修订图纸。
- [ ] TS3 heater-MOS 与 TS4 power-MOS 的物理位置、热耦合和独立软件保护阈值。
- [ ] AFE HW TEMP 当前只启用 TS1/TS2 时，确认 TS3/TS4 软件保护不会被遗漏。

## 4. SH3673510 保护/恢复

- [ ] SCONF1..7、0x47/0x48 上电实际 readback 与 `D011_PRODUCT_REFERENCE.md` 一致。
- [ ] AFE HW V2 enable_mask 对 SCONF5 OCC_EN / SCONF6 OV/UV/OCD/SC/TS1/TS2 的实际覆盖。
- [ ] OCD/OCC 恢复必须验证实际故障已移除，而不是仅凭关 MOS 后的0A。
- [ ] Sleep `0xAA` 后 CADC/WDT/保护/FET/charge pump/balance 状态与手册一致；wake失败必须保持 fail-safe。

## 5. 软件/硬件保护参数独立性

- [ ] 修改 `g_tParam.protect` 的 65 个软件参数之一，AFE HW profile requested/effective 不变。
- [ ] 修改 AFE HW 35-word profile，软件三级参数不变。
- [ ] 非法 profile、掉电、persist/apply/readback 失败时验证 rollback；rollback失败必须暴露 `CONFIG_INCONSISTENT`。

## 6. Balance / Open-Wire / 通信 / OTA

- [ ] 10S balance mask、同时均衡约束、温升、采样干扰、停止条件。
- [ ] Open-Wire 判定与实际断线试验。
- [ ] RS485 19200 等实际产品通信参数、DMA/方向控制、错帧、超时、唤醒。
- [ ] BLE/Modbus 参数、Factory Session、OTA、Flash 掉电恢复端到端回归。

## 7. 证据格式

记录板号/BOM、固件 commit、SH3673510 批次/版本、Rsense实测、AFE requested/effective、仪器、环境、波形/日志和结论。源码契约/TC32 CI 成功不等于实板安全验收。