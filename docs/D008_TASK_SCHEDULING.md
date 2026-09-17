# D008 任务调度与维护入口

本次基于 `refactor/d008-common-bms-features` 的 `f73461d` 审核实际编译源表 `bms_tools/source_order.txt`。目标是保持业务顺序、保护参数、协议与 Flash 所有权，先使调度易读，不引入 RTOS、通用任务表、回调总线或新的 manager 层。范围是产品执行链路，不是逐行重写 Telink SDK。

## 1. 从哪里读起

以下文件均位于 `tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/`。

| 要理解的问题 | 首先阅读 | 再沿调用继续 |
|---|---|---|
| MCU 启动、watchdog、中断 | `main.c` | `app.c` 的 `user_init_normal/main_loop` |
| 产品引脚、装配、固定 AFE 配置 | `conf.h`、`d008_product_profile.h` | `dvc1124_project_config.h`、产品参考文档 |
| 何时采样、睡眠、处理通信 | `app.c` | 本文任务表 |
| AFE 通信资格、失败退避、输出授权 | `bms_afe_guard.c` | `dvc1124_bms.c`、`dvc1124.c` |
| 软件阈值和滤波 | `bms_sw_protection.c` | `g_tParam.protect`，见软件保护文档 |
| 电流故障锁存和解除 | `dvc1124_bms.c` | `docs/D008_CURRENT_RECOVERY.md` |
| 加热、开线、均衡状态 | `bms_features.c` | `dvc1124_feature_backend.c`、`dvc1124.c` |
| SOC 积分、静置、容量学习 | `SocEnhance.c` | `bms_soc_profile.h`、`bms_state_store.c` |
| BLE/串口协议 | `app_att.c`、`modbus_uart.c` | `modbus_rtu.c`、`bms_afe_hw_access.c` |
| 一线通与 UART 切换 | `bus_mux.c` | `sif_send.c`、`modbus_uart.c` |
| 参数/事件/状态持久化 | `param.c`、`bms_config_store.c`、`bms_state_store.c`、`bms_event_log.c` | `storage_record.c`、`bms_storage_platform_telink.c` |
| RAM 诊断与 Trace | `bms_diag.c` | 各 owner 的 `*_diag_poll`，只发布证据 |

`runtime.c` 只是工厂老化计时，不是整个系统的任务调度器；`soc_kv_store.h` 是 State 存储的历史兼容命名。不要在这些位置增加第二套调度或持久化 owner。

## 2. 启动与运行顺序

```text
main
  硬件/时钟初始化，尽早拉高 MCU_LDO 保持供电
  user_init_normal
    BLE SDK 初始化
    诊断/板级初始化
    参数升级门禁 -> LoadParam -> Event 初始化
    AFE 初始化 -> 启动采样 -> State/SOC 初始化
    SIF timer / bus mux / 名称 / 启动事件 / 工厂计时
    注册采样唤醒 -> MOS 请求 -> 输出授权 -> 冻结启动快照
  irq_enable
  while (1)
    watchdog feed（编译启用时）
    main_loop
```

正常轮次：

```text
已提交 ACC 休眠？ -> 仅保持深睡眠并返回
诊断 RAM 状态同步
已提交断电？ -> 保持静止并返回（调试器供电时也不恢复业务）
BLE SDK -> 工厂计时
app_sample_task：到期才执行一次完整采样链
app_event_log_1s_task：到期才采集一次事件状态
bus_mux_task -> UART Modbus
State 保存检查
blt_pm_proc：最后根据最新状态决定下一轮 suspend / 关机
```

诊断在已提交断电判断之前的既有顺序保留；ACC 已提交后不进入诊断。事件日志采用上一轮发布的 `low_power_mode`，不代表直接测得 MCU 物理休眠状态。本次没有移动这些可观察时点。

## 3. 任务表

| 入口 | 执行条件/节奏 | 状态归属与注意点 |
|---|---|---|
| `blt_sdk_main_loop` | 每个正常轮次 | SDK 处理无线事件/回调，优先于采样；通信写命令可能同步触发参数事务 |
| `Runtime_Poll` | 工厂模式每轮，32k 累计分钟 | `runtime.c`；完成老化后直接返回，重新进入工厂模式重建时间基准 |
| `app_sample_task` | `s_sample_due` 或系统时钟超过 200 ms | `app.c` 管截止时间，子模块各自拥有业务状态；超时合并为一次，不追赶执行多次 |
| `app_event_log_1s_task` | 系统时钟超过 1 s | 调用 `bms_event_log_poll_1s`；事件保存/失败退避仍由 Event owner 决定；不新增独立唤醒 |
| `bus_mux_task` | 每轮 | RX 高稳定 50 ms 进入 OWC_TX；监听到 18 ms 窗内至少 3 个下降沿进入 UART；UART 无活动 5 s 回监听 |
| `main_loop_modbus` | UART 功能编译启用时每轮 | 接收完成后解析、响应、重新接收；有既有错误恢复检查 |
| `bms_state_store_update_and_log_if_changed` | 每轮检查 | 正常保存间隔 60 s、失败退避 5 s；不是每轮写 Flash；运行时间写入与关机强制保存另有入口 |
| `blt_pm_proc` | 正常轮次末尾 | 显式断电指令 > ACC 休眠 > 自动低压断电 > suspend；不可合并这些不同电源路径 |
| `sif_timer_irq_handler` | Timer0，每 500 us（已初始化时） | 一线通位时序及组帧；当前即使非 OWC_TX 也进中断，随后快速返回 |
| `modbus_uart_irq_proc` | UART IRQ | 搬运/接收状态，解析在主循环 |
| `bus_mux_irq_handler` | RISC0 GPIO IRQ | 监听下降沿计数，不在这里解析协议 |
| `app_sample_wakeup` | SDK AppWakeup 回调 | 只设置 volatile 标志，不访问 AFE，不写 Flash |

