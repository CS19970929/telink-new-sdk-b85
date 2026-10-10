# SH 低功耗与异常处理

适用 D011/D013/D014。低压、AFE 异常强制深睡见 [四产品保护性深睡](LOW_POWER_POLICY.md)。

- 保护性条件到期：`app_protective_sleep_poll()` → 撤销输出授权、尝试 AFE sleep → MCU deep sleep。固定 UART、TX、BLE、OTA、Flash、AFE 失败均不阻止。
- 普通开关休眠：`app_enter_switch_deepsleep()` 保留 OTA/Flash/固定 UART/TX/PAD 门禁与三秒失败退避；AFE sleep 失败仍保留请求；所有开关深睡尝试均需执行 AFE sleep，没有跳过 AFE 的参数分支。
- 两条路径均通过 guard；watchdog 总线静默期间不访问 SPI，不重置静默资格。
- AFE 命令可能成功但 ACK 丢失；后端保留需恢复标记和原唤醒恢复流程。保护性深睡唤醒走完整启动，不凭旧缓存恢复输出。
- 保护性深睡的 SDK 拒睡返回只重试 PAD/MCU 深睡，不能恢复正常业务或重复写 Flash。

三种 SH 产品保持原 200 ms sample wakeup 调度：callback 只置位，采样、SOC、MOS 与诊断在主循环。保护性计时使用 32K 墙钟，不按样本数量推算。

依据 SH36735XX V1.0A 原 PDF 第 10 页；未执行本轮编译或测试。
AFE 失联不证明芯片已睡或 Gate 已关。需实测全部产品的静态有效 PAD、PAD 竞争、
死 SPI watchdog、实际功耗、OTA 中断恢复和唤醒保护恢复。D013 图纸/BOM、D014 板级
限制仍见 [HARDWARE_VALIDATION](HARDWARE_VALIDATION.md)。
