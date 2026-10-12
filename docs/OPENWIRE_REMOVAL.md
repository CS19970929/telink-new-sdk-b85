# 主动断线检测删除与关联逻辑精简

后续变更：SOC 已按用户决定解除加热/均衡活动阻断，见 [采样与 SOC 解耦](SAMPLING_SOC_DECOUPLING.md)。下文仍保留对应固定提交的记录。

日期：2026-10-12。基线 `ee284189`，独立分支 `codex-remove-openwire`。用户明确要求全部删除主动电芯采样线断线检测及关联逻辑，这是功能裁剪，覆盖 D008、D011、D013、D014；不把它描述为安全覆盖等价的重构。

## 删除范围

- feature：首检等待、空闲样本计数、周期、超时、失败退避、奇偶阶段结果、疑似、故障锁存、清理状态及全部运行快照计数。
- 两套 AFE：公共 start/poll/stop API、guard 转发、DVC COW 激励/窗口/诊断电压缓存/专用采样代数、SH OWD 触发/FLAG3 消费/奇偶原始掩码与退出处理。
- 电压消费者：DVC/SH 普通电压发布中的诊断分支，软件单体/整包电压与压差保护的诊断暂停条件。新正常 VADC 直接发布，原保护触发、回差和滤波继续工作。
- SOC：诊断/不可信专用输入、有界暂停与三新帧恢复状态、专用 OCV trace。OCV 直接检查正常测量、静置与上下文资格；删除失去消费者的 SOC 新帧字段及 AFE 辅助快照转发字段。
- MOS、均衡、加热：断线引起的阻断和互斥删除；取消只剩断线用途的双向阻断 API。原来的主动关闭/采样失效两个功能停止入口合为 `bms_features_stop()`，共享同一撤销行为；加热充电方向阻断继续保留。
- 协议：删除 48-word RAM 快照、发布函数、状态/错误枚举与数据读取；撤销能力位 `0x0100`。原 MOS 原因 bit 6 不复用，其他位编号不变。
- 测试/工具：删除纯断线场景及其替身；原 SH 检测测试中的均衡续期/回读测试独立保留。SOC 仿真删除断线输入列和注入场景，现有普通 ADC、保护、SOC、均衡和通信回归继续运行。

生产源码/头文件共 22 个文件，新增 93 行、删除 1037 行，净减少 944 行（不含测试、文档与原用户补丁）。删除 34 个旧函数定义，停止逻辑合并为 1 个 `bms_features_stop()`，函数定义净减少 33 个；没有新增抽象层。

| 文件 | 删除的函数定义 |
|---|---|
| `bms/afe/dvc1124/dvc1124.c` | `DVC1124_OpenWireBegin`, `DVC1124_OpenWireGetResult`, `DVC1124_OpenWirePoll`, `DVC1124_OpenWireReset`, `DVC1124_OpenWireStop`, `DVC1124_StartOpenWireCheck` |
| `bms/afe/dvc1124/dvc1124_feature_backend.c` | `dvc1124_backend_openwire_poll`, `dvc1124_backend_openwire_start`, `dvc1124_backend_openwire_stop` |
| `bms/afe/sh3673510/sh3673510_feature_backend.c` | `sh3673510_backend_openwire_poll`, `sh3673510_backend_openwire_start`, `sh3673510_backend_openwire_stop`, `sh_ow_enable`, `sh_ow_result`, `sh_ow_trigger` |
| `bms/app/bms_features.c` | `bms_features_diag_reasons`, `bms_features_on_afe_invalid`, `bms_features_on_output_disabled`, `bms_features_outputs_blocked`, `finish_openwire`, `openwire_eligible`, `openwire_hard_fault`, `openwire_put32`, `openwire_result_abnormal`, `openwire_wait_remaining`, `publish_openwire`, `service_openwire`, `stop_feature_outputs` |
| `bms/core/bms_afe_guard.c` | `bms_afe_openwire_poll`, `bms_afe_openwire_start`, `bms_afe_openwire_stop` |
| `bms/core/bms_diag.c` | `bms_diag_openwire` |
| `bms/core/bms_soc.c` | `soc_ocv_voltage_ready`, `soc_trace_ocv_inputs` |

