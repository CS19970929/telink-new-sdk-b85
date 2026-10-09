# 2026-10-10 BMS 最小优化记录

起始源码 `467f710e`，分支 `codex-bms-monorepo`。本轮按源码确认问题后逐项提交，不生成 BIN、不操作实板、不推送。工作区并行存在参数配置/OTA 任务；其 `7de16775` 不属于下列优化。D008 产品签核头文件和 `docs/TODO.md` 的外部未提交内容未纳入本轮提交。

## 已落地的修改

| 优先级 | 触发与原后果 | 最小修改、源码与独立提交 | 软件验证 |
|---|---|---|---|
| P1 | 温度保护已触发后传感器无效，旧代码清除保护锁存，重获迟滞区读数可能失去原保护 | [bms_sw_protection.c](../bms/core/bms_sw_protection.c) 保留 active/fault，清确认计数；禁用或未装通道保持原语义。`a83c26d1` | 两类温度 hot → invalid → hysteresis → release，四产品 |
| P1 | 低压在 2800 mV 两侧波动，24 小时条件被分区计时清除 | [app_power.c](../bms/app/app_power.c) 分别累计 <2800 与 <3000 条件，后者保留可靠充电排除。`f7ed3759` | 48 次半小时跨区，ADC 等待、有效恢复、充电阈值 |
| P1 | 充电回升后旧 UV 锁存仍在，静置或放电触发强制 SOC=0 | [bms_soc.c](../bms/core/bms_soc.c) 强制空端点要求当前可信欠压证据。`96b1bc2f` | 旧 UV+充电/静置/放电保持容量；当前欠压仍锚零 |
| P1 | DVC 均衡读写失败，上层仍获成功，或将最后确认状态清成零 | [dvc1124.c](../bms/afe/dvc1124/dvc1124.c) 及 feature backend 贯通返回值；失败不伪造新确认值。`6c2fa4b3` | 实际采样/均衡函数读失败、写失败、回读失败注入 |
| P1 | State/Event 初始化失败与 Config 失败共用启动输出阻断 | [bms_parameters.c](../bms/core/bms_parameters.c) 仅安全配置决定启动许可；State/Event 报错并按既有路径降级。`e0806514` | 各域失败、Config 锁存、逐字节中断、重启重试 |
| P1 | watchdog 静默长度由 200 ms 调用次数决定 | [bms_afe_guard.c](../bms/core/bms_afe_guard.c) 改实际 tick，保留 DVC 5 s/SH 35 s。`473dccc5`；DVC 时钟夹具 `011d0963` | 密集调用不推进时间、到期边界、回绕、三新鲜样本资格 |
| P2 | SH 均衡续期依赖调用次数，抖动改变实际续期长度 | [sh3673510_feature_backend.c](../bms/afe/sh3673510/sh3673510_feature_backend.c) 20 s tick 续期，保留状态不一致立即重写。`becaa46e` | 到期、回绕、失败重试、掩码丢失 |
| P2 | 静置空电校正绕过 OCV 的基本数据资格 | [bms_soc.c](../bms/core/bms_soc.c) 复用 rest context，排除检测阶段。`28fcced7` | 十类不合格上下文拒绝、健康正控制 |
| P2 | SH 生产默认 LED=1 与生产门禁冲突，测试额外宏掩盖冲突 | [sh3673510_defaults.h](../bms/products/sh3673510_defaults.h) 生产默认 0，显式开启仍拒绝。`197fd03f` | 四产品实际预处理正负控制 |
| P2 | 24 h 区间改用独立计时后，旧运行诊断仍显示短计时 | [app_power.c](../bms/app/app_power.c) 按 region 报告对应累计值。`2a9e6eed` | 实际电源入口累计 110 s 报告 |
| P3 | heater_allowed 与 heater_supported 在所有产品完全相同 | [bms_board.h](../bms/core/bms_board.h)、板级实现和 feature 删除重复 API/判断。`c65e8dc5` | 四产品 feature 与能力契约 |
| P3 | 旧 workflow 引用六个已删除脚本及旧产物位置 | 删除 afe-hw-split-ci，保留统一 [bms-ci.yml](../.github/workflows/bms-ci.yml)。`434c5f3e` | 路径核对；未触发远端 CI |
| P3 | D013 文档写 4S，实际默认宏是 10S | [D013 reference](D013_PRODUCT_REFERENCE.md)、产品注释及 README 更正，未改硬件参数。`cf3f1c95` | 对照宏及 SCONF4 组合为默认 0x6A |
| P3 | SOC 固定周期文本断言及 DVC 恢复签名过时 | `b20709bd` 更新真实入口与时间场景；`59465b40` 同步并行 SH control 接口的夹具类型 | 对应 contract/生产函数回归 |

