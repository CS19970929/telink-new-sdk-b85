# 功能间影响与职责精简

日期：2026-10-12。源码基线 `2baa713d`，本轮生产代码 `e6c68261`，分支 `codex-bms-monorepo`。区分已经实施的等价调整与会改变现有业务策略的候选，不把后者称为已确认冗余。

## 已实施：参数资格直接进入输出门禁

原链路：`参数有效性 → bms_features_outputs_blocked → guard.apply_requested → backend FET`。

现在由 `guard.apply_requested` 直接读取 `bms_protection_params_valid()`，feature 只报告自身的已确认断线和未完成清理。原先分开的两次 OFF 判断合并为一次：`!output_enabled || !parameters_valid || feature_blocked`。

任何输入组合下的最终 CHG/DSG 请求均不变。参数 getter 同时包含保护参数有效性与 Config 启动资格，仅读 RAM；feature 查询也无副作用。全仓只有 guard 一个生产消费者。输出门禁不再依靠加热/均衡/断线功能替参数模块传话，没有增加接口、状态或抽象层。

feature 内部参数校验仍保留：heater、balance、open-wire 分别决定是否能启动自己的硬件动作，与最终 MOS 门禁不是重复动作。通信静默判断仍在参数与功能判断之前，不增加静默期间的总线流量。方向性阻断仍留后端，DVC AUTO_DIODE、SH 同口反向恢复不变。

本轮删除的是一次间接职责委托和一处分开的 OFF 判断，不是删除某项 BMS 功能。生产文件含说明注释净增加 2 行，没有以减少行数代替职责改进。

## 已核对的功能关系

| 关系 | 当前行为与必要性 | 处理 |
|---|---|---|
| 保护参数 → MOS | 参数无效或 Config 启动失败必须禁止输出，该判断属于输出资格 | 已从 feature 移到 guard |
| State/Event → MOS | 参数启动流程已只用 Config 结果决定输出资格，State/Event 故障独立降级 | 已解耦，保持现状，不重复称为本轮删除 |
| 软件保护 ↔ AFE 硬件保护 | 时序、硬件锁存和恢复能力不同；在线软件写入不应触发 AFE 全量重配 | 保留独立参数与恢复流程 |
| Open-Wire → 电压消费者 | 激励/清理/最后诊断帧不能作为普通电压确认；SOC 电压校正、均衡与电压保护必须知道测量性质 | 保留；现有合格电流区间仍可积分，不能把诊断变成全部 SOC 停机 |
| Open-Wire → MOS | 活动检测本身不关 MOS；已确认断线或清理未完成才形成硬阻断 | 保持，不能把正在检测当故障 |
| heater → CHG | ARMING/ACTIVE 阻断充电方向，零充电电流确认后才启动加热；低温本身是预热需求 | 保留；低温不能反过来成为 heater 硬故障 |
| heater ↔ balance/Open-Wire | 当前策略不同时执行加热与均衡/断线激励 | 保留；解除互斥需验证电路与测量扰动 |
| balance → SOC OCV | 泄放/采样扰动不等于静置电压，硬件回读非零时仍阻止 OCV | 保留；不能用软件请求 OFF 代替硬件 OFF |
| SOC Low / 显示 / ETA → MOS | SOC Low 不在 MOS 阻断掩码内，显示与 ETA 不授权物理输出 | 保持单向报告关系，不新增 SOC 百分比关 MOS |
| Config → SOC | 容量/化学体系改变尺度或曲线；名称、SN、均衡与加热普通写入没有通用重启全部模块广播 | 保留按参数类别的具体调用，不增加统一更新框架 |
| PM → 通信/存储/AFE | Suspend、D008 显式断电/ACC、保护性深睡失败语义不同；保护性到期不能被普通业务无限延迟 | 不统一为一个全局 busy/fault，保留现有有界行为 |
| 诊断/日志 → 控制 | MOS 诊断缓存组成原因，控制不通过诊断寄存器反向授权；运行日志不增加 PM 阻断 | 保持观察关系；事件落盘仍服从具体休眠入口策略 |

源码入口：`bms/app/bms_features.c`、`app.c`、`app_power.c`、`bms/core/bms_afe_guard.c`、`bms_soc.c`、`bms_parameters.c`、`bms_parameter_access.c` 及两套 AFE backend。D008/D011 支持 heater；D013 不支持 heater/balance；D014 无 heater，不能把禁用产品的测试结果当作相应硬件功能验收。

## 最有价值的后续策略候选

### 1. 均衡是否需要“曾经进入充电场景”

`update_balance_charge_session` 在有可信充电电流或确认充电源时建立会话，零电流保持、真实放电清除。同样电压、温度和零电流，冷启动静置与充后静置可能得到不同的均衡决定。这是历史依赖，不是被动均衡的直接测量输入。

