# 本地 monorepo 持久化 / OTA / AFE 在线事务补充审查

本轮基线：`D:/telink/bms-monorepo` HEAD `0aaa842938c37b0016a23d8a29ce2aa416ca2c4d` clean 提交的逐字节冻结内容；快照 `../source`，工作区内容 SHA256 `05fb3147811468a347d3b27538b364d6ce92d56c533a17d713bcc8e1ac689613`。只读源码，所有探针和输出在 `storage_audit`，无驱动/参数/协议/存储布局修改。报告日期 2026-10-06。

## ST-AFE-01 [P1] 配置回滚失败后，新采样资格会释放输出，芯片寄存器尚未恢复

**确认当前缺陷；D014 原源码 TU 主机复现；D011/D013/D008 同路径源码支持，但没有等同四产品器件主机复现，更没有实板 Gate/Vgs 验证。**

- 原厂依据：`SH36735XX CV1.0A.pdf` p34 SCONF6 的 TS、SC、OCD、UV、OV 使能位为 0 表示关闭；p36 OV[9:0] 的阈值为 `code × 5mV`。本子审已重新打开 [p34 原图](SH3510-p034.png) 和 [p36 原图](SH3510-p036.png)。芯片寄存器不会因“MCU 请求配置已恢复”自动还原，软件必须明确写回。`CONFIG_INCONSISTENT` 的输出策略由产品软件负责；提供的 D014 AGENTS 明确要求 AFE 重配失败保持 fail-safe。

- 代码：`bms/core/bms_afe_hw_profile.c:503` 先保存 candidate，`:510` 应用，失败后 `:446` rollback；`:452` 保存 before，再调用 apply。`bms/core/bms_afe_guard.c:270` apply 失败置通信 inhibit，随后 rollback 的 apply 被同一 inhibit 拒绝；`:231` 仅凭三帧资格解除 inhibit。`bms/afe/sh3673510/sh3673510_control.c:177` 和 `:270` 将 effective valid 清为 0，但失败未清 `s_control_ready`；`sh3673510_bms.c:214` 最终输出健康条件没有检查配置一致性，`:855` 的失败也未置重配 pending。`INCONSISTENT` 仅是 `profile.c:458` 的诊断状态，未加入输出门禁。

- 独立场景：合法授权后提交有效候选 OV 3750→3850mV，并将允许写入的 enable_mask 清零。SPI peer 只在 OVL 回读时给一次 CRC 正确但数据不一致的响应；真实芯片写入函数、回读、事务、配置记录、guard、SH backend、控制编码全部来自当前源码。未复用仓库原测试 main 或断言。disable profile 本身不是本报告认定的缺陷；它用于让“错误候选仍在芯片、旧请求已恢复”的影响可观察。

- 实际结果：`commit_error=5 / apply_state=3 / requested_restored=1 / before_OV_code=750 / device_OV_code=770 / SCONF6=0x00`；失败立即关闭输出。故障注入解除后，第 **5** 次调度采样重新发出 DSGMOS ON；此时 `apply_state=3`、`effective_valid=0`、`SCONF6=0x00` 仍未恢复。命令不等于实板导通。探针以业务断言返回 1：`CONFIG_INCONSISTENT_OUTPUT_GAP expected=OFF_UNTIL_VERIFIED_RECONCILIATION actual=ON`，不是编译失败或崩溃。

- 影响：健康通信被错误当成配置一致性证明；输出可能在阈值、使能或延时仍是错误候选/部分新旧混合配置时恢复。软件保护也许仍启用，不能代替保证独立 AFE 保护正确。

- 产品差别：三种 SH 产品编译相同 profile/guard/control/backend 文件，差异是产品宏与默认参数；D014 实测 host。D008 使用同一事务和 guard，`dvc1124.c:1010` 应用失败只记录错误，`:1581` 的采样仅在 s_need_config 才重配，`:1713` 健康采样会清 AFE 错；源码支持同类风险，但本探针未以 DVC 虚拟芯片复现。不能将四产品全部写成实测 ON。

- 建议：建立与通信资格独立的 configuration-consistent 状态；candidate 应用/回读失败即锁定，健康采样不得清除。通过受限的恢复事务或完整重初始化成功写回、回读 before 后再取得新的三帧资格；回滚存储失败仍阻断。不要直接绕过 watchdog 静默窗口。重启时的事务恢复策略另行签核。