## 状态所有权与保留理由

- guard 仍独占通信隔离、配置 inhibit、watchdog bus silence 和三次新鲜样本资格。芯片后端保留硬件锁存、寄存器恢复及物理释放证据；应用意图不等于 Gate 已导通。
- 软件保护和硬件保护仍独立。SC/OCD/OCC 不允许用关 MOS 后零电流代替负载/充电器释放证据。无效温度不能成为解除旧温度故障的证据。
- Open-Wire 的检测阶段、疑似结果、已确认故障、清理失败各有不同含义，不能合并成一个故障位。均衡/加热需要的互斥保留，未把检测阶段统一变成充放电故障。
- 充电会话只是功能输入；SOC 继续用带符号电流积分。没有新增 CHARGE/DISCHARGE/IDLE 总状态机，也没有 Manager、Event Bus 或兼容层。
- 普通 Suspend、ACC/命令关机、保护性休眠仍分别处理。低压计时不由 BLE、OTA、业务 busy 或 ADC 等待反复清除；实际执行受各产品已有硬件入口约束。
- Config 仍是安全配置所有者；State 与 Event 降级不改变 CRC、提交标记、journal 几何、重试预算、掉电一致性或 Config 的失败锁存。SOC 缺持久态时是估算，不是精确剩余容量证明。
- OCV、满端点、空端点与显示跟随解决不同问题，保留现有电池曲线和容量/SOH策略；没有用源码去推断电芯真实曲线，也没有新增充放电切换补偿。

## 本轮未据猜测修改的边界

1. SH VADC/CADC 完成事件异步性及最大样本年龄：仍需原始标志轨迹和实板验证，不能仅为了增加采样频率改新鲜度资格。
2. 软件保护名义采样滤波与芯片恢复计数：其连续样本含义不能直接替换成墙钟等待；本轮只改明确代表 watchdog/均衡时间的计数。
3. D013 串数、NTC、引脚及各产品实际装配；D008 后缀/Rsense/Gate。文档默认值不构成硬件签核。
4. SH 固定 UART 低功耗阻断保持；低压定时到期不证明实际进入深睡。D008/SH 各唤醒源、充电器/负载释放证据需分别验收。
5. 非安全存储降级、欠压反复跨区、无效温度恢复、watchdog 到期后 MOS 状态，以及 Flash 擦写中真实掉电仍需实板测试。

## 验证与回退

每个行为改动用相关现有 host 入口或实际生产函数故障注入验证。若运行期间 HEAD/源码被并行任务修改，runner 会报告完整性失败；即使场景全 PASS，也不计作该轮统一通过。

最终代码固定后执行四产品 `sources --check`、开发 ELF `link --jobs 4`、`resources` 和受影响的 host 子集。D008 使用默认 16S LFP；不将其声称为 20S/24S 或生产审批通过。日志、对象和报告统一放用户 `CodexTemp/bms-monorepo/optimize-20261010`；最终实际结果以该目录收据和本轮交付说明为准。未执行完整 host、sanitizer、静态分析、远端 CI 或实板验收。

回退采用对应独立提交的逆向补丁，先核对后续依赖。尤其 watchdog 计时、低压独立计时与其新增夹具/诊断应一起回退；不要通过撤掉三新鲜样本、物理释放证据或 Flash 校验来让测试通过。