系统无抢占式业务任务，任何同步 I2C、Flash、协议事务都会占用当轮时间。不能根据“200 ms”就宣称严格实时；长耗时和错误路径必须用板上波形测量。

## 4. 一次采样内部的所有权

```text
app_sample_task
  清 due，记录本次开始时刻
  bms_afe_sample [guard]
    shutdown hold / failsafe wait 检查
    DVC1124_BmsApp_AFEGet [backend]
      获取有效快照
      软件电压/电流与温度分组保护
      硬件告警恢复 + 电流恢复锁存
      合并硬件故障、发布诊断、记录保护边沿、DVC balance service
    检查有效采样连续资格
    bms_features_service：加热 -> Open-Wire -> Balance
    apply_requested：最终输出仲裁
  用新鲜测量驱动 SOC（无效测量明确传 invalid）
  mos_update：产品请求，最终控制仍经过 guard
  若执行已超过周期则标记下轮再采样
  更新 SDK 唤醒，保留当前蓝灯调试翻转
```

不能把 `mos_update` 与 guard 的 `apply_requested` 因名称相似就删掉其中一个：一个表达产品请求，另一个应用资格和故障约束。同样，DVC balance service 与公共 Balance 策略不是简单重复；前者包含芯片维护，后者选择目标。

Requested、AFE Command、AFE Driver Flag 分开；没有物理反馈时保持 unavailable。放电过流/短路保留充电及 AUTO_DIODE 续流，负载移除或可靠充电才允许恢复；这些状态不归调度层所有。

## 5. 两种时钟与唤醒

- `clock_time()` / `clock_time_exceed`：系统时钟 tick；微秒要乘 `SYSTEM_TIMER_TICK_1US`。SDK 判定是严格 `>`，本次不改为 `>=`。
- `pm_get_32k_tick()`：本项目按 32000 tick/s 计算；恢复、新鲜度、SOC 和存储计时使用它，不得套用 32768。
- 用无符号差处理单次回绕；并不支持跨多个完整回绕推算长时间。ACC deep sleep 后完整启动；不能用未知休眠时长补 SOC。
- AppWakeup 的 owner 是 `app.c`。当前产品没有使用 SDK `blt_soft_timer` 调度；以后启用时必须检查同一 AppWakeup 回调/截止时间被覆盖的问题。
- 当前基线 `conf.h` 的 `BMS_APP_SAMPLE_WAKEUP_ENABLE=0` 是功耗测试选择。本次保留。无电流锁存时采样依赖其他唤醒，200 ms 是到期检查而非保证每 200 ms 醒来；其他按样本数的滤波/开线等待也可能变长。
- 有电流故障锁存时，即使宏为 0 仍安排 200 ms 唤醒；否则约 800 ms 的广播间隔会破坏 400 ms 新鲜度和连续恢复资格。
- BLE 连接允许事件之间 `SUSPEND_CONN`，但 OTA、Flash 会话、总线忙、无效/过期样本、待采样、双向电流达到 500 mA 仍阻止 suspend；连接仍阻止自动低压断电。

## 6. 本次简化与功耗结论

已实现：

1. `main_loop` 只展示调度顺序；200 ms 完整采样链集中到 `app_sample_task`，1 s 事件截止时间放回事件任务内部。没有新源文件、新全局 owner、动态内存或函数指针任务表。
2. 工厂老化完成后 `Runtime_Poll` 不再读取 32k 时钟和执行无意义时间更新。Host test 验证正常模式连续 1000 次 poll 为零时钟读取，工厂重入、分钟保存、失败和回绕仍工作。
3. 调度回归测试覆盖执行顺序、严格截止时间、超时合并、采样中回调、无效测量、tick 回绕及已提交休眠/断电的业务隔离。

这减少 CPU 无效工作，**没有实测功耗降低数值，也不能声称重构已证明全部硬件行为等价**。

