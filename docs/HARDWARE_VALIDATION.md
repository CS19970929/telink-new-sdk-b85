# 四产品硬件验证与发布阻断项

本页是四产品交接的未关闭清单，不是已通过的测试报告。产品引脚/拓扑见各产品 reference；代码、host、TC32/资源通过均不能代替实板。每个产品/profile 单独验收，不能用另一板的通过结果销项。

2026-10-06 [原厂资料审查](afe-audit/20261006-0aaa8429/审查报告.md) 已记录软件反例和未签核项；导入资料没有完成问题修复或实板销项。六份 AFE 原手册与 D008 24S 原图的位置见 [资料指南](AFE_REFERENCE_GUIDE.md)。

软件修复状态与新写入规则见 [修复记录](AFE_AUDIT_REMEDIATION.md)。本页实板复选项仍未销项；用户选择保留现有复位启动策略和历史参数，相关发布阻断继续保留。

## 四产品共同项目

- [ ] 提供受控原理图/BOM、板号、AFE 型号/批次、装配串数、Rsense 实测、传感器配置及参数签核人。
- [ ] 空白设备/schema 3 新布局同编号/变编号/旧格式/回退分别检查 Config、State、Event，证明实际策略与八类有效编号及一个保留槽一致。
- [ ] Flash 写入及 sector 轮转掉电，启动失败 inhibit、重试与恢复；记录真实电源波形和参数读回。
- [ ] 充/放电正负、零点、增益、温漂与 SOC 积分方向；OV/UV/OC/温度触发及恢复的实际时间。
- [ ] AFE 失联、重配失败、watchdog、持续短路/负载未移除、wake 失败；测 Gate/Vgs 和负载电流，不只读寄存器。
- [ ] SH 三产品按 [恢复证据与采样资格](SH_RECOVERY_AND_FRESHNESS.md) 验证 C+/LOAD 切换、200 ms 等待、残压/反灌、900/2100 mV 板级判据、SC/OCC 并发及清 flag 失败静默；确认 1.2 s 温度启动和 400 ms ADC 活性时限。[D014 软件闭环](D014_SAFETY_LOOP.md) 与本轮DVC修复均已接入软件 Third OC 恢复输入，物理 Gate/负载仍需实测；DVC另验证512ms活性、反向电流和PB1/R6恢复证据。
- [ ] 明确软件过流之后的 MCU reset/掉电重启策略：当前软件 OC 仅在 RAM 锁存，AFE 重初始化保留，MCU 冷复位不保留。不得以持久故障历史代替实际输出锁存策略。候选行为、跨进程失败探针和台架验收条件见 [D014 软件闭环](D014_SAFETY_LOOP.md#mcu-复位边界探针2026-10-06)。
- [ ] 串口/BLE 长短帧、非法/部分帧、断连重连、日志采集与 OTA 互锁；RS485 产品测最后停止位与 DE。
- [ ] 正常/故障/OTA/Flash 压力下采样 gap、主栈/IRQ 栈水位、watchdog 和功耗；深睡恢复后配置和保护完整。
- [ ] 发布镜像身份和 CRC、实际 OTA 及回退；固定主机工具版本、固件 SHA/profile/BIN hash。

## D008 专属

参见 [D008 reference](D008_PRODUCT_REFERENCE.md)。

- [ ] 分别签核实际使用的 16S LFP / 20S NMC / 24S LFP；NMC 保护默认与 profile 的差异必须处理。
- [ ] SCD 已按产品确认默认开启 200 A / 请求 256 µs（名义量化延时 249.92 µs，整数读回 250 µs）；实测短路动作、Gate/Vgs、MOS SOA、持续短路/恢复及 OTA/回退。签核固定 WDT/body-diode/AUTO_DIODE 的动作与裕量。
- [ ] 同口单侧保护后反向续流，不出现周期性关断；真实 I2C dead-bus 的自主关 MOS。
- [ ] PC4 整机断电与 PA0/ACC 保电 deep sleep 分别验证保存失败、AFE shutdown 失败及重复唤醒。
- [ ] heater 在充电会话下的 ARMING/ACTIVE、拔充电器空载/反供；COW 诊断帧与均衡互锁。

## D011 专属

参见 [D011 reference](D011_PRODUCT_REFERENCE.md)。

- [ ] 10S、250 µΩ、通信供电/485 波形；PB5 fuse 全流程保持安全 LOW，普通联调不触发不可逆输出。
- [ ] TS3/TS4 实装 10K 与图纸差异留证，heater/MOS 温度和开短路实测。
- [ ] 加热、均衡、开线及 SC/OCD/OCC 恢复按本板参数验证；固定 UART PM 门禁单独记录。

## D013 专属

参见 [D013 reference](D013_PRODUCT_REFERENCE.md)。

- [ ] 补专属原理图/BOM，逐网核对所有继承 GPIO；4S/100 µΩ、SH 型号和 direct UART 目前只是代码输入。
- [ ] 核对 heater/balance/MOS NTC 禁用与真实装配一致；TS1/TS2 必需温度失效路径实测。
- [ ] 核对继承容量/OV/UV/OC/SC 参数、唤醒网络与通信方式后，才开展对应上板验收。

## D014 专属

以下为 **HS-D014-8S15A / TLSR8251 / SH3673510** 的原有详细清单，继续保持未完成。板级事实见 [D014 reference](D014_PRODUCT_REFERENCE.md)。

### 1. P0：首次上板

- [ ] VC1..VC8 逐通道电压正确；VC9..VC20 未使用通道不会进入 min/max、保护、SOC、balance、open-wire。
- [ ] 3×2mΩ 并联分流路径实测；确认等效阻值、Kelvin 采样方向、零点、增益和温漂。当前软件模型为 667µΩ。
- [ ] 充电/放电电流符号与 `charge_current_a10/discharge_current_a10` 一致；SOC 积分方向正确。
- [ ] AFE SPI Mode 3 / 500kHz：初始化、连续采样、CRC/ACK、异常恢复。
- [ ] CHG/DSG 正常开关、上电默认安全态、通信失效 fail-safe。
- [ ] RS485：PA1方向控制、PC2 TX、PC3 RX、PD4 CMNT-EN；最后停止位发完后再切回接收。

### 2. AFE 保护

- [ ] COV/CUV requested -> code -> effective -> readback -> 实际 MOS 波形。
- [ ] OCD1/OCD2/OCC requested -> code -> effective 使用 667µΩ，而不是 D011 的 250µΩ。
- [ ] SC 阈值/延时/LOADOFF 恢复；持续短路不能出现 clear -> reopen -> short 循环。
- [ ] WDT、FLAG2/WDT_FLG、Powerdown 行为按 SH36735xx 手册实测。
- [ ] 软件三级保护与 AFE HW profile 独立修改、独立持久化、rollback 行为回归。

### 3. 温度

- [ ] TS1/RN6 10K-3435 温度点、开路、短路。
- [ ] TS2/RN5 10K-3435 温度点、开路、短路。
- [ ] 确认 TS3 的确为 NC；固件 heater 必须保持 disabled。
- [ ] TS4 MOS 10K-3435：核对用户确认的实装 BOM 与图纸 RN4=10M 差异；做低/中/高温点和开短路实测。
- [ ] 验证 TS4 开短路使 `BMS_ERROR_TEMP_BREAK` 关断 CHG/DSG；MOS 75/85/95°C 软件保护及 80°C 恢复按实际持久参数验证。

### 4. Balance / Open-Wire

- [ ] B1..B8 balance mask 与物理 cell 一一对应。
- [ ] 同时均衡限制、温升、采样扰动、起停压差、充电会话条件。
- [ ] Open-Wire 对 8S 有效通道正确，不误判未使用 VC9..VC20。
- [ ] 断线或异常压差时 balance 不得误开。

### 5. 低功耗 / 唤醒

- [ ] PA0/DI1/SW1 有效电平和去抖。
- [ ] PB1/INT-WK-MCU、PC0/ALARM、PC1/RESET 的有效电平与重复唤醒。
- [ ] PD3/CMNT-WK 的有效电平；普通入口通信互锁与保护性深睡到期优先分别验证。
- [ ] SH3673510 Sleep/Wake 与 MCU deep sleep 无竞态，wake 失败保持 fail-safe。
- [ ] BLE connected/advertising/idle 三种状态的 suspend/deep-sleep 电流。
- [ ] 按 [保护性深睡](LOW_POWER_POLICY.md) 验证低压/AFE异常到期，BLE/UART/OTA、Flash/AFE失败及静态有效PAD均不阻止；SDK拒睡只重试深睡，实测功耗和唤醒后的保护恢复。

### 6. 产品参数发布签核

- [ ] 额定容量；当前 `BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH=116` 仅为继承迁移默认，不得直接作为 D014 量产值。
- [ ] 软件 OV/UV/OC/温度/压差 First/Second/Third/Recover/Filter。
- [ ] AFE HW OC/SC/温度 requested/effective。
- [ ] D014 独立 wire numeric ID 是否需要从历史 D11 wire ID 分离（存储已使用内部 tag 14），并同步 Windows 上位机。
- [ ] BLE 名称、硬件版本、序列号策略。

### 7. 发布证据

每项测试记录至少包含：板号/BOM、固件 commit、AFE 批次、分流实测、参数 requested/effective、仪器、环境、波形/日志、结论。Host contract、TC32 编译、MAP 和 cppcheck 通过不等于实板安全验收。

### 2026-09-21 低功耗失败闭环

- [ ] 按 [SH低功耗修复说明](SH_LOW_POWER_FAILURE_HANDLING.md) 验证逐级休眠失败、SLEEP ACK丢失、SPI持续失联功耗、OTA/UART互锁、PAD竞争及BLE下200ms采样周期；Host通过不关闭此项。
