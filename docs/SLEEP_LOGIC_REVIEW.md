# 四产品休眠入口、收尾、断电与唤醒

2026-10-14。基于当前目录 `codex-remove-openwire` 的修改前提交 `24c3a8fb`，并包含本次 D008 ACC 强制深睡修改。用户已确认继续在此分支开发。本文是源码审查和策略说明；未执行测试、编译、资源检查、生成 BIN、烧录、OTA 或实板测量。

## 1. 先区分三种动作

| 动作 | MCU / AFE 状态 | 退出方式 |
|---|---|---|
| 普通 Suspend | MCU 在 BLE/采样间隙短睡；AFE 保持正常运行，不执行 AFE Sleep/Shutdown | 下一次 200 ms 采样期限、BLE 调度；SH 另有 PAD/UART 唤醒 |
| MCU DeepSleep | 关闭正常业务和采样定时唤醒，调用 `cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0u)` | 配置的 PAD 唤醒后完整启动；SDK 临睡竞争返回则只保持深睡，不恢复业务 |
| D008 PC4 断电 | 完成收尾后将 `PC4/MCU_LDO` 拉低，申请切断 MCU 供电；不再调用 DeepSleep 或定时 Suspend | 外部硬件重新提供 MCU 电源后启动 |

PC4 控制的是 MCU 电源锁存。AFE 与电池、电源检测网络仍有自己的供电和状态，不能把 PC4 低等同于整板所有器件断电或零耗电。SH 的 `PD4/CMNT_EN` 只关闭通信供电，不是 PC4 整机断电。

当前 `app_config.h` 的 `PM_DEEPSLEEP_RETENTION_ENABLE=0`。普通 Suspend 不会因闲置时间足够长自动升级成上述强制 DeepSleep；健康电压下单纯零电流、无通信也不触发新的深睡入口。

## 2. 入口、时间与最终动作

四产品的 `sources.txt` 都选择 `bms/app/app_power.c`；D008 选择 DVC1124 后端，D011/D013/D014 选择 SH3673510 后端。

| 触发条件 | 请求/确认时间 | D008 最终动作 | D011/D013/D014 最终动作 |
|---|---|---|---|
| 开关 OFF：PA0 高 | 本次主循环检测到即提交；无额外 200 ms、3 s 或 5 s 确认延时 | AFE Shutdown 尝试；PC4 保持高；MCU DeepSleep | AFE Sleep 尝试；CMNT_EN 低；MCU DeepSleep |
| Modbus `0x1102=0x000A` | 锁存请求；UART/BLE 待发应答为空立即提交，否则最多等 500 ms | AFE Shutdown 尝试；PC4 拉低 | AFE Sleep 尝试；CMNT_EN 低；MCU DeepSleep |
| 最低有效单体低于下档电压 | 连续下档延时；编译默认 `<2800 mV / 60 分钟`，充电也不取消 | 同命令断电 | 同命令深睡 |
| 最低有效单体低于上档电压，且电流 `>=-200 mA` | 连续上档延时；编译默认 `<3000 mV / 1440 分钟` | 同命令断电 | 同命令深睡 |
| AFE1 错误或无新鲜有效测量 | 固定 30 分钟 | 同命令断电 | 同命令深睡 |
| 三级单体欠压或总压欠压持续未恢复 | 固定 180 秒 | 同命令断电 | 同命令深睡 |

开关入口需要 `BMS_PRODUCT_SWITCH_SLEEP_ENABLE=1`，四产品默认均为 1。D008 使用 ACC/PA0，独立于其旧的 `BMS_PRODUCT_SWITCH_ENABLE=0`；SH 还要求 `BMS_PRODUCT_SWITCH_ENABLE=1`，其 `FAC_TEST` 构建会禁用开关能力。宏关闭、引脚没有实际呈现 OFF、或请求前已回到 ON，都不能称为开关 OFF 必然深睡。

两档电压和延时以设备已保存的 `bms_sleep_config` 为准，编译默认不能证明现有设备使用的参数。比较是严格小于；电压等于门限不满足该档。已有最低电压筛选不包含无效串位 61001。四项保护计时独立，执行最先到期项；同时到期时最后选择的过放原因优先显示。

