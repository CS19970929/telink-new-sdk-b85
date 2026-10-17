# 架构、数据流与状态所有权

四产品直接使用同一份 `bms/core` 和 `bms/app`。完整组织见 [BMS_MONOREPO](BMS_MONOREPO.md)，逐步阅读见 [CODE_READING_GUIDE](CODE_READING_GUIDE.md)。

## 1. 模块与调用边界

```text
产品头文件 + sources.txt → 选择能力/默认值及唯一 AFE 后端
Telink main/IRQ → app_ble SDK 初始化/回调 → app_init / app 调度
app / Modbus / BLE → 公共参数、SOC、feature、诊断
app / feature → bms_afe.h → bms_afe_guard → bms_afe_driver.h
                                            ↓
                              DVC1124 或 SH3673510 后端 → I2C/SPI
Config / State / Event → storage_record → Telink Flash 平台
```

每个固件只链接一个后端，直接实现统一的 `bms_afe_driver_*` 接口；Guard 不再识别芯片型号。固定 watchdog/初始化顺序/休眠语义由后端只读 policy 给出，无运行时 factory/ops 表。业务调用公共 AFE API；guard、后端及受控诊断使用 driver 边界。GPIO、UART/BLE、Flash 仍按 Telink SDK 实现；公共业务时间由 `bms_time_ms()` 累计原始 tick 增量及余数；`bms_time_now_32k()` 只保留诊断、日志及协议时间字段。

保护性休眠的低压/AFE/过放计时和选择集中在 `bms_sleep_config.c` 的
`bms_sleep_policy_step()`：输入为已验证快照、配置和业务秒数，输出为原因与剩余期限，
不访问外设或执行休眠。Application 唯一拥有已提交标志，并执行原 BLE/Flash/AFE 收尾及
板级 GPIO、Sleep/Wakeup。名称的 BLE 广播应用位于既有 `app_ble.c`。
PC CMake 使用实际 Core TU 验证这些接口，无 SDK include 路径。旧
AFE 板级 GPIO/I2C 和 Telink 普通 Suspend
仍是后续 MCU 移植需要适配的具体位置；不宣称整个源码树已经脱离 SDK。

State/Event checkpoint 与失败退避均使用毫秒时间；持久载荷和 schema 4 不变，见 [三域存储](STORAGE.md)。

SH 后端的 C+/LOAD 互斥、ADC 新样本资格和清 flag 失败处理见 [SH 恢复证据与采样资格](SH_RECOVERY_AND_FRESHNESS.md)。充电器查询与 feature 快照均只读已验证缓存，guard 继续拥有输出资格和 watchdog 静默。

## 2. 谁能修改什么

| 状态/数据 | 所有者与入口 | 失败/限制 |
|---|---|---|
| 软件保护参数 `g_bms_protection_params` | `bms_parameters.c` 的 bms_parameters_init / bms_protection_params_commit；通信先构造候选 | 无效参数不授权输出；普通 bms_protection_params_commit 不解除 Config 启动失败；State/Event 故障降级报错 |
| 持久 Config 和 OTA 执行标记 | `bms_config_store.c` 的 get/set/default/update | 参数和标记一起提交，候选成功落盘后才替换 cache |
| AFE requested/effective、apply-state | `bms_afe_hw_profile.c`，独立授权/提交接口 | apply/readback 失败回滚；回滚失败 CONFIG_INCONSISTENT |
| 三等级软件故障 | `bms_sw_protection.c` 私有故障字；SOC Low 在 `bms_soc.c` | 仅在报告边界合并硬件故障，SW 阻断查询只读私有来源 |
| AFE 请求、通信抑制、样本资格 | `bms_afe_guard.c` | watchdog bus silence、重配、三次合格样本；失效不能以旧样本恢复 |
| AFE 实际 FET 回读及有效性 | backend 经 `bms_afe_publish_fet_status()` 发布，`bms_afe_guard.c` 保存 | 命令变化、通信失效、输出撤销后立即作废；标准协议不依赖开发诊断缓存，也不把寄存器状态宣称为实测 Gate |
| 芯片 latch、物理恢复窗口、命令缓存 | DVC/SH backend | SC/OCD/OCC 依赖物理窗口/AFE 状态，零电流不等于已移除负载 |
| heater/balance 策略 | `bms/app/bms_features.c` | 产品头用 `BMS_FEATURE_*_SUPPORTED` 编译期裁剪；保留产品的温度、可信采样及故障互锁 |
| SOC estimate/display/OCV/循环 SOH | `bms_soc.c`，应用输入推进 | 首轮建立基准、重复应用时间不积分、无效/GAP 不补算；有效缓存可积分，新测量确认独立 |
| SOC/放电累计/循环持久值 | `bms_state_store.c`；只持有最后已提交值，最新快照由 SOC 提供 | 周期 checkpoint、失败退避与有界物理重试；SOC_STATE 独立执行标记 |
| 采样到期标志 / 休眠计时与提交状态 | `app.c` 持有采样标志；`app_power.c` 持有 PM 状态并只读该标志 | 回调只置位；已提交保护性深睡、ACC 休眠/关机先执行保持动作，不再进入业务调度 |
| 毫秒业务时间 | `bms_time_ms()` 的 Telink 平台实现，由主循环上下文消费 | 启动建基准，普通 Suspend 继续累计；原始 tick 消费间隔短于一次回绕，禁止 ISR 调用 |
| 事件及运行日志 | `bms_event_log.c` / `bms_debug_log.c` | 前者 Flash checkpoint，后者仅 RAM；均有丢失窗口 |