| 后续机会 | 源码证据 | 本次处理/实施边界 |
|---|---|---|
| 非 OWC_TX 停止 Timer0 | SIF 每 500 us 中断，非发送状态只重置软件状态返回 | 高价值候选；本次保留时序。需将停止/启动、计数器复位、pending IRQ 清除统一到总线状态切换，验证切回 UART/OWC、首帧和异常中途退出；不是简单删中断 |
| 关闭测试蓝灯 | 每次采样 `gpio_toggle(LED_BLUE_PIN)` | 保留当前调试行为；功耗测量必须记录 LED 状态，关闭需明确产品指示要求 |
| 降低正常采样频率 | 保护滤波及 Open-Wire 等仍有样本计数 | 本次不做；必须先明确检测延迟及 AFE watchdog 保活约束，不能只改周期宏 |
| 减少重复 AFE 访问 | 采样、驱动 readback、均衡维护各有入口 | 本次不做缓存删读；AFE 自主关断/重初始化会使旧缓存失效 |
| 增大 Flash 保存间隔 | State 已有 60 s / 失败 5 s 节流 | 本次不改，避免改变掉电丢失窗口及寿命策略 |
| 调整 BLE 参数/发射功率 | SDK 无线活动影响功耗 | 本次不改，避免影响连接、OTA、客户端兼容 |

## 7. 今后新增功能的放置规则

- 必须每轮快速处理的通信入口直接放 `main_loop`；不要再藏进 SOC 或诊断函数。
- 依赖采样的安全/测量功能放现有采样链 owner，明确有效性和顺序；不要独立再读 AFE。
- 有自身计时的任务把截止时间放在自己内部；只有需要从 suspend 唤醒的任务才参与唤醒安排。
- IRQ 只做必要工作；当前 SIF 有组帧工作是既有复杂点，后续迁移需确认共享缓冲的原子交接，不扩大 ISR。
- 不为少量相似 if 语句引入通用策略表。每个状态只由一个模块修改，协议读取/诊断不补写控制命令。

## 8. 验证与实板边界

自动化入口：`tests/app_scheduler_host_check.py`、`tests/app_recovery_wakeup_host_check.py`、`tests/d008_power_soc_host_check.py` 及现有保护/恢复/存储 contracts。CI 新增调度测试。

实板仍 `TODO_VERIFY_HW`：同配置前后测板端和 MCU 供电电流，覆盖广播/连接、串口接入及 5 s 回退、一线通完整首帧、OTA、充放电、电流锁存恢复、ACC 和指令断电。记录宏、LED、连接间隔、采样间隔以及 AFE I2C 波形，不能只比较 `low_power_mode`。

### 2026-09-18 软件验证结果

- 基线 `f73461d` 与修改后的生产调度代码使用相同 C fixture，均通过顺序与边界断言。
- Host 脚本共 21 组，其中 19 组通过；`tests.test_bms_tools` 另有 22 个测试全部通过。
- 两项既有失败已在未修改基线副本复现：`d008_framework_contract_check` 仍断言 24S，但当前用户提交实际为 16S（profile 名称仍为 24S）；`afe_hw_profile_contract_check` 的 `cuv_delay_ms == 8000` 默认值断言失败。本次没有修改 profile、阈值或这些断言来掩盖失败。因此不能把完整 Host 套件标成全绿。
- 当前 16S / AppWakeup=0、AppWakeup=1、SW/HW=1/0、0/1、0/0 共五个配置均完成 source-order、TC32 clean rebuild、check-fw、MAP、manifest、verify；温度保护配置保持当前开启。
- 当前配置 cppcheck：实际源文件 94 个，分析 31 个应用 C 翻译单元，覆盖缺口 0；95 个 style 提示，无 error/warning，MISRA 未执行。
- 同工具链同配置比较：text 102904 → 102920 字节（+16），data 4016、bss 7160 字节均不变；BIN 107092 字节。没有为了压缩字节数增加难读代码。
- MAP 静态 RAM 边界检查通过，未测动态栈高水位。manifest 继续提示既有 TLSR8251 / MCU_STARTUP_8258 SRAM profile 差异，本次未改启动文件或内存边界；不得据此宣称实板栈余量已验证。
- 未烧录，未进行板端电流、首帧、BLE 稳定性或保护动作实测。

### 交付

实现提交：`496d196`，仍在原分支。

- [16S 当前测试配置 BIN](../outputs/d008-task-scheduling-20260918/D008_16S_TASK_SCHEDULING_496d196.bin)：AppWakeup=0，保留当前保护默认值与蓝灯行为。
- [BUILD_INFO.json](../outputs/d008-task-scheduling-20260918/BUILD_INFO.json)：完整提交号、SHA-256、编译开关、硬件验证边界。
- [验证证据 ZIP](../outputs/d008-task-scheduling-20260918/validation-evidence.zip)：Host/基线失败/编译矩阵日志、MAP、原始 manifest 与外部构建脚本；原始 manifest 路径指向临时构建位置。

交付 BIN 注入诊断 build ID `0x496d196` 后再次 clean rebuild/check-fw/MAP/manifest/verify，text=102936，data=4016，bss=7160，BIN=107108 字节。上面的 +16 字节对比使用相同编译定义；注入 build ID 另增加 16 字节。SHA-256：`eab88577484b110fdc56af22c9422971db865602f8e340cc80e6c51b0fce27c2`。没有自动烧录或更改当前参数。
