# 第三轮简化：循环 SOH、生产能力与维护入口

实施基线为 `9e8a2a05ad581baad4846887aa3953bd615eb4c1`，在 `codex-bms-monorepo` 原工作区直接开发。需求来自“完整审核与简化建议”及本次明确授权删除 SOC 容量学习。

## 实现与行为

- 删除学习状态机、候选/拒收统计、学习容量持久接口与配置开关；仅沿用原循环 SOH 曲线。80 次以前 100%，500 次 90%，800 次及以后 80%，整数插值和循环饱和规则保留。OCV、端点、显示、ETA、时间资格及低 SOC 告警保留。
- CFG2 322-byte payload、State 44-byte payload、schema 3、各更新编号及 Flash 分区保持。原学习槽写零/读取忽略，旧有效 SOC/循环不重置；原诊断槽恒零，SOH source=1/confidence=25。
- Production 关闭 trace ring 和 DVC raw register window，运行日志继续关闭；保留 boot、AFE/MOS、校准、storage error 与 fault snapshot。开发保留诊断能力，raw 写始终拒绝。
- 产品签核在生产镜像生成前检查：D008 SCD、D008 20S NMC 保护、D013 硬件配置当前均未签核。真实签核后修改源码并提交，禁止 EXTRA_DEFINES 覆盖；未签核的工程 ELF 可以继续 link/resources，不能据此交付生产镜像。签核前拒绝 build/rebuild/finalize，rebuild 不先删除现有产物。
- 四份相同策略头合为 `bms/products/bms_parameter_policy.h`，八类编号不变。板级差异、AFE/guard、软件保护、requested/effective 与 Config/State/Event 的责任边界保留。
- DVC 九个仅内部消费的函数改为 static，删除无消费者的 boot-zero getter；寄存器编码、RC/W0C、校准及恢复函数体不变。Boot-zero 快照仍由原路径发布。
- Telink SDK BLE/FIFO/Flash 回调与 SDK 初始化移到 `bms/platform/telink/app_ble.c`；`app.c` 保留主循环、采样、MOS、业务低功耗和 `app_init`。正常启动在原业务开始位置调用 app_init，其他任务顺序不变。
- 测试专用 AFE shutdown/wake 入口只允许 BMS_HOST_TEST；production 禁止该宏。实际 shutdown/恢复状态与硬件动作不改。
- 测试按同产品范围与领域直接合并，公共 storage 更名；catalog、CI、失败日志和结构化证据随之更新。独立场景函数保留原断言/故障注入，没有新增调度框架。只有已删除学习功能的预期改为循环边界、容量更新、重启、旧保留槽兼容场景。
- CLI 新增 test/release，日常入口为 link/test/static，明确需要镜像时 release 串联原 checker/manifest/verify。历史说明与固定证据移到 docs/history，当前导航见 [README](README.md)，根 README 继续直接链接全部项目文档。

## 维护规模

| 指标 | 基线 | 本轮 |
|---|---:|---:|
| SOC 源文件行数 | 2034 | 1617 |
| app.c 条件编译指令 | 115 | 58 |
| DVC public 非 inline 函数声明 | 35 | 25 |
| 独立 *_check.py 入口 | 72 | 45 |
| 重复参数策略头 | 4 | 1 |

这些数字反映维护入口，不代表安全覆盖率。测试入口合并后执行组数降低，原场景断言保留；保护物理恢复、采样资格、存储掉电模型仍独立可定位。

## 验证边界

已完成四产品 sources --check、相关 host 回归及开发/production 的工程链接与资源检查。完整 host 共 110 组（45 个入口按适用产品展开加公共工具检查），包括长时 SOC、逐字节掉电、保护/恢复、寄存器、协议及故意变异场景；固定快照结果、输入指纹与六 production 资源收据见下节。此次没有请求 BIN，未生成本项目固件镜像、烧录或 OTA。

实板 Gate/恢复波形、Flash 掉电、reset 策略、低功耗/BLE 时序和容量精度验收仍需 [HARDWARE_VALIDATION](HARDWARE_VALIDATION.md) 的独立证据。产品签核值保持待确认，未自行选择新阈值或短路政策。

