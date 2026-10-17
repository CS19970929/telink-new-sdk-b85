# 四产品采样、计时与低功耗主链重构

实施基线：`4adb2df563bd4084d948fcd72d8e74d177fd5de7`；分支
`codex-bms-deep-simplification-20261016`。本记录仅覆盖本轮修改，历史测试不作为本轮结果。

## 基线与约束

- 四产品 sources.txt 共用 app、Guard、SOC、PM 和时间平台；D008 选择 DVC1124，另外三个产品选择 SH3673510。
- 基线顺序：后端采样/软件保护 → Guard 三次新证据 → feature → MOS → SOC。
- 后端拥有测量活性、芯片配置、物理恢复和 read-clear 标志；Guard 拥有通信禁止、watchdog 静默、输出资格；feature 拥有均衡/加热确认；app 拥有调度；PM 拥有休眠提交。
- DVC 512 ms / SH 400 ms 测量活性，SH 1200 ms 温度启动，应用 400 ms 观察窗口分别保留。
- 三次资格、均衡可信度、加热启动和熔断不能把缓存重复读取算作新证据。
- 普通 Suspend 保留 200 ms 唤醒、200 mA 电流门禁、UART 30 s 静默；强制休眠保留最长 500 ms 应答收尾和已提交保持。
- Config/State/Event schema 4、factory_reset、AFE 配置、协议单位、61001、OTA/APP 边界不改。

## 对外时间兼容清单

`bms_afe_aux_measurements_t.sample_tick_32k`、参数采样时间窗口、运行/AFE/MOS/休眠
诊断、开发日志及 SOC 实板记录中的 32K 字段保留原单位。内部毫秒时间不得乘 32 冒充
原始绝对 tick；必要的原始时间仅供这些兼容输出使用。

## 实施记录

1. 本提交固定源码基线、状态职责及兼容约束；尚未改变生产源码。

## 验证边界

用户要求实施计划，但计划将运行验证保留为另行明确授权步骤。本轮默认只进行源码、
调用链和 Git 差异审查；测试用例随接口修改，未执行 host、sources --check、编译、
链接、资源检查、静态分析工具、BIN、烧录、OTA 或实板操作。

## 阶段二：采样契约与应用顺序

- 后端直接返回 OK / NOT_READY / ERROR 和一次性 confirm_sample；删除公共 sample_pending 查询及两后端 pending 状态。
- DVC 保留 voltage_since_current 仅形成恢复证据；SH 保留同时就绪事件用于原有恢复确认，普通测量分别发布。删除 SH temperature_started 副本，启动等待由采样起点直接判断。
- Guard 仅消费结果、隔离通信、累计三次证据。应用明确调用功能决策和最终 MOS 应用，再向 SOC 提交测量。
- 均衡电压合理性、温度、许可及加热停止条件每个有效轮次检查；可信计数、加热启动及熔断计数只消费 confirm_sample。
- 本地原资料依据：DVC1124-2 V1.2 PDF 第6–8页（RC ADC 标志、R6 FET 状态）；SH36735XX CV1.0A PDF 第19、21页（VADC/CADC 独立周期、FLAG2 读清）。保留已有启动等待和活性窗口，数值为项目策略。
- 更新采样、Guard、调度与功能夹具，并加入缓存不累计资格、缓存立即关闭均衡用例。仅完成源码和差异检查，未执行测试或编译。
