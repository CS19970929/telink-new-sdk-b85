# SH 低功耗与异常处理

适用 D011 / D013 / D014，共用 `app_power.c` 和 `modbus_uart.c`；D008 不采用本页串口策略。
2026-10-10 按用户要求移除固定 UART 禁睡门禁，开关、低压与命令统一提交深睡。
2026-10-20 普通Suspend改为任务间可睡，UART静默窗口缩为1秒，充放电不再单独阻止；本次仅源码修改，未编译/测试/实板验收，见[实现记录](MCU_SUSPEND_POWER_RESEARCH_20261020.md)。下方历史固定提交成绩不覆盖本次修改。

## 深度休眠

| 入口 | 确认时间 | 提交条件 |
|---|---|---|
| 开关 OFF（PA0 高） | 宏启用时，主循环检测到即提交，无额外等待 | 不再要求 INT_WK 为低，不受 UART、BLE、OTA、Flash 状态阻止 |
| 最低单体低于 2800 mV | 连续 1 小时 | 保留既有低压计时和有效恢复判据 |
| 最低单体低于 3000 mV，且 `current_ma >= -200` | 连续 24 小时 | 无可靠充电；与一小时条件独立累计 |
| AFE 异常／无新鲜有效采样 | 连续 30 分钟 | 与低压独立计时 |
| 三级单体/总压欠压未恢复 | 固定 180 秒 | 独立计时，恢复资格见休眠配置 |
| `0x1102=0x000A` 命令 | 应答空闲后立即；最长等待 500 ms | 已锁存命令不能被通信卡死无限延后 |

表中2800mV/1小时、3000mV/24小时是编译默认，设备使用已保存的两档配置，见 [休眠配置](SLEEP_CONFIGURATION.md)。低压完整定义见 [保护性休眠](LOW_POWER_POLICY.md)。计时使用 32K 墙钟，处理无符号回绕。
开关由 `app_sh_explicit_sleep_poll()` 持有，四产品命令共用 `app_command_sleep_poll()`，电压与 AFE 条件由
`app_protective_sleep_poll()` 持有，最终调用同一个 `app_enter_protective_sleep()`。

提交后撤销输出授权，经过 guard 尝试 AFE Sleep；允许写 Flash 且未 OTA 时，State/Event
各尝试保存一次。随后停广告、停应用定时唤醒；强制 DeepSleep 还会停止 UART/DMA/RISC0，
保持 PA1 为 RS485 接收/禁发低电平，将 PC2/TX、PC3/RX、PD3/CMNT_WK 设为无上下拉高阻，
再关闭 CMNT_EN 并执行 MCU deep sleep。这避免通信隔离器掉电后，UART 运行态上下拉通过
IO 反向取电。普通 Suspend 不执行这组 GPIO 释放，仍保留 UART 唤醒能力。
AFE、存储、BLE 控制失败不撤销请求；watchdog 总线静默期间不能绕过 guard 访问 SPI。
OTA 可被中断，未保存的状态可能丢失，这属于强制深睡的明确取舍。

UART RX 的 PAD 唤醒与 RISC0 中断在强制深睡入口关闭，持续串口不能取消深睡。
INT_WK 的 PAD 唤醒继续等待输入变化。ALARM、RESET 不参与任何运行时判断或唤醒。
开关按“OFF保持休眠，ON恢复工作”处理：

- 入睡时开关 OFF：直接设 PA0 低电平（ON）唤醒。
- 命令或低压入睡时开关 ON：不能直接设低电平，否则立即拒睡。先设高电平，
  用 B85 `DEEP_ANA_REG6`（0x35）保存过滤标记与 INT_WK 的入睡电平（bit0）。
- 后续 OFF 会使 MCU 短暂硬件启动。`main()` 在 GPIO/clock 初始化后、watchdog 启动及
  `user_init_normal()` 前调用 `app_power_boot_sleep_hold()`。若只有开关变为 OFF，
  保持通信供电关闭，不初始化 BLE/AFE/业务、不保存 Flash，改为低电平唤醒并重新深睡。
  再次 ON 才放行正常启动；INT_WK 同时改变仍保留正常唤醒。
- SDK 若因竞争返回，启动保持入口重新检查输入；OFF继续尝试深睡，ON或其他输入变化
  才放行。这里保证不恢复业务，并非宣称 OFF 完全不产生 MCU 硬件启动电流脉冲。

此标记仅由 PM 使用，不占用 Flash、不改协议。SDK `pm.h` 规定 REG6 在深睡保留，
watchdog、芯片复位、RESET和断电清零；不是旧的 `USED_DEEP_ANA_REG` 或 SDK专用 REG2。
正常启动清除标记；无有效标记或非 PAD 启动不套用该过滤。
若 SDK 因 PAD 竞争返回，仅重设 PAD 并重试深睡，不返回业务，也不重复保存或发送 AFE 命令。
真正深睡唤醒走完整启动、AFE 重配及保护资格恢复；不能凭旧缓存恢复 MOS。

