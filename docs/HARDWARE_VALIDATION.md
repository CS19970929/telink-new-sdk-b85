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
## 2026-09-17 低功耗实现后的验收补充

源码路径与主机测试见 [D008_POWER_SOC_IMPLEMENTATION.md](D008_POWER_SOC_IMPLEMENTATION.md)。下列事项仍全部为 `TODO_VERIFY_HW`，不得由 CI 通过自动勾选：

- [ ] 示波器同步记录 PC4/3V3/PD7/SDA/SCL：冷启动电源保持、AFE shutdown 命令完成、PC4 最后拉低、整机真正掉电，排除调试器/串口反向供电。
- [ ] 外部唤醒后 MCU 冷启动，经既有 I2C 唤醒/reset/固定配置读回及三帧 guard 资格，再恢复 MOS 请求；验证连续循环及失败分支。
- [ ] ±199/200/499/500/501 mA 实际电流校准、32k 计时误差、广播/连接/总线/Flash 负载下最坏采样间隔及退出 suspend 延迟；周期目标 200 ms，软件拒收 >400 ms 间隔。
- [ ] Flash 失败与 I2C shutdown 失败均保持 PC4 高；重复失败限速，不能产生密集擦写。
- [ ] 24S LFP 与 20S NMC 数值回放及断电恢复，不补算未知时间，不继承静置资格；低功耗电流实测。
- [ ] PB1 电平变化不触发 MOS/key/charger 策略；ACC按最新独立休眠规则验收；没有独立充电源资格时自动加热保持关闭。

## 存储 schema 2 实板验收（TODO_VERIFY_HW）

- [ ] 精确 Flash MID/BOM、温压条件、program/erase 最大耗时与 200 ms 采样/BLE 的干扰；采集 `bms_storage_platform_get_diagnostics()`，不以 mock 时间作为实测。
- [ ] OTA 单独提高 SW/AFE/SOC-config/system/SOC-state/Event/runtime revision；验证保留无关域和重复启动。schema 1→2 按开发期策略重置，无旧参数迁移。
- [ ] 每个域写入/擦除/commit 时断电；分别验证 Config 完成而 State/Event 未完成时的启动输出门禁。
- [ ] State/Event pending 时突发掉电与受控 PC4 断电；受控路径失败保持 PC4 高。记录正常约 60 s 合并窗口和失败期间更长的丢失窗口。
- [ ] AFE 应用/readback 失败、新三帧资格、实际 MOS Gate；失败时不能仅凭 Requested 或 CHGF/DSGF 声称物理关断。
- [ ] 连续事件风暴、反复启动、手动参数写入、写校验失败 5 s 退避与日擦写量，按 schema 2 几何重新评估寿命。

## ACC 保电深睡眠（TODO_VERIFY_HW）

- [ ] PA0高稳定200ms后AFE shutdown，PC4及3V3始终保持高/供电；实测深睡眠电流。
- [ ] PA0低电平PAD唤醒，完整初始化AFE、三次采样资格后正常输出；测量接点抖动、上电时高/低电平、进入瞬间反转。
- [ ] OTA期间延后；BLE连接先断开、串口/一线通忙时不截断事务；保存/AFE失败不进入深睡眠。
- [ ] 不改变显式deepsleep_en和低压PC4断电路径，验证两种休眠电平及唤醒方式区别。

## 2026-09-17 电流保护恢复更新

用户最新授权替代此前“PB1 暂不实现负载检测”和“SCD 不自动清除”的限制：PB1 低=负载在、高=负载移除，仅在有效 DSGF=0 时判定；软件三级/硬件放电过流及短路在负载移除或可靠充电后恢复，充电过流等待 30 s。保留单侧 AUTO_DIODE 续流；运行期间锁存不因 AFE reinit 丢失，MCU 复位按原启动策略。见 [恢复实现](D008_CURRENT_RECOVERY.md)。实板仍为 TODO_VERIFY_HW。