2026-10-11 的职责收口见 [全模块审查与简化](SIMPLIFICATION_20261011.md)：软件保护初始化由 guard
拥有，feature 不再初始化保护模块；加热周期由 `heater_state` 保持。2026-10-12 按用户决定，
均衡删除充电会话记忆，冷启动静置也可建立资格；当前真实放电仍停止均衡。
充电源查询仅用于启动有需求且受支持的加热周期，不参与均衡。
Config 独占完整 SOC profile 与四项休眠参数；化学体系/曲线/保护在一条 CFG4 中提交。
State/Event 独立保存，周期检查共用一秒任务。详见 [三域存储](STORAGE.md)。
PM 的原因/elapsed/delay 是本轮局部结果，跨轮只保存计时、最后确认条件和提交状态。

## 3. 软件保护与 AFE 保护

软件用 First/Second/Third/Recover/Filter；First/Second 报告，Third 中相应电压/电流/温度位阻断方向，三级压差同时阻断充放电。SOC Low 不直接列入 MOS 关断掩码。压差保护持续独立运行，软件恢复细节见 [SOFTWARE_PROTECTION](SOFTWARE_PROTECTION.md)。

AFE 用独立 `bms_afe_hw_profile_t`，不含软件三级概念；后端按 Rsense/芯片能力校验、量化、应用和读回。运行时修改软件参数不重写 AFE，反之亦然。D008 的软件及 DVC 初始默认分别来自产品 `d008_protection_config.h` 的 `D008_SW_*` / `D008_AFE_*`，构造时也相互独立；SH 保护初始默认来自各产品 `dXXX_protection_config.h`，板级能力仍在产品头，详见 [D008 保护配置](D008_PROTECTION_CONFIGURATION.md)。

AFE 事务：授权 → 校验完整候选 → 保存 → apply → requested/effective readback → verify；失败恢复前一 profile 并重新应用，回滚失败保持配置不一致。详见 [AFE_HARDWARE_PROTECTION_V2](AFE_HARDWARE_PROTECTION_V2.md)。

## 4. 输出与物理反馈

`main_loop()` 四产品共用任务顺序，GPIO 初始化和 D008 总线调度保留原时点；PM 状态机由 `bms/app/app_power.c` 持有。`app_init()` 一次性提交同口 CHG/DSG 请求；`bms_afe_sample()` 返回明确结果并完成 Guard 资格处理，应用在 OK 轮次调用 `bms_features_service(confirm_sample)`，再经 `bms_afe_apply_outputs()` 仲裁输出，最后更新 SOC，应用不再每帧重复提交固定请求。最终输出还取决于参数/启动授权、guard 通信资格、feature、软件阻断、AFE 硬件锁存。`bms_afe_set_output_enabled(1)` 只表达允许申请，并非直接强制导通。

DVC common-port 单侧保护可映射为 AUTO_DIODE，共同故障 hard OFF；SH 保留芯片自己的锁存清除与恢复状态机。SPI/I2C 断线时通过同一总线发 OFF 只是 best effort，最终关断仍需实测硬件 watchdog/Gate。guard 保存的 AFE status、诊断的 requested/command cache 都不可合并为“已测 MOS 导通”。