## 未使用的 AFE 引脚与 INT_WK

2026-10-19 按用户确认，D011/D013/D014 的 PC0/`BMS_BOARD_AFE_ALARM_PIN`、
PC1/`BMS_BOARD_AFE_RESET_OUT_PIN` 当前不使用。`main()` 在 `gpio_init()` 之后、
启动休眠保持之前只配置一次：GPIO、高阻、输出关闭、数字输入关闭、无内部上下拉、
PAD 唤醒禁用。运行时不再读取、重新配置、布防或保存其电平；AFE 重初始化也不再操作它们。
依据 [SH36735XX CV1.0A 原 PDF](../references/afe/AFE_完整开发参考库_20261005/sources/SH36735XX%20CV1.0A.pdf)
物理第 4 页表 1，ALARM（29 脚）和 RESET（34 脚）均为 AFE 开漏输出，MCU 不得驱动。
固定电气配置不表示网络必须为固定高/低电平；外部电压仍由实际板级网络决定。
这里取消的是 MCU 对两个引脚的用途；AFE 内部保护、ALARM 寄存器配置与 SPI 故障/复位标志恢复保持原流程。

PB1/`BMS_BOARD_INT_WK_MCU_PIN` 是板级输入，当前软件用途如下：

| 场景 | 当前逻辑 |
|---|---|
| AFE 初始化 | 配置 MCU 输入、关闭输出，初始启用高电平 PAD 唤醒；入睡前会重新布防 |
| 普通 Suspend / MCU DeepSleep | 采样当前电平，低则等高、高则等低；稳定高电平本身不阻止 MCU 睡眠 |
| 开关 ON 入睡后再拨 OFF | 用保留的 PB1 电平过滤单独 OFF；PB1 同时改变则允许完整启动 |
| 尝试 AFE Sleep | `sh3673510_control_sleep()` 发现 PB1 为高即返回失败，不发送 Sleep；PB1 低且控制层就绪才继续芯片休眠流程 |

最后一项不阻止已提交的 MCU DeepSleep：应用不因 AFE Sleep 失败撤销深睡。
PB1 高时可能出现 MCU 已睡而 AFE 仍运行，整板电流需据此区分。PB1 未用于采样有效性、
AFE 通信故障确认、MOS 保护恢复判据，也没有独立 ISR 或软件去抖；实际入睡前的相反电平布防由 PM 持有。
软件不能仅凭网络名证明 PB1 的充电/负载来源，具体电路、电平与脉冲持续时间仍须各板验证。
AFE 的充电器唤醒及 ALARM 输出见原 PDF 物理第 10 页 §7.1.3；取消 ALARM PAD 后，
AFE 自行唤醒不再通过 PC0 唤醒 MCU，MCU 须由开关或 PB1 的有效布防电平恢复。

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
默认启用并取消原200ms确认。2026-10-14起D008 ACC也直接锁存深睡，通信、OTA/Flash及保存/AFE/BLE失败不拒睡；PC4保持高，命令仍切断MCU供电。完整对照见 [休眠梳理](SLEEP_LOGIC_REVIEW.md)。

“立即”指主循环本次检测到OFF即请求/提交，不含人为3秒等待，也不表示GPIO中断内睡眠；
已有当前任务与必要AFE/保存事务仍有执行时间。SH开关提交后不受业务门禁取消。

## 普通 Suspend 与串口