- 验证：当前探针保留为回归；覆盖每个寄存器写/回读故障、部分写入、rollback 保存/应用失败、掉电和 MCU reset、长故障 watchdog 窗口、并发 SW/SC 锁存；实板观察 SPI、SCONF6/阈值、ALARM、Gate/Vgs、负载恢复，不只看 Modbus requested 值。

证据：[原始 host 结果](probe_results.json)、[production 宏 host 结果](probe_results-production.json)、[探针脚本](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/storage_audit/online_transaction_probe.py>)、[22 TU 清单及源码哈希/编译命令](probe_compile-production.json)、[源码行证据](source_evidence.json)。

## 已核实的存储和 OTA 参数行为

四产品均编译同一 Config/State/Event/journal/参数事务/Flash 平台实现；实际 `sources.txt` 交叉证据见 [effective_storage_policy.json](effective_storage_policy.json)。当前持久产品 tag 分别 8、11、13、14，不把 D014 历史 wire ID 与持久 tag 混为一谈。四个 `bms_parameter_policy.h:9–17` 的九个目标编号目前全部 `1u`。

| 配置类别 | 编号改变后的行为 | 编号相等 | 来源 / 验证 |

|---|---|---|---|

| SW | 65-word 软件保护使用当前默认 | 保留 | `config_store.c:230`；本轮独立64组合 |

| AFE | 35-word requested profile 使用当前默认 | 保留 | `:233`；固定 wiring/ADC/WDT/Rsense 仍随每次后端 init，不受此编号保留 |

| BUSINESS | 容量、heater、balance 使用当前默认 | 保留 | `:236`；能力宏仍约束实际硬件 |

| SOC | SOC 算法、化学体系、profile 使用当前默认 | 保留 | `:246` |

| CALIBRATION | offset=0 / gain=1000000ppm | 保留 | `:251`；硬件零点流程独立 |

| IDENTITY | 用户 SN / BT suffix 恢复空默认 | 保留 | `:255`；外显空身份回退编译身份 |

| SOC_STATE | 状态/学习数据默认；runtime保留 | 保留 | `bms_state_store.c:179`；本轮源码审查，未注入编号变化 |

| FACTORY_RUNTIME | runtime=0，其余状态保留 | 保留 | `state_store.c:186`；本轮源码审查 |

| EVENTS | 旧编号记录不解码，新默认事件域写入 | 保留 | `bms_event_log.c:89`、`:199`；本轮源码审查 |

`CFG2 schema=2`、322字节 payload、356字节对齐槽；Config 六组与六个编号同 CRC/双提交标记记录。不同产品 tag、装配串数或旧 schema 不直接沿用；旧 schema 拒绝是当前 `AGENTS.md` / `docs/OTA_PARAMETERS.md:46` 明确约定，不能当历史迁移未实现缺陷。State/Event 均 schema2、独立记录，不宣称三域原子事务。`param.c:106–118` 全域启动成功才设置 `s_storage_startup_valid`；普通 `SaveParam` 不会清除此启动门禁。

本轮独立验证以 `docs/OTA_PARAMETERS.md` 的 reset/preserve 和启动提交语义为期望，手工建立六组字段 span，Python zlib 独立读 journal，而非读取现有测试断言。D014 当前22个原源码 TU，在64个类别组合中均按类别恢复/保留，64次重复启动无额外写入；全部六组不匹配时的 **357个 program-byte 截断点及重启**均保持旧或完整新 payload，未出现已提交的混合记录，失败启动阻断、重启重试成功。覆盖的是虚拟Flash编程字节，不包含擦除电气行为、真实OTA镜像切换，也未注入State/Event编号变化。结果见 [ota_selection_results.json](ota_selection_results.json)。当前默认数值由 public 编译默认 API 得到以测试更新策略，本测试 **不证明这些默认阈值正确或已产品签核**。

Telink平台每次 program 分256字节页、逐字节回读验证，erase按4096字节回读验证；OTA/Flash锁状态或失败退避会使 begin 拒绝，见 `bms_storage_platform_telink.c:41–64 / :82–132`。512K 当前配置分区 Config `[0x5B000,0x5F000)`、State `[0x53000,0x5B000)`、Event `[0x40000,0x48000)`；factory保留 `[0x5F000,0x61000)`，当前 runtime 在 State，不自动认为factory区有有效生产数据。物理擦写时延、掉电部分擦除、低压写入、保护锁、Bootloader OTA 所有权仍需硬件测量及SDK运行验证。

## 授权与校准缓存边界