可选简化是以当前电压、压差、温度和放电条件直接决定均衡，删除会话及建立/撤销链。但会增加未经历充电的静置均衡，改变储存耗电和静置行为。当前产品文档包含充电会话条件，不能在功能不变的承诺下直接删除。优先明确这一条是否仍是产品需要。

### 2. 均衡温区是否应借用充电保护恢复阈值

`balance_temperature_safe` 除产品均衡热回差，还使用 `charge_otp_recover_x10`、`charge_utp_recover_x10`、`mos_otp_recover_x10`。修改充电恢复参数会同时改变均衡准入温区；`balance_hard_fault` 又叠加充放电高低温及过流等故障。

高温、测温失效与均衡发热的关系直接；低温充电限制和被动泄放并不等价。可评估只消费均衡自己的温区与明确硬故障，但先要确定各板最低工作温度、均衡电阻/MOS 温升和既有参数含义。不能直接删除低温/故障检查，也不为解耦擅自新增一套 Flash 参数。

### 3. OCV 是否应依赖所有现有故障锁存

`soc_rest_context_valid` 已检查电压、温度合理性、加热/均衡、电压不可信，又检查 AFE、温度、电流和整包保护故障。可能出现测量已稳定，但故障锁存尚未解除，OCV 仍无法累计。

更窄的原则是让 OCV 依赖真实静置和测量质量，但必须区分故障是否意味着电流读数不可信、采样异常或持续内部耗电。先用锁存保留但测量恢复的回放量化差异，再决定具体资格。现有故障字段也用于只读诊断，不宜简单合成丢失原因的 `fault`。

### 4. 无效温度是否必须禁止主动断线检测

`openwire_hard_fault` 把 `TEMP_BREAK` 与 AFE/短路等故障一起作为检测准入条件。温度通道无效与电芯采样线诊断对象不同，有进一步分离的可能；但检测主动操作 AFE，不能仅凭两个传感器不同认定互锁多余。需要核对具体 DVC/SH 激励条件、故障期总线策略与停机状态，保留超时、退出确认、退避和异常锁存。

建议顺序：均衡充电会话 → 均衡温区来源 → OCV 故障资格 → 断线检测准入。前两项最直接决定能否删去业务历史状态和跨功能参数依赖；后两项更接近测量可信度边界。本轮未选择新的产品行为或硬件策略。

## 验证与交付

局部回归 12/12 通过，包含四产品 feature、D008 guard/SOC/电源、三产品 SH 恢复与安全链、D014 真实安全闭环。新增测试覆盖授权/参数/功能阻断八种组合，并确认参数失效仍关闭加热和均衡。

完整主机回归 **114/114 通过**，0 失败；与上一轮可比较的行为摘要无变化。相关 **ASan/UBSan 12/12 通过**，0 失败，覆盖同上局部范围，报告明确 `partial=true`，不表述为全部 114 组 sanitizer 回归。完整报告 `partial=false`。

两份报告输入指纹均为 `75fbbe74a42975ee198560503b677edbb3f8c3f0a9e389c0bde5539322c30a24`，运行期间源码未变化，完整性错误为空。已逐文件复核当前 SHA-256，并核对原 D008 补丁全文不变、构建树无 BIN，收据为树外 `final-input-audit.json`；原始报告为 `full/report.json`、`asan/report.json`。

Standards 独立复核：无剩余阻断；Spec 独立复核：无剩余阻断。二者仅做源码审查，不冒充独立运行测试。

四产品 `sources --check`、四产品链接及 D008 20s-nmc/24s-lfp 链接全部通过；六次链接均为 0 errors / 0 warnings。本轮不重复执行 cppcheck/MISRA，不沿用上轮结果声称当前静态分析通过。

| 产品/profile | Flash byte | 相对基线 | RAM span byte | 相对基线 |
|---|---:|---:|---:|---:|
| D008 / 16s-lfp、20s-nmc、24s-lfp（分别链接） | 124100 | 0 | 25852 | 0 |
| D011 | 123492 | -16 | 25400 | 0 |
| D013 | 122676 | -16 | 25380 | 0 |
| D014 | 123332 | 0 | 25400 | 0 |

资源对比使用上轮 `bms-issue11-followup/build/47196ba33c07/` 与本轮 `bms-functional-coupling/build/47196ba33c07/` 对应 `gen/resources.json`。同为 development、`-O2`、dirty=1，含相同用户 D008 修改。六配置 `image_generated=false`；Flash 余量小于 8 KiB 的既有提示仍在。RAM span 及主栈预算不是实测栈水位。

源码提交：`e6c68261 重构：由 AFE 输出门禁直接检查参数资格`。树外证据：`C:/Users/Administrator/AppData/Local/CodexTemp/bms-functional-coupling/`。用户原 D008 参数修改不纳入提交；不生成 BIN、不烧录、不执行 OTA。主机与链接证据不替代真实 Gate、保护恢复、Flash 掉电或低功耗验收。