参数/启动资格由 guard 直接检查；feature 的双向断线阻断已删除，加热仍可阻断充电方向。功能间影响的保留依据及策略候选见 [功能耦合审查](FUNCTION_COUPLING_REVIEW.md)。

SOC 不再读取 feature 状态。加热/均衡活动不阻止 OCV 或静置空电锚定；实际电流、电压、温度、故障和时间资格仍独立检查。AFE 功能快照由 getter 返回值唯一表示有效性；成功时完整填写，失败时调用方不使用内容，见 [采样与 SOC 解耦](SAMPLING_SOC_DECOUPLING.md)。

## 5. 产品和单位

| 产品 | 输入 | 能力限制 |
|---|---|---|
| D008 | DVC1124；产品 `d008_product_profile.h` + `bms_product.h` | 三种 profile；SC 默认 200 A / 请求 256 µs，实板验收独立 |
| D011 | SH；产品 `bms_product.h`；10S/250 µΩ | heater/balance；PB5 fuse 安全 LOW |
| D013 | SH；默认 10S/100 µΩ，构建可覆盖串数 | heater/balance 禁用；TS4 必需 MOS 10K-3435（用户确认），原理图待核 |
| D014 | SH；8S/667 µΩ | 无 heater，TS3 NC；TS4 必需 MOS NTC |

公共电流 mA 正放负充，SH 在测量边界转换；温度 `(°C+40)*10`；名义容量 0.1 Ah、报告容量 0.01 Ah。无效串位 61001，不进入有效通道计算。字段定义与协议大小端以生产头文件和读写实现为准。

## 6. 通信与采样所有权

SH 只有完整采样读取及全串范围校验成功才发布报告；feature getter 只读已验证的有效温度缓存，不产生 SPI。公共三新帧资格只由 guard 计数；backend 保留自己的硬件锁存、恢复窗口和命令证据。

主动采样线诊断、诊断帧隔离和 SOC 暂停/恢复状态已删除，见 [删除说明](OPENWIRE_REMOVAL.md)。新 VADC 直接更新普通电压报告；ADC 活性、guard 三次新样本、通信静默及物理恢复资格继续独立维护。

UART RX IRQ 暂停 RX DMA 后交付单个缓冲区，主循环解析完才重新装载。此设计要求主站等待应答；不能保证接收任意背靠背请求。TX 全帧接受或拒绝，不截断、不覆盖活动 DMA。RS485 正常完成仍要求 DMA、UART 完成和最小线时长；超过 50 ms 在主循环中中止、重置 UART/DMA、释放 DE，再恢复 RX。

D008 SIF 由主循环显式编码到两个缓冲区，IRQ 只领取完成包和推进波形。正在发送的包不被更新；等候包可以随新样本刷新。线格式显式小端：公有 `0x01` 为20字节，实时 `0x3A` 为33字节，单体 `0x3B` 为 `4+2N` 字节，电池编码 `0x3C` 为 `4+字符数` 字节；私有 len 为总长减4，所有 SUM 为校验字节之前全部字节之和低8位，不能用 host 原生结构尺寸推断。2026-10-13 起修正原 `0x3B` 固定0xAA及公共 MOS 温度截断，不再保持这些历史错误。

一线通公开字段按 A1.3.3 表 A.2/A.5/A.6/A.7。本项目线版本选用0x13（主版本1、次版本3；文档末位修订号不另占字节），不再沿用历史0x01。电流保留0.1A、正放电负充电，线值为 `5000+放电A*10-充电A*10`，超出±500A或双向同时非零为0xFFFF；温度报告 `(℃+40)*10` 除10，无效报告0或超过150℃发0xFF。SOC、循环数、单体极值及位置按协议范围编码。主循环每次组包先查询现有 `bms_afe_get_aux_measurements()` RAM 快照与门禁；采样无效时 SOC/电压/电流/温度/单体极值及位置发送协议无效值，单体整帧无效。不修改保护报告、MOS 请求或采样恢复资格。