PM的新鲜测量要求有效辅助快照且样本年龄不超过400ms。无效/旧样本不能证明电压恢复，已确认的低压和过放继续计时。新鲜有效电压达到下档才清下档计时；达到上档或可靠充电 `<-200mA` 才清上档资格，下档不受该充电条件取消。过放计时需要欠压位都清除、AFE无错、测量有效且单体/总压均达到各自恢复阈值才解除；AFE异常需要测量有效且AFE1清除才重置。全部计时为本次启动的RAM状态，按32K差值消费完整秒并保留余量，处理回绕，reset/断电后不跨启动累计。

“立即”表示主循环本次观察到条件后提交，不是 GPIO 中断里关机，也不是已测得 OFF 到物理睡眠的固定毫秒数。500 ms 仅限命令的应答等待，仍需加上当前任务调度和前置 AFE/Flash 事务时间。关机命令已锁存时，D008 先处理命令，之后执行 PC4 断电，即使 ACC 同时为 OFF；SH 的 OFF 则优先于命令应答。保护到期在两产品分支中都优先。

## 3. 为什么修改前 D008 可能慢几秒或一直不睡

修改前 `app_enter_acc_sleep()` 有以下门禁：

- 必须非 OTA、Flash 已恢复锁定，且总线为 `BUS_STATE_OWC_IDLE`。
- BLE 连接时先等 Tx FIFO 空，再发断开请求，再等连接状态退出。
- State/Event 保存、停广告、AFE Shutdown 均必须成功；失败后 5 秒再试。

`bus_mux.c` 的 UART 模式在最后一次 RX 后超过 5 秒且收发空闲，才回退 `OWC_IDLE`。所以刚用完串口的 ACC 休眠可能晚约 5 秒；持续 UART 请求会反复延长，持续一线通 `OWC_TX` 也不会满足这个门禁。保存或 AFE 等持续失败会导致无限重试。这些是当前修改前源码的可解释路径，不能仅凭它们断定现场延时由哪一项造成。

本次移除 ACC 的上述持续等待和失败退避。ACC 检测到 OFF 即锁存，共用 `app_prepare_deep_sleep()` 完成一次收尾；PC4 保电和 PA0 唤醒保持。命令及保护性入口也调用这个公共收尾动作，原有应答等待和最终断电/深睡类型保持。

## 4. 哪些状态能挡住普通 Suspend，哪些不能挡住深睡

| 条件 | 普通 Suspend | 开关深睡 / 命令关机 / 到期保护性动作 |
|---|---|---|
| D008 一线通接入或 UART 模式 | 总线不是 `OWC_IDLE` 时阻止 | 不能阻止 |
| SH UART 收发、RX 低、坏帧、DMA/残帧待处理 | 阻止；上电/最近活动后需连续 30 秒静默 | 不能阻止；深睡时 UART RX PAD 禁用 |
| BLE 已连接 | 本身不禁止普通 BLE Suspend，仍服从 BLE 时隙和其他资格 | 只尝试断连，不等完成；命令原有应答最多 500 ms |
| OTA 或 Flash 尚未恢复锁定 | 阻止 | 不能阻止；此时跳过新增 State/Event 写入 |
| 无效/过期样本、待采样、电流绝对值 `>=200 mA` | 阻止 | 不作为开关/命令/已到期动作的否决条件 |
| UI 按键活动（当前 UI 宏为 0） | SH 对应 UI 启用时阻止 | 不能阻止 |
| State/Event、AFE、BLE 控制失败 | 普通 Suspend 本来不做强制收尾 | 各接口有限调用后继续最终动作，无失败重试循环 |

SH 普通 Suspend 的最终 SDK 回调还会复核 UART 活动与 RX 电平，避免许可后刚收到数据仍睡。D008 普通 Suspend 不配置 ACC/PB1 PAD 唤醒，依赖采样和 BLE 调度。30 秒串口静默、5 秒 D008 UART 回退都属于普通运行策略，不能再成为深睡等待时间。