状态删除包括 feature 的全部 21 个 `openwire_*` 成员、DVC 诊断结果及采样代数、SH 检测阶段状态、SOC 4 个诊断恢复成员、96 B 诊断窗口缓存，以及仅用于该链路的输入字段。编译后的实际 RAM 变化以资源表为准，不把结构字段尺寸简单相加当作链接结果。

## 硬件与协议边界

DVC1124-2 DS V1.1 原 PDF 第 15 页说明断线功能开启下拉，约 1 秒自动结束；RM V1.2 第 22 页定义 COW。本分支不再发起激励，原初始化配置仍清零 COW，不能删掉用于保持关闭的寄存器掩码。

SH36735XX CV1.0A 原 PDF 第 16、32 页说明 OWD_EN/OWD_TRG 控制检测。静态配置不再提供这两个业务宏，位值固定为零；OWD_INT 从 ALARML 配置移除。切换 C+/LOAD 物理恢复模式仍保留 wake 位和读回验证，同时保持 EN/TRG 关闭。OWV 与正常 ADC 中断共用寄存器，保留其原无效阈值编码及硬件寄存器定义，不把这些定义当作仍运行的检测逻辑。

旧诊断窗口 `0x3600..0x362F` 不再读取出健康/故障数据，Modbus 返回非法地址；该区间继续作为禁止写入的保留地址，不分配其他功能。上位机应根据能力位隐藏或标记“不支持”。Windows 上位机维护来源是另一个分支，本次未复制其源码或发布客户端；旧客户端若强制轮询，将收到不支持响应，不能宣称已完成客户端联调。

普通无效串位仍发送 `61001`。温度探头开短路/TEMP_BREAK、ADC 活性和新鲜度、watchdog 总线静默、三次有效新样本、SC/OCD/OCC 物理恢复、均衡真实 OFF 确认、加热熔断及低功耗失败路径均保留。Config/State/Event schema 3、CFG2 payload、OTA 分组与存储布局不变。

## 时序与其他策略的处理

删除主动诊断同时消除了首次两段等待叠加、检测周期被持续电流延期、诊断跨帧暂停/清理以及诊断失败影响 SOC/均衡的整条链路。

200 ms 应用调度、AFE 自主 ADC 周期以及恢复资格不是同一个时钟。此次不修改它们，也不把有效缓存当作新测量。普通 SOC 400 ms GAP 仍会舍弃盲区；未借删除诊断放宽积分门禁。

均衡充后静置会话、均衡温区与保护参数的关系、OCV 对故障上下文的依赖仍有各自业务或硬件依据。未提供新温区、储存耗电策略或故障恢复要求，因此不将这些策略候选当作已确认缺陷随意删除。后续需从具体产品行为判断，不能声称本次消除了项目所有潜在问题。

## 验证

生产源码提交 `095829d0`；测试契约修正提交 `adb79e3`。最终完整主机回归 **114/114 通过**，0 失败；相关 **ASan/UBSan 21/21 通过**，0 失败。完整日志和中间产物均保存到 `%LOCALAPPDATA%/CodexTemp/bms-remove-openwire/`，不生成 BIN，不执行 OTA 或烧录。

完整回归包含四产品 `sources --check`、保护/恢复、采样、SOC、存储掉电注入、通信畸形帧、低功耗和验证工具故障注入。ASan/UBSan 范围为 feature、D008 guard/SOC/电源、保护、诊断、DVC ADC、SH 均衡/恢复及 D014 真实生产安全闭环；报告 `partial=true`，不称为全部 114 组 sanitizer 回归。完整报告 `partial=false`。本次未另行执行 cppcheck/MISRA，也未做实板操作。