材料由编译化学体系映射（LFP=1、NMC=3），额定容量取 Config 的0.1Ah值。厂家代码、型号及独立额定电压没有可信配置来源，继续发0xFF/0xFFFF；不冒用星恒厂家码，也不用保护阈值代替额定电压。单体只发送编译有效串数，一线通边界将内部61001及其它超过60000mV的值转为0xFFFF；其它协议和内部空槽61001约定不变。

接入顺序为三帧公有帧、一帧单体、三帧编码；之后单体按60s采样tick门限在下一发送机会发出，编码紧随单体一帧，其间持续发送实时帧。已有 OW 上拉检测触发接入；当前 D008 未提供独立 DETD 输入，不能宣称实现独立 DETD 接入识别。编码与单体共用周期，tick无符号相减处理回绕；重接入重置首次发送与编码待发计数。500µs位节拍、结束低电平5ms及双缓冲所有权保持。

故障表使用现有 First/Second/Third 状态，保护优先于异常报警和预警，同级后匹配项覆盖先匹配项。Third 映射0x01/03..09，Second放电过流沿用0x02，Second充/放电高温为0x14/15，已有其它预警映射0x22..29；欠压/过压包含单体及总压。不新增保护阈值，不用MOS过温冒充0x0A/0x0B MOS损坏；当前没有这两个故障的独立检测来源。

有效放电/充电状态分别为0/1，请求应答高三位为0（不请求应答）。文档没有静置状态，本项目以0xFF表示静置/未知，上位机显示 `--`；这是明确的未提供状态标记，不是文档定义的静置码，不能宣称此值已获外部设备认可。无法从当前测量区分回馈与充电，不伪造回馈。用户明确保留加密、充电策略和互认为未实现；回馈限流、请求充电电压/电流、充电状态及需解码的BMS状态/加密结果填FF，key=0显式标记未实现（不属于合法加密报文的1..255）。不得把这些字段解释为许可或认证通过，也不实现未知应答格式。

`0x3C` 电池编码来自 `bms_config_get_user()` 读取的已提交 `Config.user.serial[32]`。SN 通过 `0x2E40=0` 开始暂存，`0x2E50..0x2E5F` 写入完整32字节，用 `0x2E40=generation` 提交；验证和持久化成功才发布缓存。发送不读暂存区、不新增Flash字段、不使用 `0xC002` 的共享编译默认SN。ASCII取首NUL之前字符，不发NUL，满32字节完整发送；空/全空格/非法SN或读取失败发 `len=1,payload=FF`。跨设备唯一性由配置来源保证，不做查重。

2026-10-13 用户确认原一线通链路实板通信正常。本轮 A1.3.3 完善按此前约定只修改源码及回归断言、检查调用链/边界/Git差异；没有执行host、目标编译、EXE打包、生成BIN、烧录或新增实板验证。接收端快照已升为version=2，必须配套更新Comm Tool与上位机，原dist EXE和已运行固件不代表本轮源码。

## 7. 修改边界

固定板级配置放产品目录，芯片寄存器/量化/恢复留后端，Flash/UART/BLE/中断留平台；不复制公共 `.c`。软件采样与 PM 时序变化要核对 watchdog、通信事务和主循环阻塞。SH 由 UART 所有者确认 30 秒静默后允许普通 Suspend，并用 RX PAD 唤醒；开关、命令及[低压/AFE 异常保护性深睡](LOW_POWER_POLICY.md) 提交后不受业务状态阻止，详见 [SH低功耗](SH_LOW_POWER_FAILURE_HANDLING.md)。

持久化使用 [STORAGE](STORAGE.md) 与 [OTA_PARAMETERS](OTA_PARAMETERS.md) 的 schema/镜像更新选择规则。根 CMake 只是可移植子集，SOC、参数、语义存储和 Modbus 仍有平台依赖，不声称已完成 STM32 移植。

2026-10-14：D008 ACC也使用锁存后一次收尾的强制深睡，移除通信/存储/AFE成功门禁与5秒退避，保持PC4高与PA0唤醒。公共收尾为 `app_prepare_deep_sleep()`；普通Suspend资格独立。完整入口、优先级、PC4与AFE/MCU唤醒边界见 [休眠梳理](SLEEP_LOGIC_REVIEW.md)，本次未执行测试或编译。

2026-10-17 的接口、无消费者字段、SOC 重复判断与保留状态裁决见 [第二阶段重构](REFACTOR_PHASE2_20261017.md)。