以下常供电/RX唤醒条目适用于D011/D013，以及D014主动线路诊断构建。D014正常构建现使用独立PD3高电平唤醒及485按需供电：启动保持PD4低，只有通信活动请求上电；20ms工程等待后恢复UART/DMA，并经5ms帧尾静默解除残帧隔离。连续通信保持供电，静默1秒且收发均空闲才关闭；纯200ms采样/BLE唤醒不启动电源。普通断电仍保留PD3输入/PAD/RISC1，强制DeepSleep则关闭这些唤醒并锁存不再上电。当前仅源码实现，未编译/测试/实板验收，详见[实现和边界](MCU_SUSPEND_POWER_RESEARCH_20261020.md#9-d014独立pd3唤醒与485按需供电2026-10-20)。

- 普通运行保持 AFE 正常采样、CMNT_EN 通信供电以及 RS485 接收方向。
- 上电及每次 UART 收发之后，连续 1 秒无串口活动才允许 Suspend。坏帧、零长度帧、
  RX 边沿、RX 持续低、UART TX 与 RS485 DE 保持阶段均延长活动窗口。
- 保留普通 Suspend 的样本有效、采样未到期、UI 空闲、非 OTA、Flash 已恢复锁定条件。
  开关 ON 和充放电本身不阻止普通 Suspend，仍每200ms采样、保护并按实际时间积分SOC。
- SDK 最终入睡回调再次检查 UART 活动、DMA 待处理状态和 RX 电平，防止主循环许可后
  收到数据仍入睡。PC3 RX 使用低电平 PAD 唤醒；运行时 RISC0 下降沿只记录活动。
  SH 的 RISC0 归 UART，D008 原总线检测所有权不变。
- Suspend 退出回调只交付状态，SDK返回后主循环先于AFE/存储任务恢复UART/DMA。
  SDK取消入睡时也关闭本次PAD并清临睡标志，防止之后持续丢帧。PAD唤醒或退出附近的RX活动
  进入丢弃阶段，整个唤醒帧不交给 Modbus，避免残帧误执行写命令。
- 等 RX 高电平且无新边沿超过 5 ms 后清残帧并重新接收。发送端先发唤醒帧，再留出
  帧尾间隔并重发有效请求；联调可先用 20 ms 间隔，最终最小间隔须实板确认。
- 200 ms 应用采样以及 BLE 定时唤醒继续工作；单纯定时唤醒不重置串口 1 秒静默计时。
  PAD 来源不能区分管脚，其他 PAD 唤醒也保守保持运行 1 秒。最终入睡再次检查采样标志和系统tick期限。

SDK 依据：B85 `gpio.h` 的 RISC0/输入极性接口、`pm.h` 的 PAD 状态和唤醒定义，
`vendor/ble_module/app.c` 的 before-suspend 回调返回约定。本机 `liblt_825x.a` 中
`blt_brx_sleep` 将 `cpu_sleep_wakeup` 返回状态的低字节作为 `SUSPEND_EXIT` 的 `p[0]`；
`pm_get_wakeup_src()` 来自启动缓存，不能用于普通 Suspend 本次来源判断。
AFE 依据 SH36735XX CV1.0A 原 PDF 第 10 页，见 [AFE资料指南](AFE_REFERENCE_GUIDE.md)。

## 验证边界

2026-10-15 针对 D014 直接强制休眠约 300 uA、命令休眠后再拨 OFF 约 50 uA 的现象，
源码将两条路径统一到同一组通信域关断动作，并同步了 host 场景断言。已完成源码、
SDK 接口/库调用、异常路径与差异审查；
未执行编译、host 回归、资源检查或生成 BIN。后续按授权执行 `sh_power_host_check.py`、
`uart_ownership_host_check.py`、`sh3673510_comm_mode_check.py` 及公共改动四产品检查。

2026-10-19 取消 ALARM/RESET 运行时用途后的验证（工作树基于 `08656c09`，dirty=1）：

- D011/D013/D014 各执行 `sh_power_host_check.py` 通过。新增真实启动配置代码执行，
  检查继承的输入/输出/上下拉/唤醒使能被关闭；ALARM/RESET 全部四种电平组合及翻转
  不释放 OFF 启动保持，运行时读取/布防这两个脚会令夹具失败；INT_WK 两种入睡电平、
  开关休眠宏 0/1、命令、失败重试、普通 Suspend 与 tick 回绕场景通过。
- D011 `sh3673510_d011_integration_check.py`、D014 `d014_configuration_contract_check.py`、
  D011/D014 `sh3673510_comm_mode_check.py`、D014 `uart_ownership_host_check.py` 通过。
  使用当前 `tests/validation_catalog.py` 的入口；产品文档中旧名 D013/D014 integration 脚本并不存在，未计作执行项。
- 四产品当前源码 `sources --check`、开发模式 `link --jobs 4`、`resources` 通过；
  四产品链接均 0 errors / 0 warnings，未生成 BIN，未烧录/OTA。
  D008 使用源码选择的 16S LFP；D014 为当时源码的 8S NMC。构建包含用户已有配置修改，
  本轮提交只包含引脚逻辑、相关测试和说明，不收录这些预先暂存的参数文件。
- 日志位于本机 `%LOCALAPPDATA%/CodexTemp/bms-sh-unused-pins-20261019/`；
  这是软件与链接证据，不是实板功耗、PAD 竞争或物理保护验收。

实板须覆盖三个产品：开关关闭立即请求、宏0/1、低压到期与持续串口/OTA并发；AFE失联与保存失败；
命令ON入睡→OFF不恢复通信/MOS→ON正常启动、其他PAD并发以及watchdog/reset标记清除；
广播/连接期间 UART 首帧唤醒、第二帧正常、1秒后再次 Suspend；9600/19200/115200
对应产品长帧、DE最终停止位、RX持续低、GPIO竞争及唤醒后的采样/MOS保护。
软件保证已提交请求不被业务条件取消，不代表 MCU 故障、输入持续抖动时仍有物理睡眠保证。
AFE 失联不证明芯片已睡或 Gate 已关，实际整板功耗和保护恢复仍须实测。
硬件未决项见 [HARDWARE_VALIDATION](HARDWARE_VALIDATION.md)。
