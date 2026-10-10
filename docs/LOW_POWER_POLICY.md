# 四产品保护性休眠

适用 D008 / D011 / D013 / D014。状态和计时只由 `bms/app/app_power.c` 持有。

| 条件 | 连续时间 | 到期动作 |
|---|---|---|
| 新鲜有效样本的最低单体低于 2550 mV，或低于产品 `BMS_SLEEP_LOW_CELL_MV` | 低于 2550 mV 为 1 小时，其余使用 `BMS_SLEEP_LOW_SECONDS`；跨分界保留已累计时间 | D008 关闭 MCU 供电；SH MCU deep sleep |
| 最低单体低于产品 `BMS_SLEEP_NORMAL_CELL_MV` 且有效电流未显示可靠充电（`current_ma >= -200`） | `BMS_SLEEP_NORMAL_SECONDS` | D008 关闭 MCU 供电；SH MCU deep sleep |
| AFE 报错，或无法取得新鲜有效测量 | 30 分钟 | D008 关闭 MCU 供电；SH MCU deep sleep |

低压与 AFE 异常分别计时。低压区域只由有效新鲜采样改变；一旦确认低压，
短暂无新样本、旧缓存或 AFE 故障均不能证明电压已恢复，保留区域并继续墙钟计时。
专用低压与低压无充电条件独立累计。跨 `BMS_SLEEP_LOW_CELL_MV` 只解除专用低压条件，
不能清除持续低于 `BMS_SLEEP_NORMAL_CELL_MV` 且无可靠充电的计时资格。
有效恢复或可靠充电仅解除对应条件，显示当前最早到期的一项。
AFE 恢复且测量有效才清零异常计时。BLE、UART、OTA 和 Flash 状态不清零计时。
阈值与延时沿用产品头文件；未改变协议、参数编号或持久化布局。

2026-10-09 D008 实板发现：PM 的 400 ms 新鲜度边界会短暂失效约 41 ms，旧逻辑
将它等同于低压解除，导致一小时计时反复归零。修复保持普通 suspend 的严格新鲜度
门禁和独立 AFE 30 分钟计时，不提高超时、不放宽输出资格。24 小时区的充电判据
同时对齐 SOC 可靠电流区：仅小于 -200 mA 才视为可靠充电，小偏移不能取消计时。
低于 2800 mV 的一小时策略仍不排除充电，这是独立产品策略。

回归场景分别执行 D008 应用 PM 入口和 SH 共用保护性入口：反复跨过 400 ms，
墙钟累计 3600 秒必须到期；无效缓存不能取消，真实有效恢复可以取消；保留 AFE
异常优先、PAD 返回保持、tick 回绕及保存失败路径。host 的时钟推进不是实板睡眠证明，
实际一小时到期、功耗、Gate 和 PAD 唤醒仍须独立实板验收。

到期后立即锁存：撤销输出授权，D008 所有原因尝试 AFE Shutdown，
SH 产品仅尝试 AFE Sleep，不发送 Powerdown 命令；允许 Flash 写入且未 OTA 时，
State 和事件各尝试保存一次。保存、AFE、BLE 控制失败都不阻止 D008 断电或 SH MCU deep sleep。
OTA 可以被中断。AFE guard 的 watchdog 总线静默门禁保持，禁止绕过静默访问 AFE。

D008 的低压和 AFE 异常共用保护性入口：完成上述前置动作后，最后拉低
PC4/MCU_LDO，关闭 MCU 供电，不启用 PA0/PB1 PAD 唤醒。只有 ACC 开关休眠保持
PC4 高，并以 PA0 低电平唤醒。显式命令关机继续拉低 PC4。
真正断电后的重新启动依赖外部硬件供电恢复，不能由 MCU GPIO、BLE 或定时器唤醒；
具体充电/开关复电条件须实板确认。若调试器等外部供电使 MCU 未实际掉电，保护性
入口仅执行 200 ms 定时 Suspend 保持，不恢复业务、不重复保存或发送 AFE 命令。

SH 保持原有深睡：关闭 CMNT_EN，将开关、INT_WK、ALARM、RESET 等 PAD 输入配置为
入睡时电平的反向，静态有效电平不能拒睡。SDK 返回后仅重设 PAD 并重试，
不恢复 BLE/采样业务、不重复保存或发送 AFE 命令。真正 deep sleep 唤醒走完整启动。

2026-10-10 按产品要求将 D008 的全部 AFE 低功耗请求统一为 Shutdown，包括低压和
AFE 异常保护性深睡；开关及命令关机原本已用 Shutdown。公共 `bms_afe_sleep()`
复用已有受保护的 `bms_afe_enter_shutdown()`，移除 DVC Sleep 命令入口：均衡关闭、
FET 关闭及 shutdown 应答成功后保持总线静止。通信 inhibit、命令失败或 watchdog
bus silence 不阻止保护性 MCU 深睡，不追加绕过 guard 的命令。
SH 软件仅请求 Sleep；按用户确认关闭欠压自主 Powerdown（`PD_EN=0`），保持
`PD_CTL=0`。这是固定运行配置，启动和 Sleep 唤醒重配时写入并回读验证，不依赖
OTA 参数更新组或 Config journal。`UV_EN` 欠压关 MOS 保护、其它硬件保护及 WDT
保留。SH36735XX CV1.0A 原 PDF 第 10 页规定内部过温和 WDT 故障也可独立触发
Powerdown；关闭 PD_EN 不能禁止这些芯片自保行为。普通 BLE Suspend 不发送
AFE 低功耗命令，D008 AFE 继续采样。
上述 AFE 模式统一改动最初保留 PC4 高及 PA0/PB1 反向电平唤醒，现已按用户要求
改为本页的 D008 保护性断电策略。依据 DVC1124-2 DS V1.1 PDF 第 12 页功能模式、
RM V1.2 PDF 第 7 页 CST；PC4 供电控制见 HS-D008-24S100A-V1 原理图第 1 页和
用户确认，不能仅由芯片典型电流推断整板功耗或硬件复电行为。本次同步 host 场景，
覆盖最终拉低 PC4、保存/AFE 失败仍断电、外部供电保持及 ACC 不断电，但未执行；
未编译、生成 BIN 或连接实板。下述历史验证不覆盖本次断电策略改动。

普通 suspend、D008 ACC/显式命令关机、SH 开关休眠保留原门禁；保护性深睡优先。
[实时诊断](SLEEP_STATUS.md) 中保护性原因的阻止位为零，`COMMITTED` 只代表已提交动作。

原资料依据：DVC1124-2 RM V1.2 PDF 第 7 页 CST sleep/shutdown；SH36735XX V1.0A
PDF 第 10 页 SLEEP/NORMAL 和唤醒。见 [AFE资料指南](AFE_REFERENCE_GUIDE.md)。
AFE 失联时无法确认芯片已睡或 MOS 已关。2026-10-10 的相关 host、四产品开发 ELF
与资源检查见 [优化及固定提交验证记录](OPTIMIZATION_20261010.md)。软件检查不能证明
实际功耗、PAD 竞争、OTA 中断恢复及唤醒后的保护恢复；这些仍需实板验证。