WSL Ubuntu GCC 13.3 的选定 ASan/UBSan 回归 37 组全部通过、运行期间源码未变化；这是辅助 host 证据。使用 validation_support 编译的 TU/场景实际插桩；同组保留的旧自带编译命令仍按其原证据层级报告，未宣称全部 45 个入口插桩。

四产品 `BMS_DEBUG_LOG_ENABLE=1` 的开发 link/resources 已通过，编译零警告；D008 此开发日志配置 Flash free=6828 B 产生既有开发预算告警。它不是 production 结果，不降低生产 8192 B 硬门槛。

## 固定快照验证记录

业务代码提交为 `27765c88e8d6a888f73a081805755995ebccd780`。以下收据使用该提交及实际 SDK 快照，后续仅 CI 命令缩进、对应工具检查和文档证据变更。完整路径/哈希、MAP 前 30 个自有函数与前 30 个 rodata 见 [机器证据](SIMPLIFICATION_20261007_EVIDENCE.json)。

- Windows 完整 host：110/110，通过；WSL 选定 ASan/UBSan：37/37，通过。两端实际源码输入指纹相同：`996ede8ef294b96193e406be66f463fff8def597f1a212f836652d816ac38b17`，运行期间未变化。收口后补充的 YAML 命令折叠检查已实际解析为单条 shell 命令，36 项工具单测通过。
- 四开发 + 六 production ELF/MAP/resources 全部通过，TC32 编译均零错误、零警告；全部 resources 标记 image_generated=false。
- 六 production ELF 均不含 trace 缓冲及测试 shutdown/wake API，D008 也不含 raw API。
- 四目标 static 完成：D008 104 项 style，其余各 86 项 style，无 error/warning 项；SH 的 bus_mux.h/sif_send.h 两个 DVC 专用头在该目标未分析；MISRA 未执行。未据 style 项批量改名/改逻辑。

| Production 配置 | Flash 占用投影(B) | Flash free(B) | 预留主栈后 SRAM 余量(B) |
|---|---:|---:|---:|
| d011 / 固定板级 | 111140 | 15836 | 7120 |
| d013 / 固定板级 | 110100 | 16876 | 7140 |
| d014 / 固定板级 | 110948 | 16028 | 7120 |
| d008 / 16s-lfp | 114228 | 12748 | 6288 |
| d008 / 20s-nmc | 114228 | 12748 | 6288 |
| d008 / 24s-lfp | 114228 | 12748 | 6288 |

D008 三 profile 均为 12748 B（12.45 KiB），达到 ≥12 KiB 目标，16S 相比修改前的同口径 8268 B 增加 4480 B；生产 8192 B 硬门槛保留。默认开发 D008 为 7996 B、运行日志开发为 6828 B，均产生开发预算告警，不能把开发诊断版当生产余量。最大自有函数仍为 SOC（3720 B）、Modbus（2428 B）、软件保护（2060 B）、feature（1908 B）、DVC 采样（1644 B）和 journal save（1628 B）；它们承担真实业务/安全责任。最大 rodata 是 BLE attributes（768 B）、DVC semantic read 跳转表（316 B）及 NTC（112 B）。达标后未继续裁剪必要快照、安全事务或协议属性。

### SDK 与 Git 状态边界

本次发现两处修改前已存在的 SDK 覆盖：common/sdk_version.h 的 PATCH_NUM 为 1（Git 为 0），drivers/B85/clock.c 的 48 MHz 分支使用 48000000（Git 为 24000000）。它们与仓库附带 Patch_0001 原文件一致，且实际 SHA256 与保存的 `9e8a2a05` 构建输入收据一致。本轮没有修改、重置或提交这两个 SDK 文件。

Windows Git stat 报告干净未识别这些覆盖；WSL Git 初始报告 97 项 dirty，其中多数为换行配置差异，规范化配置后剩这两处真实覆盖。机器证据保留原始状态与两文件实际/提交 SHA256。**本轮验证对应源码提交加已记录 SDK 覆盖及实际输入指纹，不声称纯 Git HEAD 或远端 CI 使用同一 SDK 快照。** 新环境复现必须对照收据与受控官方补丁，不能仅凭 Git clean 推断一致。

三个产品签核值仍为 0。工程验证与资源达标不等于正式镜像签核；本轮没有生成本项目 BIN，没有烧录、OTA 或实板验收。
