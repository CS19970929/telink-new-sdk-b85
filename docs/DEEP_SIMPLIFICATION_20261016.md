# BMS 深度减法重构记录

## 起点和隔离

用户指定起点 `dd7115033e9e6d73808fdf303d78f37196ca7b40`。原工作区分支
`codex-remove-openwire` 的本地 HEAD 为 `3cb223237917ac6951302446b6b7e5e592656003`，
远端为指定起点，原工作区存在未提交修改。本轮未复制、丢弃或提交它们。
独立工作区分支为 `codex-bms-deep-simplification-20261016`；已有 `codex` 引用使
`codex/bms-deep-simplification` 无法创建。明确 SHA 优先于附件中“最新 HEAD”的一般描述。

## 最小架构及审查原则

五类职责足够：Core 保存保护、SOC、功能策略与安全资格；AFE 保存寄存器、转换完成、
芯片锁存和物理恢复证据；Platform 保存 GPIO/总线/Flash/时钟/PM；Product 选择能力、
参数及源码；Application 用固定顺序调度。编译期选择一个 AFE，不增加运行时注册、
动态分配、任务框架或统一大状态机。

先判断接口是否承载独立事务，再判断是否有消费者。输出意图、已成功命令、AFE 回读、
转换事件和物理证据不能合并。SC/OCD/OCC 的零电流不能作为移除证据；有效缓存可供
业务计算，但不能推进三次新样本资格。协议、61001、CFG2/schema 3、OTA/APP 分区保持。

AFE 原页依据：DVC1124-2 RM V1.2 PDF p6–8 的读清转换标志、R6 输出状态和 CST；
DS V1.1 p13–15 的转换周期、p22 的唤醒；SH36735XX CV1.0A PDF p16–20 的
C+/LOAD、转换、watchdog 和自主 MOS，p32–36 的模式、flag 与保护开关。
使用现有型号配置；未核验硅修订和缺失的三款 SH 原始图纸/BOM，不扩大原资料保证范围。

## 基线和第 0 轮：恢复验证入口

原始完整回归：116 组，80 通过、36 失败。原始四产品生产链接：SH 三产品通过，
D008 在 profile 验证时被错误的摘要比较拦截。当前源码装配摘要本身有效。

修复：工程 profile 覆盖先验证源码装配的固定参数包，再返回工程装配；镜像交付仍要求
与源码选择一致。没有改参数、更新组选项或固定标识。修复诊断关闭时两个 SOC 局部量
的编译警告；夹具显式选择诊断宏，预处理与编译保持一致；补实际 profile-store TU；
不再调用 D013 不编译的 heater/balance 后端；更新已取消固定装配白名单的旧断言。
没有以关闭 warning 或放宽生产保护门禁使测试通过。

验证收据根：`%LOCALAPPDATA%/CodexTemp/bms-deep-simplification/20261016/`。
`baseline-host` 为原始完整失败记录；`round0-host`、`round0-repaired`、
`round0-last`、`round0-final` 保留逐次修复结果。所有原失败项均在后续针对性重跑通过；
工具单测 55 项通过。最终完整回归将另行记录，不能把合并的局部结果写成全量通过。
六配置生产 ELF/MAP 已完成；D008 数据含第 0 轮验证入口修正且 dirty=1，
SH 三产品数据来自 clean 原始起点，资源比较须标明这一证据差别。

## 逐轮记录

| 轮次 | 提交 | 修改范围 | 删除函数 | 删除状态 | 验证 |
|---|---|---|---:|---:|---|
| 0 | `06dd68b8` | 验证入口、诊断夹具及工程 profile | 0 | 0 | 原失败项针对性复测、55 项工具单测；六配置 ELF/MAP |
| 1 | `caba4286` | DVC 功能并入后端、删除转发和无入口休眠取消 | 5 | 0 | 12 组相关检查及修复后两项 contract；四产品源码/生产链接和资源 |
| 1 补充 | `d9468667` | D008 恢复夹具改用合并后的公开 FET 事务 | 0 | 0 | `d008_recovery_host_check` 的 HW=0/1 及电压恢复通过 |
| 2 | `e49142af` | SH NTC 有效性与加热命令状态去重 | 0 | 2 项 | 15 组 feature/SH/温度检查；四产品源码、生产 ELF/MAP 和资源 |
| 3 | `d70599e0` | DVC 初始化与采样分离，删除复位重试和重复快照 | 4 | 2 | D008 30 组；16S LFP/20S NMC/24S LFP 生产 ELF/MAP 和资源 |
| 4 | `52458ef6` | SH 失败阶段直接返回、均衡事务并回后端 | 2 | 2 | 22 组及修复后的三产品恢复重跑；四产品源码、生产 ELF/MAP 和资源 |
| 5 | `6f3ef232` | Guard 唯一授权，SH 直接执行已裁定请求 | 2 | 3 | 26 组及修复后 contract；四产品源码、生产 ELF/MAP 和资源 |
| 6 | 本提交 | 低压条件统一位表示，关闭诊断时不执行诊断查询 | 0 | 1 | 六组相关检查（D008 夹具宏修正后复测）；四产品源码、生产 ELF/MAP 和资源 |

