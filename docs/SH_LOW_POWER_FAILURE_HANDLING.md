# SH 低功耗失败处理与采样调度

适用 monorepo 的 D011/D013/D014。读 `bms/app/app.c` 的 SH 分支、`bms/core/bms_afe_guard.c` 和 SH backend/control。D008 的 AFE shutdown/PC4/ACC 路径另见其产品 reference。

## 当前调用链

`blt_pm_proc()` 评估现有产品资格 → `app_note_sleep_and_enter_deepsleep()` 检查互锁 → `bms_afe_sleep()` → SH driver/control → 成功后才尝试 MCU deep sleep。

`SH3673510_FIXED_UART_BLOCKS_PM=1` 保留原固定 UART 门禁，默认配置可能先被此条件阻止；函数存在、host 场景通过不说明实机已经能进入该路径。不能为了省电删除门禁。

- OTA、SDK Flash session、UART TX/RS485 active 和固定通信门禁先于 AFE 休眠副作用检查。
- AFE 事务前后复核既有 PAD wake 条件，不新增唤醒极性或休眠入口。
- SH sleep 返回成功/失败；任一步写入失败不进入 MCU deep sleep。
- 发 SLEEP 前标记需恢复；ACK 丢失时下次采样仍执行 NORMAL、配置恢复和 FET OFF，成功后才继续。
- 旧采样、命令缓存和恢复资格作废，但保留故障锁存；不能把 requested OFF 当写入成功。
- guard watchdog bus silence 期间禁止 SPI，休眠请求不重置静默资格；保留有界恢复监督。
- 现有休眠资格计数及至少 3 s 的失败重试间隔避免忙循环；时间计算需保持 tick 回绕语义。

sleep 事件只记录进入尝试。最后时刻 PAD 变化或 SDK 拒睡仍可能使 MCU 没有进入；事件不是功耗证明。SH 在 sleep 前尝试事件刷新，存储失败并不等同所有路径都永久禁止低压休眠；D008 显式保存的门禁不能照搬。

## 采样与验证

三种 SH 产品使用同一 200 ms sample wakeup 调度：callback 只置位，采样、SOC、MOS 和诊断在主循环；超时合并，不补造多帧。软件保护计数仍按名义 200 ms 样本，真实 BLE/Flash 阻塞可能影响墙钟响应。

使用 [构建指南](BUILD_AND_TEST.md) 的环境选择产品后运行 `sh3673510_sleep_host_check.py`、`sh3673510_sample_schedule_host_check.py`、`sh3673510_recovery_host_check.py`，公共变化跑完整 runner 及四目标 link/resources。测试有 SDK/驱动桩和隔离 PM 门禁的故障场景，不是新增支持模式；日常不运行会生成 BIN 的 `bms.py ci`。

## 实板未关闭项

逐产品验证 SLEEP/NORMAL 波形、死 SPI 的 watchdog/功耗、恢复时 CHG/DSG Gate、UART 最后停止位、OTA/Flash 互锁、全部 PAD 竞争及广播/连接下采样周期。D013 先补原理图/BOM；D014 heater/TS3 禁用、TS4 MOS NTC 必需。保留 MCU 监督可能增加死总线耗电，必须测量而不能假定 AFE 已睡眠。清单见 [HARDWARE_VALIDATION](HARDWARE_VALIDATION.md)。
