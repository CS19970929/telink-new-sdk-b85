# 四产品保护性深睡

适用 D008 / D011 / D013 / D014。状态和计时只由 `bms/app/app_power.c` 持有。

| 条件 | 连续时间 | 到期动作 |
|---|---|---|
| 新鲜有效样本的最低单体低于 2550 mV，或低于产品 `BMS_SLEEP_LOW_CELL_MV` | 1 小时；跨 2550 mV 不清零 | 强制 MCU deep sleep |
| 最低单体低于 3000 mV 且有效电流未显示充电 | 24 小时 | 强制 MCU deep sleep |
| AFE 报错，或无法取得新鲜有效测量 | 30 分钟 | 强制 MCU deep sleep |

低压与 AFE 异常分别计时；低压恢复或进入不同延时区域重置低压计时，
AFE 恢复且测量有效才清零异常计时。BLE、UART、OTA 和 Flash 状态不清零计时。
阈值与延时沿用产品头文件；未改变协议、参数编号或持久化布局。

到期后立即锁存：撤销输出授权，尝试 AFE sleep；允许 Flash 写入且未 OTA 时，
State 和事件各尝试保存一次。保存、AFE、BLE 控制失败都不阻止 MCU deep sleep。
OTA 可以被中断。AFE guard 的 watchdog 总线静默门禁保持，禁止绕过静默访问 AFE。

已有 PAD 唤醒输入设置为当前电平的反向，静态有效电平不能拒睡。
D008 保持 PC4/MCU_LDO 高，以 ACC/负载输入变化唤醒；SH 使用原有开关、INT_WK、
ALARM、RESET 输入变化唤醒并关闭 CMNT_EN。SDK 拒睡返回后仅重设 PAD 并重试，
不恢复 BLE/采样业务、不重复保存或发送 AFE 命令。真正 deep sleep 唤醒走完整启动。

普通 suspend、D008 ACC/显式命令关机、SH 开关休眠保留原门禁；保护性深睡优先。
[实时诊断](SLEEP_STATUS.md) 中保护性原因的阻止位为零，`COMMITTED` 只代表已提交动作。

原资料依据：DVC1124-2 RM V1.2 PDF 第 7 页 CST sleep/shutdown；SH36735XX V1.0A
PDF 第 10 页 SLEEP/NORMAL 和唤醒。见 [AFE资料指南](AFE_REFERENCE_GUIDE.md)。
AFE 失联时无法确认芯片已睡或 MOS 已关。未执行编译、host 或实板测试；实际功耗、
PAD 竞争、OTA 中断恢复及唤醒后的保护恢复需要实板验证。