## 5. 提交后的收尾与失败语义

`main_loop()` 的正常顺序是：已提交保持检查 → BLE SDK → 到期采样/SOC → 每秒事件 → D008 总线/SIF → UART Modbus → State checkpoint → PM 决策。OFF 因此仍可能先完成当前轮已开始的任务；启动时也要先完成现有初始化，才进入正常主循环检查。这里没有新增在空闲或启动早期进入深睡的入口。

提交时先设置对应的已提交标记，再执行：

1. 撤销输出授权，停 feature；通过 guard 尽力关闭均衡/MOS，并尝试 DVC Shutdown 或 SH Sleep。
2. 非 OTA 且 Flash 可用时，调用一次 `bms_state_store_write_all()` 保存当前 SOC、放电小数累计和循环数，并独立调用一次 `bms_event_log_note_sleep()`；State 失败不跳过 Event。底层 journal 原有去重、初始化、回读、失败预算和退避保留，接口调用一次不等于必然产生一次物理 Flash 写入。
3. 尽力终止 BLE 连接、停广告；禁止 SDK 普通 Suspend 和应用定时采样唤醒。
4. 按入口保留 PC4 并调用 DeepSleep，或最后拉低 PC4；SH 关闭 CMNT_EN 后调用 DeepSleep。

不为了保存成功一直等待，也不在 OTA/Flash 会话内强行追加业务写入。保存失败或被门禁拒绝时，最新 SOC/Event 可能未落盘，原已提交 journal 记录仍按原规则恢复；OTA 也可能被开关/保护性动作中断。这是保证强制动作完成所采用的失败策略。

后续 `app_power_prepare_loop()` 只运行保持动作，不再采样、通信、诊断轮询或重复写 State/Event、访问 AFE。D008 ACC 遇到 PA0 已为低或临睡竞争后变低，调用 `start_reboot()` 走完整初始化，不凭旧缓存恢复 AFE。PC4 拉低后的保持函数直接返回，软件没有检测 MCU 是否真的掉电，也没有 DeepSleep 兜底。

AFE guard 的 watchdog bus silence 不绕过。处于静默期或 AFE 命令失败时，MCU 仍执行最终动作，但不能由此推断 AFE 已睡或 Gate 已关。SH `INT_WK` 当前为高会使 AFE Sleep 请求失败，也不会挡住 MCU DeepSleep。

PM 没有新增无界重试。DVC I2C、SH SPI 保留其 timeout；SDK Flash 擦写仍是同步调用，B85 `flash_wait_done()` 使用最多 10000000 次轮询而非固定毫秒 timeout。它与主循环、watchdog、AFE 事务共同决定实际最长时间；未测量前不能承诺任何条件下 OFF 后若干毫秒内已物理深睡。

## 6. MCU 唤醒条件

| 入睡类型 | 当前实际配置的 MCU 唤醒 | 不具备的唤醒 |
|---|---|---|
| D008 ACC DeepSleep | PA0 低电平（开关 ON），走完整正常启动 | 无 200 ms 定时采样，无 PB1 负载 PAD，无 UART/一线通/BLE 业务唤醒 |
| D008 PC4 断电 | 外部电源网络恢复 MCU 供电；具体开关/充电复电条件须板测 | MCU 断电时不能靠内部 tick、BLE 或软件 GPIO 唤醒 |
| SH DeepSleep，入睡时 OFF | PA0 低电平 ON；PB1/INT_WK、PC0/ALARM、PC1/RESET 相对入睡电平的变化 | 不启用 UART RX PAD；PD3/CMNT_WK 未配置为该深睡入口的独立 PAD；无采样/BLE定时唤醒 |
| SH DeepSleep，入睡时 ON | 其他三个 PAD 变化仍可启动；开关先 OFF 再 ON 恢复工作 | 开关仅 OFF 不恢复 BLE/AFE/通信业务 |
| SH 普通 Suspend | 定时采样、BLE 调度、PC3 UART RX 低，以及开关/INT_WK/ALARM/RESET 的配置电平变化 | 不等同于停业务的 DeepSleep |

