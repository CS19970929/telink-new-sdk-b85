# SH3673510 低功耗失败传播与采样调度

2026-09-21，适用 D011 / D013 / D014 的 Common BMS 分支。仅处理 MCU 软件调用链；没有更改板级 GPIO、AFE 寄存器值、保护阈值、参数布局或 OTA 格式。

## 问题与修复

原 `sh3673510_control_sleep()` / backend / guard 都返回 void。控制器未 ready、wake 有效、关均衡/关 FET/设置 charger wake/SLEEP 写失败时，app 仍继续 MCU deep sleep。OTA 的 suspend-disable 检查位于这些显式 deep-sleep 入口之后，无法阻止它们。

SH 休眠链改为返回成功/失败，任一步失败都不进入 MCU deep sleep。DVC 历史兼容编译单元保留 void API；这不是 D008 shutdown 路径的修改。成功只表示现有驱动事务完成，不等于物理休眠电流/Gate 测量。

- OTA、SDK Flash session、bus mux 非 idle 或 UART TX busy：在任何 AFE 休眠副作用之前退出。
- 进入前及 AFE 事务后检查现有产品的 PAD wake 条件；不发明新唤醒极性。
- AFE backend 立即失效采样与 FET command cache，丢弃恢复资格计数，但保留故障锁存。失败不得把均衡 requested OFF 伪装成已成功写入。
- 发 SLEEP 前即记录需要恢复。即使 ACK 丢失、MCU认为写失败，下一次采样仍需 NORMAL + runtime/protection restore + FET OFF，成功后才继续采样。恢复失败进入既有 guard 故障处理。
- guard 的 watchdog 静默窗口禁止任何 SPI，休眠请求不得重置其计数；MCU继续服务既有的有界 wait/reinit 路径，不把“SPI不可用”当作“AFE已睡眠”。
- 低压/开关/通信异常原有资格时间保持。计数饱和，到期后仅在实际进入成功时清零；失败每至少3秒重试，避免一次瞬态失败又等待整段小时级延时，也避免忙循环。正常 uint32 tick 回绕有效。
- 事件 sleep 标记只在 AFE 准备成功后记录，仍是 MCU 进入尝试；PAD 在最后时刻变化仍可令 SDK 拒绝进入，不能把该事件当成整机休眠成功证明。

## D013 的调度遗漏

D011/D014 已向 BLE PM 注册200ms采样唤醒；D013原先仅在 main loop 检查200ms elapsed，BLE suspend 没有相应 app wake deadline。其保护与恢复仍用每帧200ms计数，实际墙钟周期可能被 BLE 间隔拉长。

D013复用现有 `app_sample_wakeup` / `app_schedule_sample_wakeup` / `app_sample_task`：callback仅置位，采样/SOC/MOS/diagnostics仍在主循环执行；延迟只合并为一次，不补造多帧。不改 D013 GPIO/NTC/身份。三产品执行同一 scheduler Host 测试，真实 BLE 下的抖动仍待测。

## 自动验证

```text
python tests/sh3673510_sleep_host_check.py
python tests/sh3673510_sample_schedule_host_check.py
python tests/run_host_regression.py
python bms_tools/bms.py ci -j 4
```

新测试提取并编译当前生产 C 函数，外围只模拟时间、SDK和驱动事务结果，不在 Python 重写状态机。休眠测试覆盖逐次写失败、SLEEP失去回执但芯片可能已接受、wake竞争、SDK拒睡、恢复失败、旧样本/命令缓存、总线静默、连续故障、限速、整数溢出与回绕。初始原代码有43项断言失败；修复后66项断言通过。调度测试覆盖 deadline、重复callback合并、无效样本、执行超时及tick回绕。

本次三产品各18组Host回归通过；SW/HW的1/1、1/0、0/1、0/0均通过TC32 clean build/check-fw/MAP/manifest/verify，均0 warning/error。默认配置cppcheck各114项style，warning/error和应用编译单元coverage gap均为0；未执行MISRA。

## 必须留给实板的边界

持续死SPI时，本修复选择保留MCU监督与恢复，而不是假定AFE睡眠成功。这样可能增加故障下耗电；需验证硬件WDT/Powerdown、UV、供电拓扑后才能确定独立强制关电策略，不能让软件臆造一个成功返回值。本次没有改变低压与开关/PAD优先级。

需按本产品 `HARDWARE_VALIDATION.md` 验证 SLEEP/NORMAL 波形、失联功耗、恢复时CHG/DSG Gate、BLE OTA中开关变化、UART最后停止位、全部wake引脚竞争，以及200ms采样在广播/连接/长latency下的实际周期。D013先补原理图/BOM；D014保留heater/TS3禁用及未签核MOS NTC边界。Host/TC32均不能替代这些证据。