第 1 轮删除 `dvc1124_feature_backend.c`，温度角色和有效性共用后端现有实现，
删除 `dvc_ntc_valid`、`dvc_temp`；共口 MOS 事务直接作为 `dvc1124_backend_set_fets`，
删除 `dvc_apply_common_port_fet_state` 转发层。删除 `storage_record::read_bytes` 的纯转发，
存储读错误仍由原调用方处理。已提交保护性休眠没有取消入口，删除无生产消费者的
`bms_event_log_cancel_sleep`，重复休眠仍只记录一次；持久重复次数和 payload 不变。

第 2 轮删除 `s_ntc_valid[4]`：原转换范围为 500..300000 Ω，0 从不代表有效值，
因此有效性直接由经过范围校验的 `s_ntc_ohm[]` 得到；-40°C 的温度编码 0 不参与判断。
删除 `s_feature.heater_on`，唯一成功设置入口仍是 `set_heater()`，读取其已经发布的
`g_bms_system_status.bits.heater_status`。删除两项存储（四元素数组加一个字节字段），
不是移动到新结构。实际 RAM 差值以最终 MAP 为准。

第 3 轮：固定运行配置与硬件保护在一次初始化内完成，全部成功才清 `s_need_config`。
失败后的正常采样只返回无效，交 guard 的两次失败及 5 秒 watchdog 静默处理；
采样不再配置或发 shutdown-wake/reset。新增固定配置失败注入，以及 100 次采样不重配断言。
合并 `DVC1124_BmsApp_AFEGet` 与 `dvc1124_backend_sample`，每轮保护只取一次快照；
方向直接读取唯一 CC2 缓存，删除 `s_direction_allow_mask` 和 `s_project_config_pending`。
保护配置和输出授权直接由驱动实现后端契约，删除两层纯转发；初始化内联原来的
`dvc_project_reset_with_shutdown_wake` 单次包装。初始化仍保留有界唤醒等待、配置验证及
两次 270 ms 启动电流检查，未把这些硬件阶段伪装成完全非阻塞；实板 BLE 时序仍待验收。

第 4 轮删除 `sample_failed` 与全局 `s_sample_failure_stage`，采样/保护函数直接返回
失败阶段，调用者一次处理失败。失败阶段、原子发布和清 flag 失败锁存仍由故障注入验证。
删除 `sh3673510_feature_backend.c`，均衡留在同一 SH 后端；有效通道掩码是编译常量，
不再通过 `sh_valid_cell_mask()` 取值。删除 `s_balance_requested`：实际寄存器已经等于请求
且未到续期时无需再保存并比较一份请求；回读不符、写/读失败和 20 秒续期仍独立处理。
没有删除 ADC seen/age、1.2 秒启动温度资格、C+/LOAD 切换等待、短路清除后确认或
`s_flag_clear_failed`；它们分别证明新转换、模式稳定、物理移除及清除事务结果。

第 5 轮删除 SH 的 `s_output_enabled`、`s_requested_charge_on`、
`s_requested_discharge_on`，不再保存第二份授权/请求。公开 `sh3673510_bms_afe_set_fets`
直接执行原仲裁事务，删除私有调用转发和 `sh3673510_bms_afe_set_output_enabled`。
Guard 在提交前统一检查参数、输出许可和三帧资格；SH 正常执行不再反查 Guard 资格。
诊断只读取公开 Guard 快照，不能修改授权；实际 FET 状态仍通过唯一发布接口交给 Guard。
软件保护、加热阻断、芯片锁存和共口方向规则仍在各自所有者，成功命令缓存及物理回读不合并。
联合生产场景覆盖远程许可、配置回滚、启动/失联三帧资格、保护并发、ADC 过期及 tick 回绕。

第 6 轮把低压档位与上档资格合为两个独立条件位，删除一份长期状态；下档与上档
计时仍独立，无效样本保留最后有效条件，可靠充电仅影响上档。诊断仍派生原 2/3 编码。
诊断关闭时不再执行只为诊断服务的总线/Flash 查询。保护性深睡的提交门禁、命令应答
有界等待、AFE 异常计时、过放持续计时、Suspend 与唤醒路径均保留。

本报告随实现逐轮补充。软件验证不代替实板 MOS Gate、保护波形、低功耗电流、
唤醒、Flash 掉电和客户 OTA 验收。本轮不生成正式 BIN、不烧录、不执行 OTA。
