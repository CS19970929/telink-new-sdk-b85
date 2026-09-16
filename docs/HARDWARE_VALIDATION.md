# D008 实板验证与发布阻断项

本文件只保留当前 **HS-D008 / TLSR8251 / DVC1124-2** 尚需实板证据的项目。2026-09-17 已核对用户提供的原理图和产品说明，连线/用途见 [D008_PRODUCT_REFERENCE.md](D008_PRODUCT_REFERENCE.md)。图纸已提供不代表实板已通过，以下复选框保持未完成。

## 1. P0 发布阻断

- [ ] 24S LFP 实板逐通道确认 VC1..VC24；20S NMC 实板确认 Cell21..24 被软件/AFE mask 且不参与 min/max/保护/SOC。
- [ ] 用已知电流校准 200 µΩ 路径的方向、零点、增益和温漂。
- [ ] 逐项验证 COV/CUV/OCD1/OCD2/OCC1/OCC2 的 requested -> code -> effective -> readback -> 实际关断延时和 MOS 栅极动作。
- [ ] 确定并签核 SCD threshold/delay；未签核前保持关闭。
- [ ] 验证 I2C dead-bus：SDA/SCL 卡死、NACK、CRC异常、AFE掉电；证明 MOS 最终安全路径。若采用 DVC WDT 或 PD7 power-cycle，必须记录时序和恢复竞态。
- [ ] 24S LFP / 20S NMC 分别签核容量、OV/UV、OC、温度、SOC OCV/端点；不得继承 D3PRO 历史默认作为产品依据。

## 2. IO/电源

- [ ] PD7 `MCU-AFE-EN`：上电、reset、sleep/wake、重复 reinit 的实际电平和 AFE 电源状态。
- [ ] PB1 `CHG-IN`：按负载检测验证 Q40/C− 电路在空载、接负载、MOS 开/关及抖动下的电平；暂不加入业务逻辑。不得继续以“充电器在位”或 MCU 断电后的 PAD 唤醒验收。
- [ ] PA0 `ACC-MCU`：确认 Q57、CN3 及选装支路下的输入范围/电平；D008 无独立开关，ACC 业务逻辑暂不实现，不能验收成 key 开关策略。
- [ ] PD4 `MCC-EN-RF` / PA1 `MCC-EN-HT`：核对加热/熔断链、R74 等 NC 装配和有效电平；不可逆动作仅在安全夹具上验证。
- [ ] PC4 `MCU-LDO`：验证用户确认的低电平关断 MCU 3V3；记录启动保持时点、供电下降/上升、掉电复位和重复开关机。
- [ ] 电路唤醒 MCU：逐项记录真实外部触发、C−/B− 电位、最短脉宽和保持时间；验证断电时调试器、OWC、SWS、SDA/SCL 不会反向供电，不能用 PB1 GPIO 唤醒替代。
- [ ] PC2/PC3 OWC 与 BLE/串口/bus mux 并发，不发生总线抢占。
- [ ] SOC25/50/75/100 和 BLUE LED 的极性、休眠默认态和漏电。

## 3. DVC 专属

- [ ] GP1=NTC2（加热区）、GP4=NTC1（功率 MOS 区）：图纸连线已确认，实际热位置/耦合仍需 PCB；验证 `SNC103B13435F0603E` R-T、温箱和开短路 fail-safe。
- [ ] 明确 GP2/GP3 BOM：若未装 NTC，Product Profile 必须改为 OFF；禁止悬空通道参与保护。
- [ ] 0x53/0x54 mask readback 与当前固定策略一致：I2C WDT=4 s、timeout-close CHG/DSG 允许、Body-Diode=80 µV 已在源码启用；验证自主动作，不能标成“当前关闭”。
- [ ] Body-Diode=80 µV 验证双向续流与栅极；Current-Wake、Core-OT 当前仍关闭，若要改变必须另做参数/时序签核。500 mA suspend 退出要求不等于启用 AFE current-wake。
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

- [ ] 分别测 Active、广播、连接、suspend、AFE shutdown + MCU power off 的电流；不能把 SDK deep sleep 和整个 MCU 断电混为同一状态。
- [ ] 关机全链路：必要 State/Event 保存 → 加热/均衡/FET 安全状态 → AFE shutdown → 无后续 I2C → PC4 低 → MCU 断电；覆盖保存失败、AFE 命令失败、关机中外部触发和重复执行。
- [ ] 复电全链路：硬件恢复 MCU 电源 → 电源保持/安全 GPIO → PD7/I2C 唤醒 AFE → 固定配置与 HW profile 重申/验证 → 新有效样本资格 → 允许的输出；避免旧请求提前开 MOS。
- [ ] 记录 SDA/SCL 唤醒幅值/脉宽及 AFE 就绪时限；核对官方 DVC1124-2 手册，不能将普通 I2C 读取视为已经完成 shutdown 唤醒。
- [ ] suspend 双向电流退出：两方向分别测试 0/199/200/499/500/501 mA、跨零、噪声、短脉冲；500 mA 包含边界；记录精度、延迟、阈值抖动和采样间隔。
- [ ] suspend 内 AFE 测量周期、4 s watchdog、保护处理及 LED/OWC/BLE 活动兼容；无效样本不得当作零电流静置。进入延时、迟滞与响应预算尚待定义。
- [ ] SOC：有效静置不足/达到 10 min、200..499 mA 非静置、丢样/陈旧帧、AFE 重初始化、不同 suspend 间隔、计数器回绕；校准遵守 [SOC.md](SOC.md) 的方向和速率限制。
- [ ] MCU 断电恢复：不继承 RAM 静置计时，不用未知关机时长补积分/OCV；保存失败与重启应保持可解释的 State 恢复行为。
- [ ] OTA A/B 中断点、Flash 保留区、参数兼容迁移和回滚；OTA/参数持久化进行中不能被旧 key/charger 路径直接切断电源。

## 7. 证据格式

每次实测至少记录：板号/BOM、DVC型号与版本、固件 commit、24S/20S profile、AFE HW profile requested/effective、仪器、环境、波形/日志、结论。CI 绿色不能替代以上实板证据。