`bms_afe_hw_access.c` 会话为 RAM、60s，完整35-word AFE请求通过授权/完整帧/CRC检查；成功和rollback失败关闭会话。apply失败置INCONSISTENT，不能保证真正完成硬件回滚。COMMIT `:187` 注释称失败也不重复使用，但代码仅清 s_received；validation/store失败或rollback成功并不总清 s_active，客户端可能以同 token 重新stage新完整帧。该接口的“单次会话/允许重试”产品策略 **Unknown**，不是无需需求便认定的安全漏洞；如要求一次性授权应补明确规则与故障分支测试。

电流校准四产品在同一 Config 中以 signed 32-bit offset_ma 与 unsigned32 gain_ppm 持久化；默认0和1000000，编码/解码 `config_store.c:157–158 / :206–207`。缓存有效范围是 **offset ±1000000mA、gain 100000..10000000ppm（0.1..10倍）**，见 `:495–496`；这是代码运算/配置边界，不能称作已签核的工厂校准工程范围。`bms_parameter_access.c:194–199` 的0x2E24四word写入要求有效授权；production宏还要求MODE_FACTORY（`:44–51`）。协议32bit low-word-first、每word大端。保存失败不发布候选cache（`config_store.c:274–279`），本轮64类别测试实证校准随CALIBRATION编号保留/重置。SH采样 `sh3673510_bms.c:671–673` 与DVC `dvc1124.c:1616` 从同cache应用校准；DVC启动RAM零点残差独立。数学校准和启动zero校准的完整正确性由父审查交叉验证，本子审未把校准字段持久化通过写成电流测量准确。

## 尚未选择或尚无实板证据的策略

1. **[P1安全政策 Unknown] MCU cold reset后的过流/短路锁存归属。** `bms_sw_protection.c:285–301` 仅保留同进程重初始化的锁存，注释明确cold reset RAM从零；State记录 `state_store.c:27–42` 没有保护锁存，Event是历史而非当前安全状态。AFE reset/init、MCU reset、上电须分别定义何时可重开。原厂资料不能代替产品决定是否持久化锁存、boot holdoff、资格恢复或人工授权；父审查本轮reset探针证据单独纳入，不引用历史测试当当前通过。

2. **[P2政策 Unknown] 在线commit断电语义。** `profile.c:503` durable candidate 在apply之前，`storage_record.c:339` commit标记使其成为新有效记录；若在这个点MCU掉电，下一次初始化使用candidate，即使上位机没收到成功或此前effective未完成。journal保障旧/新记录完整性，不能给Flash与AFE提供跨设备原子提交。需要定义“持久candidate即提交”还是“最后成功applied为授权”；若要求后者，增加可追溯pending/applied恢复策略需另行设计及存储兼容评审，本轮不改布局。当前64/357测试验证startup更新，不是这个在线窗口的实板掉电证明。

3. **[P2测量/签核 Unknown] 当前默认及允许校准/disable profile范围的工程批准。** 源码支持的范围并不等于Rsense/BOM、Gate、安全响应、负载、温升等已签核；不根据源码自洽判通过。

4. **[P2介质验证 Unknown] journal擦写、OTA镜像中断/回退与实际数据保存。** 主机字节cut覆盖逻辑部分，需在真实Flash擦除/页编程/提交标记、MCU reset、AFE保持供电等组合下测量。重刷不同更新编号会按“不相等”重新写旧固件默认，非单调防回退；该行为已在OTA文档明确。

## 证据层与重现

本次online probe使用22个四产品实际编译清单中的D014**原源码TU**，未切出/重写函数体；SDK头、SPI peer、Flash、GPIO、clock使用fixture层。开发宏host与`BMS_PRODUCTION_BUILD=1`宏host均复现同样反例。后者显式使用 `BMS_DIAG_BUILD_DIRTY=0` 和匹配当前 HEAD 的非零 buildID，符合原目录 clean 提交身份；**没有分析身份override**。host 仍不是 TC32 production link，不能以此替代父审查的真实构建证据。online编译命令、逐TU SHA在JSON；不编译app main scheduler、真正Flash平台或SDK库；无BIN、无下载、无HIL/实板。

```powershell

$env:AUDIT_PRODUCTION='1'

C:/Tools/Python312/python.exe storage_audit/online_transaction_probe.py

C:/Tools/Python312/python.exe storage_audit/ota_selection_probe.py

```

online脚本报告编译结果和业务断言exit1；脚本本身返回0以收集反例，须读取JSON runtime returncode和断言，不能把脚本exit0称安全通过。OTA测试处于development宏host；source身份没有改变。完整 [source_evidence.json](source_evidence.json) 保存当前源码行号与哈希。