SH 深睡的三个外部 PAD 使用入睡时相反电平布防，以免稳定的有效电平导致立即拒睡。PAD 是电平唤醒，表中“变化”要求信号到达布防电平并满足实际硬件时序，不是软件保证捕获任意窄脉冲。

SH 在开关 ON 状态下入睡时，`DEEP_ANA_REG6` 保存标记与其他三个 PAD 电平。随后单独 OFF 会产生一次硬件启动，但 `main()` 在 watchdog、BLE、AFE 和业务初始化前重新深睡，改等 ON。其他 PAD 同时改变则保留正常启动。标记仅在 DeepSleep 保留，watchdog/reset/断电不沿用；正常启动清除。宏关闭只关闭开关触发，不擅自修改既有开关唤醒用途。

SH 普通 Suspend 的 UART 首个唤醒帧被整帧丢弃；RX 高且无新边沿超过 5 ms 后清理残帧。主站需发唤醒帧、留帧尾间隔并重发有效请求；可先以 20 ms 联调，最短间隔待板测。定时采样唤醒不会刷新串口 30 秒静默计时，任意 PAD 唤醒则保守刷新。

## 7. AFE 自身唤醒不等于 MCU 唤醒

本次回看了以下原 PDF 完整相关页，型号后缀/硅版本及板级连接仍沿用 [AFE资料指南](AFE_REFERENCE_GUIDE.md) 的未决边界：

- [DVC1124-2 DS V1.1](../references/afe/AFE_完整开发参考库_20261005/sources/DVC1124-2数据手册_V1.1.pdf) 物理第 12 页：Shutdown 的芯片唤醒包含 I2C 唤醒信号、PACK 高于 VTOP 2 V 以上、LOAD 高于 2 V；Shutdown 不是 DVC 的 Sleep，不能套用 Sleep 的定时/电流唤醒。本项目选择 Shutdown；MCU 完整启动通过 `dvc1124_boot.c` 的现有 I2C 脉冲/复位流程恢复。AFE 受充电或负载唤醒不能证明 PC4 供电或 PA0 MCU PAD 也恢复。
- [SH36735XX CV1.0A](../references/afe/AFE_完整开发参考库_20261005/sources/SH36735XX%20CV1.0A.pdf) 物理第 10 页 §7.1.3：`SCONF1=0xAA` 进入 Sleep，关闭 CADC/WDT、电压电流保护、FET/charge pump/均衡；可由 MCU 清 SCONF1、已使能的充电/负载检测退出。当前代码 `CGR_WK=1`、`WK_INT=1`，默认 `LD_WK=OFF`，因此没有启用额外负载连接/断开唤醒。AFE 的 ALARM 脉冲和 MCU PAD 仍需板级链路成立；不是通过 SPI/UART 数据直接唤醒 MCU。

本次只改变 PM 的拒睡策略，不改两后端的寄存器命令、保护参数、Flash/协议/OTA 布局、唤醒引脚和恢复资格。深睡期间 MCU 软件保护停止运行，不能把“软件已提交休眠”写成物理保护或零功耗证明。

## 8. 当前证据与待验证项

完成：实际 sources 成员、主循环/IRQ、状态所有者、提交/保持、SDK 配置、AFE 手册相关原页、异常路径与 Git 差异审查。回归源码同步覆盖 D008 ACC 宏 0/1、持续一线通/UART/BLE、OTA/Flash不可写、保存/AFE/BLE失败、单次收尾、PA0竞争与命令优先；SH 抽取入口加入公共收尾函数。所有这些用例本轮均未执行。

后续获得测试授权时运行相关 D008/SH PM host 场景及四产品 sources/link/resources；镜像需另有明确请求。实板须测 OFF→PC4/MCU睡眠时点、PA0/PAD竞争、持续通信及 AFE/Flash失败、watchdog、ON启动资格、PC4复电、充电器/负载唤醒和整板电流。正在运行的旧固件不会因本次源码修改自动获得新策略。
