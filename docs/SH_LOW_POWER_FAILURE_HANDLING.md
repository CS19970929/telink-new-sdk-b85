# SH 低功耗与异常处理

适用 D011 / D013 / D014，共用 `app_power.c` 和 `modbus_uart.c`；D008 不采用本页串口策略。
2026-10-10 按用户要求移除固定 UART 禁睡门禁，开关、低压与命令统一提交深睡。

## 深度休眠

| 入口 | 确认时间 | 提交条件 |
|---|---|---|
| 开关 OFF（PA0 高） | 宏启用时，主循环检测到即提交，无额外等待 | 不再要求 INT_WK 为低，不受 UART、BLE、OTA、Flash 状态阻止 |
| 最低单体低于 2800 mV | 连续 1 小时 | 保留既有低压计时和有效恢复判据 |
| 最低单体低于 3000 mV，且 `current_ma >= -200` | 连续 24 小时 | 无可靠充电；与一小时条件独立累计 |
| AFE 异常／无新鲜有效采样 | 连续 30 分钟 | 与低压独立计时 |
| `0x1102=0x000A` 命令 | 应答空闲后立即；最长等待 500 ms | 已锁存命令不能被通信卡死无限延后 |

低压完整定义见 [保护性休眠](LOW_POWER_POLICY.md)。计时使用 32K 墙钟，处理无符号回绕。
开关和命令由 `app_sh_explicit_sleep_poll()` 持有，电压与 AFE 条件由
`app_protective_sleep_poll()` 持有，最终调用同一个 `app_enter_protective_sleep()`。

提交后撤销输出授权，经过 guard 尝试 AFE Sleep；允许写 Flash 且未 OTA 时，State/Event
各尝试保存一次。随后停广告、停应用定时唤醒、关闭 CMNT_EN 并执行 MCU deep sleep。
AFE、存储、BLE 控制失败不撤销请求；watchdog 总线静默期间不能绕过 guard 访问 SPI。
OTA 可被中断，未保存的状态可能丢失，这属于强制深睡的明确取舍。

UART RX 的 PAD 唤醒与 RISC0 中断在深睡保持入口关闭，持续串口不能取消深睡。
INT_WK、ALARM、RESET 的 PAD 唤醒继续等待输入变化。开关按“OFF保持休眠，ON恢复工作”处理：

- 入睡时开关 OFF：直接设 PA0 低电平（ON）唤醒。
- 命令或低压入睡时开关 ON：不能直接设低电平，否则立即拒睡。先设高电平，
  用 B85 `DEEP_ANA_REG6`（0x35）保存过滤标记与其他三个 PAD 的入睡电平。
- 后续 OFF 会使 MCU 短暂硬件启动。`main()` 在 GPIO/clock 初始化后、watchdog 启动及
  `user_init_normal()` 前调用 `app_power_boot_sleep_hold()`。若只有开关变为 OFF，
  保持通信供电关闭，不初始化 BLE/AFE/业务、不保存 Flash，改为低电平唤醒并重新深睡。
  再次 ON 才放行正常启动；其他 PAD 同时改变仍保留正常唤醒。
- SDK 若因竞争返回，启动保持入口重新检查输入；OFF继续尝试深睡，ON或其他输入变化
  才放行。这里保证不恢复业务，并非宣称 OFF 完全不产生 MCU 硬件启动电流脉冲。

此标记仅由 PM 使用，不占用 Flash、不改协议。SDK `pm.h` 规定 REG6 在深睡保留，
watchdog、芯片复位、RESET和断电清零；不是旧的 `USED_DEEP_ANA_REG` 或 SDK专用 REG2。
正常启动清除标记；无有效标记或非 PAD 启动不套用该过滤。
若 SDK 因 PAD 竞争返回，仅重设 PAD 并重试深睡，不返回业务，也不重复保存或发送 AFE 命令。
真正深睡唤醒走完整启动、AFE 重配及保护资格恢复；不能凭旧缓存恢复 MOS。

## 开关休眠宏

四项目各自 `bms/products/<product>/bms_product.h` 增加：

```c
#ifndef BMS_PRODUCT_SWITCH_SLEEP_ENABLE
#define BMS_PRODUCT_SWITCH_SLEEP_ENABLE 1
#endif
```