两份报告在 `adb79e3` 运行，896 个输入文件 SHA-256 一致，指纹 `dbe252a761cba3936f764e494f232d6fcf5daa2331b222d772500eb8150f158c`；`source_changed_during_run=false`，完整性错误为空，最终逐文件核对仍一致。报告分别在 `full-final/report.json`、`asan-final/report.json`，交叉检查收据为 `final-input-audit.json`。

与 `bms-functional-coupling/full/report.json` 基线相比有 17 项结构化摘要变化：四产品诊断能力撤销使 Modbus 报文摘要改变；四产品删除 11 个仅针对诊断电压隔离的保护断言，各保留 508700 个普通保护断言；三产品旧检测用例撤销、三产品原均衡子集独立列出；三产品 ALARML 从 `0xFF` 变为 `0xDF`，仅关闭 OWD_INT。不能把本次功能裁剪描述为全部行为与基线相同。

过程中的失败报告仍保留：初轮接口/夹具尚未同步，随后发现旧测试把只读诊断状态混入 MOS 控制断言，以及仍预期保留 OWD_EN。已收窄控制断言并保留诊断检查，寄存器预期按关闭 EN/TRG 更新；256 × 2 的模式组合、读回失败、睡眠禁止及恢复资格检查保持。修正后的七组契约回归及最终全量均通过。

四产品和 D008 三个 profile 共六配置的 `link`、`resources` 全部通过。目标编译对应 `095829d0`，之后仅修改测试与文档，生产源码相同；六份 ELF SHA-256 与资源收据一致，链接符号中没有主动断线检测函数或状态。编译日志无 error/warning，资源工具的低余量提示另列。

| 产品/profile | Flash 前 → 后（B） | Flash 减少（B） | RAM span 前 → 后（B） | RAM 减少（B） |
|---|---:|---:|---:|---:|
| D008 / 16s-lfp | 124100 → 119812 | 4288 | 25852 → 25612 | 240 |
| D008 / 20s-nmc | 124100 → 119796 | 4304 | 25852 → 25612 | 240 |
| D008 / 24s-lfp | 124100 → 119796 | 4304 | 25852 → 25612 | 240 |
| D011 | 123492 → 119236 | 4256 | 25400 → 25196 | 204 |
| D013 | 122676 → 118420 | 4256 | 25380 → 25176 | 204 |
| D014 | 123332 → 119060 | 4272 | 25400 → 25196 | 204 |

Flash 是 ELF/MAP 推算的镜像空间；六份资源收据均为 `image_generated=false`，没有 BIN。RAM span 包含代码/缓存/中断栈等地址占用，不是运行时栈水位。D008、D011、D014 仍有 Flash 余量低于 8 KiB 的预算提示，不能据链接成功宣称生产放行。基线日志在 `baseline-development/`，最终目标日志在 `verified-development/`，资源和 ELF 指纹汇总为 `verified-resources.json`。

原工作区的 `bms/products/d008/bms_product.h` 修改保留且不提交；对比两侧使用相同修改。生产模式基线的首个门禁为工作区不干净，未绕过；资源比较采用 development、同工具链和同优化配置。

## 尚需实板验证

- 四产品健康运行、长时间空闲、重初始化及唤醒后不产生主动激励，正常采样与均衡连续。
- 新电压及时发布，OV/UV/压差保护、恢复和 MOS 物理 Gate 行为符合原参数；通信失联和负载重接不绕过原资格。
- SOC 正常静置及充放电累计，温度异常、均衡失败、加热和低功耗恢复路径。
- 新旧上位机对能力撤销和非法地址响应的显示、后台轮询与通信负载。
- 采样线开路但读数暂时合理的情况不再具备主动诊断覆盖；软件回归、链接和资源减少不能证明该覆盖损失已被其他保护替代。
