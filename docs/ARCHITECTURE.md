# 架构、数据流与状态所有权

四产品直接使用同一份 `bms/core` 和 `bms/app`。完整组织见 [BMS_MONOREPO](BMS_MONOREPO.md)，逐步阅读见 [CODE_READING_GUIDE](CODE_READING_GUIDE.md)。

## 1. 模块与调用边界

```text
产品头文件 + sources.txt → 选择能力/默认值及唯一 AFE 后端
Telink main/IRQ → app 调度
app / Modbus / BLE → 公共参数、SOC、feature、诊断
app / feature → bms_afe.h → bms_afe_guard → bms_afe_driver.h
                                            ↓
                              DVC1124 或 SH3673510 后端 → I2C/SPI
Config / State / Event → storage_record → Telink Flash 平台
```

每个固件只链接一个后端，无运行时 factory/ops 表。业务调用公共 AFE API；guard、后端及受控诊断使用 driver 边界。GPIO、tick、UART/BLE、Flash 仍按 Telink SDK 实现。

## 2. 谁能修改什么

| 状态/数据 | 所有者与入口 | 失败/限制 |
|---|---|---|
| 软件保护参数 `g_tParam.protect` | `param.c` 的 LoadParam / bms_protection_params_commit；通信先构造候选 | 无效参数不授权输出；普通 SaveParam 不解除启动存储失败 |
| 持久 Config 和更新编号 | `bms_config_store.c` 的 get/set/default/update | 候选成功落盘后才替换 cache |
| AFE requested/effective、apply-state | `bms_afe_hw_profile.c`，独立授权/提交接口 | apply/readback 失败回滚；回滚失败 CONFIG_INCONSISTENT |
| 三等级软件故障 | `bms_sw_protection.c` 私有故障字；SOC Low 在 `bms_soc.c` | 仅在报告边界合并硬件故障，SW 阻断查询只读私有来源 |
| AFE 请求、通信抑制、样本资格 | `bms_afe_guard.c` | watchdog bus silence、重配、三次合格样本；失效不能以旧样本恢复 |
| 芯片 latch、物理恢复窗口、命令缓存 | DVC/SH backend | SC/OCD/OCC 依赖物理窗口/AFE 状态，零电流不等于已移除负载 |
| heater/balance/open-wire 策略 | `bms/app/bms_features.c` | 产品能力、温度、可信采样及故障互锁 |
| SOC estimate/display/OCV/学习 | `bms_soc.c`，样本入口推进 | 首帧/重复/无效/gap 不虚构时间；配置与 State 分域 |
| SOC/循环/学习/工厂时长持久值 | `bms_state_store.c`；Runtime 使用公共 State | 周期 checkpoint 与失败退避；不同组按独立编号重置 |
| 事件及运行日志 | `bms_event_log.c` / `bms_debug_log.c` | 前者 Flash checkpoint，后者仅 RAM；均有丢失窗口 |

## 3. 软件保护与 AFE 保护

软件用 First/Second/Third/Recover/Filter；First/Second 报告，Third 中相应电压/电流/温度位阻断方向。SOC Low 和压差故障不直接列入 MOS 关断掩码。软件恢复细节见 [SOFTWARE_PROTECTION](SOFTWARE_PROTECTION.md)。

AFE 用独立 `bms_afe_hw_profile_t`，不含软件三级概念；后端按 Rsense/芯片能力校验、量化、应用和读回。运行时修改软件参数不重写 AFE，反之亦然。DVC 的初始默认仍取编译期软件默认种子，SH 初始默认来自产品头；不能把运行时独立误写为所有默认来源完全独立。

AFE 事务：授权 → 校验完整候选 → 保存 → apply → requested/effective readback → verify；失败恢复前一 profile 并重新应用，回滚失败保持配置不一致。详见 [AFE_HARDWARE_PROTECTION_V2](AFE_HARDWARE_PROTECTION_V2.md)。

## 4. 输出与物理反馈

`mos_update()` 给出产品请求；最终输出还取决于参数/启动授权、guard 通信资格、feature、软件阻断、AFE 硬件锁存。`bms_afe_set_output_enabled(1)` 只表达允许申请，并非直接强制导通。

DVC common-port 单侧保护可映射为 AUTO_DIODE，共同故障 hard OFF；SH 保留芯片自己的锁存清除与恢复状态机。SPI/I2C 断线时通过同一总线发 OFF 只是 best effort，最终关断仍需实测硬件 watchdog/Gate。诊断的 requested、command cache、AFE status 不可合并为“已测 MOS 导通”。

## 5. 产品和单位

| 产品 | 输入 | 能力限制 |
|---|---|---|
| D008 | DVC1124；产品 `d008_product_profile.h` + `dvc1124_project_config.h` | 三种 profile；当前 SC 默认未启用 |
| D011 | SH；产品 `bms_sh3673510_config.h`；10S/250 µΩ | heater/balance；PB5 fuse 安全 LOW |
| D013 | SH；4S/100 µΩ | heater/balance/MOS NTC 禁用；原理图待核 |
| D014 | SH；8S/667 µΩ | 无 heater，TS3 NC；TS4 必需 MOS NTC |

公共电流 mA 正放负充，SH 在测量边界转换；温度 `(°C+40)*10`；名义容量 0.1 Ah、报告容量 0.01 Ah。无效串位 61001，不进入有效通道计算。字段定义与协议大小端以生产头文件和读写实现为准。

## 6. 通信与采样所有权

SH 只有完整采样读取及全串范围校验成功才发布报告；feature getter 只读同轮已验证温度，不产生 SPI。公共三新帧资格只由 guard 计数；backend 保留自己的硬件锁存、恢复窗口和命令证据。

UART RX IRQ 暂停 RX DMA 后交付单个缓冲区，主循环解析完才重新装载。此设计要求主站等待应答；不能保证接收任意背靠背请求。TX 全帧接受或拒绝，不截断、不覆盖活动 DMA。RS485 正常完成仍要求 DMA、UART 完成和最小线时长；超过 50 ms 在主循环中中止、重置 UART/DMA、释放 DE，再恢复 RX。

D008 SIF 由主循环显式编码到两个缓冲区，IRQ 只领取完成包和推进波形。正在发送的包不被更新；等候包可以随新样本刷新。协议按 TC32 `-fpack-struct` 固定，不能用 host 原生结构尺寸推断。

## 7. 修改边界

固定板级配置放产品目录，芯片寄存器/量化/恢复留后端，Flash/UART/BLE/中断留平台；不复制公共 `.c`。软件采样与 PM 时序变化要核对 watchdog、通信事务和主循环阻塞。SH 的 `SH3673510_FIXED_UART_BLOCKS_PM=1` 保留现有固定 UART 门禁，不因“有 sleep 函数”声称默认模式一定进入休眠。

持久化使用 [STORAGE](STORAGE.md) 与 [OTA_PARAMETERS](OTA_PARAMETERS.md) 的 schema/编号规则。根 CMake 只是可移植子集，SOC、参数、语义存储和 Modbus 仍有平台依赖，不声称已完成 STM32 移植。