设为 `0` 只取消开关触发休眠，仍保留开关输入、原有唤醒、MOS及其他用途。
SH 还要求既有 `BMS_PRODUCT_SWITCH_ENABLE` 表示开关能力；不要用这个旧能力宏代替
新的休眠策略宏。新宏不受 `FAC_TEST` 自动改写，可按产品头文件或编译定义选择，
只允许0/1，不属于持久化参数或OTA参数组。D008的新宏控制已有ACC休眠入口；
默认启用并取消原200ms确认，但其通信、存储与AFE成功门禁保持，命令仍切断MCU供电。

“立即”指主循环本次检测到OFF即请求/提交，不含人为3秒等待，也不表示GPIO中断内睡眠；
已有当前任务与必要AFE/保存事务仍有执行时间。SH开关提交后不受业务门禁取消。

## 普通 Suspend 与串口

- 普通运行保持 AFE 正常采样、CMNT_EN 通信供电以及 RS485 接收方向。
- 上电及每次 UART 收发之后，连续 30 秒无串口活动才允许 Suspend。坏帧、零长度帧、
  RX 边沿、RX 持续低、UART TX 与 RS485 DE 保持阶段均延长活动窗口。
- 保留普通 Suspend 的样本有效／新鲜、采样未到期、有效电流绝对值小于 200 mA、
  UI 空闲、非 OTA、Flash 已恢复锁定条件。开关 ON 本身不阻止普通 Suspend。
- SDK 最终入睡回调再次检查 UART 活动、DMA 待处理状态和 RX 电平，防止主循环许可后
  收到数据仍入睡。PC3 RX 使用低电平 PAD 唤醒；运行时 RISC0 下降沿只记录活动。
  SH 的 RISC0 归 UART，D008 原总线检测所有权不变。
- Suspend 退出回调只交付状态，主循环恢复 UART/DMA。PAD 唤醒或退出附近的 RX 活动
  进入丢弃阶段，整个唤醒帧不交给 Modbus，避免残帧误执行写命令。
- 等 RX 高电平且无新边沿超过 5 ms 后清残帧并重新接收。发送端先发唤醒帧，再留出
  帧尾间隔并重发有效请求；联调可先用 20 ms 间隔，最终最小间隔须实板确认。
- 200 ms 应用采样以及 BLE 定时唤醒继续工作；单纯定时唤醒不重置串口 30 秒静默计时。
  PAD 来源不能区分管脚，其他 PAD 唤醒也保守保持运行 30 秒。

SDK 依据：B85 `gpio.h` 的 RISC0/输入极性接口、`pm.h` 的 PAD 状态和唤醒定义，
`vendor/ble_module/app.c` 的 before-suspend 回调返回约定。本机 `liblt_825x.a` 中
`blt_brx_sleep` 将 `cpu_sleep_wakeup` 返回状态的低字节作为 `SUSPEND_EXIT` 的 `p[0]`；
`pm_get_wakeup_src()` 来自启动缓存，不能用于普通 Suspend 本次来源判断。
AFE 依据 SH36735XX CV1.0A 原 PDF 第 10 页，见 [AFE资料指南](AFE_REFERENCE_GUIDE.md)。

## 验证边界

本次完成源码、SDK 接口/库调用、异常路径与差异审查；同步了现有 host 场景，
未执行编译、host 回归、资源检查或生成 BIN。后续按授权执行 `sh_power_host_check.py`、
`uart_ownership_host_check.py`、`sh3673510_comm_mode_check.py` 及公共改动四产品检查。

实板须覆盖三个产品：开关关闭立即请求、宏0/1、低压到期与持续串口/OTA并发；AFE失联与保存失败；
命令ON入睡→OFF不恢复通信/MOS→ON正常启动、其他PAD并发以及watchdog/reset标记清除；
广播/连接期间 UART 首帧唤醒、第二帧正常、30秒后再次 Suspend；9600/19200/115200
对应产品长帧、DE最终停止位、RX持续低、GPIO竞争及唤醒后的采样/MOS保护。
软件保证已提交请求不被业务条件取消，不代表 MCU 故障、输入持续抖动时仍有物理睡眠保证。
AFE 失联不证明芯片已睡或 Gate 已关，实际整板功耗和保护恢复仍须实测。
硬件未决项见 [HARDWARE_VALIDATION](HARDWARE_VALIDATION.md)。
