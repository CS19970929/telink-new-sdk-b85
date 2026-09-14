# D008 实板验证与发布阻断项

本文件只保留当前 **HS-D008 / TLSR8251 / DVC1124-2** 尚需实板证据的项目。产品配置事实见 `D008_PRODUCT_REFERENCE.md`。

## 1. P0 发布阻断

- [ ] 24S LFP 实板逐通道确认 VC1..VC24；20S NMC 实板确认 Cell21..24 被软件/AFE mask 且不参与 min/max/保护/SOC。
- [ ] 用已知电流校准 200 µΩ 路径的方向、零点、增益和温漂。
- [ ] 逐项验证 COV/CUV/OCD1/OCD2/OCC1/OCC2 的 requested -> code -> effective -> readback -> 实际关断延时和 MOS 栅极动作。
- [ ] 确定并签核 SCD threshold/delay；未签核前保持关闭。
- [ ] 验证 I2C dead-bus：SDA/SCL 卡死、NACK、CRC异常、AFE掉电；证明 MOS 最终安全路径。若采用 DVC WDT 或 PD7 power-cycle，必须记录时序和恢复竞态。
- [ ] 24S LFP / 20S NMC 分别签核容量、OV/UV、OC、温度、SOC OCV/端点；不得继承 D3PRO 历史默认作为产品依据。

## 2. IO/电源

- [ ] PD7 `MCU-AFE-EN`：上电、reset、sleep/wake、重复 reinit 的实际电平和 AFE 电源状态。
- [ ] PB1 `CHG-IN`：低有效、1M pull-up、deep-sleep wake 在插拔充电器/抖动下可靠。
- [ ] PA0 `ACC-MCU`：开关输入有效电平与去抖。
- [ ] PD4 `MCC-EN-RF`、PA1 `MCC-EN-HT`、PC4 `MCU-LDO`：确认受控电源对象、active level 和低功耗所有权。
- [ ] PC2/PC3 OWC 与 BLE/串口/bus mux 并发，不发生总线抢占。
- [ ] SOC25/50/75/100 和 BLUE LED 的极性、休眠默认态和漏电。

## 3. DVC 专属

- [ ] GP1/GP4 板载 NTC 与 `SNC103B13435F0603E` R-T 数据/温箱一致；开路/短路 fail-safe。
- [ ] 明确 GP2/GP3 BOM：若未装 NTC，Product Profile 必须改为 OFF；禁止悬空通道参与保护。
- [ ] 0x53/0x54 mask readback 与预期一致；若未来启用 WDT timeout-close / body-diode，需要重新审核整个 mask policy。
- [ ] Body-Diode、Current-Wake、Core-OT 等当前关闭功能只有在单独试验/产品签核后才能启用。
- [ ] SCD fault 的负载移除、debounce、clear/readback、controlled retry 方案；当前不自动清除是预期安全行为。

## 4. Open-Wire / Balance

- [ ] 用真实断线故障确定 Open-Wire evaluate 判据，覆盖首/中/末通道和瞬态噪声。
- [ ] 验证 Open-Wire 期间 Balance 确实暂停并正确恢复。
- [ ] 验证 DVC balance 约60s auto-clear 与软件45s续期，覆盖充电、停止充电、保护触发、无效采样、温升和测量干扰。

## 5. Flash / 参数事务

- [ ] AFE HW V2 完整35-word写入：正常、非法值、掉电、persist失败、apply失败、readback失败。
- [ ] 故障注入验证 rollback 成功路径和 `CONFIG_INCONSISTENT` 路径；重启后不得把不一致状态静默当正常配置。
- [ ] 软件三级保护参数与 AFE HW profile 独立：修改任一侧，另一侧 Flash/readback 必须保持不变。

## 6. 低功耗 / OTA / 系统

- [ ] 正常运行、广播、连接、deep sleep 电流；记录唤醒源和测试条件。
- [ ] sleep/wake 前后 AFE/FET/通信/SOC 恢复顺序，无自动重开 MOS 竞态。
- [ ] OTA A/B 中断点、Flash 保留区、参数兼容迁移和回滚。

## 7. 证据格式

每次实测至少记录：板号/BOM、DVC型号与版本、固件 commit、24S/20S profile、AFE HW profile requested/effective、仪器、环境、波形/日志、结论。CI 绿色不能替代以上实板证据。