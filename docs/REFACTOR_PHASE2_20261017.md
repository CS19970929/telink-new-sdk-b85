# 第二阶段重构：驱动接口、无效数据与 SOC 重复判断

> 固定提交记录：本文是当时源码与验证的快照，不代表当前 HEAD；当前规则见 [CURRENT_RULES](CURRENT_RULES.md)。文中提交、行数与测试数字只用于追溯。

日期：2026-10-17。工作分支：`codex-bms-deep-simplification-20261016`。
先执行 `git pull --ff-only origin codex-bms-deep-simplification-20261016`，远端基线为
`10372c7ffdf735115e0f27e099d97346894bfefe`。原主目录及其 14 个修改未变动。

## 范围和验收

承接“审核重构结果”对话的第二阶段：减少真实重复操作、裁决状态必要性、降低公共安全层的芯片依赖，
每项给出可核对的收益；保护、采样资格、恢复、低功耗、协议、Flash/OTA 边界保持。
本轮用户另行指定执行相关 host 回归和四产品 sources/link/resources，不生成 BIN。

先迁入对话中已完成的三项，保留独立中文提交：

| 本分支提交 | 原第二阶段提交 | 行为 |
|---|---|---|
| `c9df0f76` | `e62d414c` | SH 已确认 OFF 时只读取一次均衡寄存器 |
| `178614e0` | `de8dc489` | 成功的均衡 set 已确认硬件值，公共层不重复回读；静默总线不能确认 OFF |
| `515c9262` | `8f6ffd25` | 有效 NTC 的 -40°C 编码 0 参与 DVC 温度极值 |

## 实施结果

- `bms_afe_driver.h` 只有同一组直接函数声明。DVC/SH 后端直接实现 `bms_afe_driver_*`，
  仍由产品 sources.txt 选择；没有函数表、运行时注册或新增转发 .c。
- Guard 不再直接调用芯片函数或按 `BMS_AFE_BACKEND` 分发。固定后端 policy 只保存 watchdog 等待 tick、
  原软件保护初始化顺序和休眠后总线保持语义。DVC 5000 ms、SH 35000 ms 仍分别保留。
  DVC Shutdown 和 SH Sleep 经同一公共入口执行，原拒绝条件、错误传播和后续保持语义不变。
- SH 私有诊断类型归入 `sh3673510_diag.h`；DVC 诊断汇总归入现有后端，仍通过 Guard 查询状态。
  普通/量产关闭诊断，协议原因位和总线静默行为保持。
- 删除 `bms_afe_aux_measurements_t` 中五个仅有写入、没有生产读取的字段：两个 NTC mV、
  两个 100-ohm 电阻和重复 pack mV。内部快照由 28 B 变为 12 B；同时删除三个兼容换算函数。
  协议温度/总压来自原 `g_bms_report`，保护/feature 仍使用真实 NTC 有效性和电阻；无效串位仍为 61001。
- SOC 每轮策略只裁定一次方向及 0.1 A 幅值，显式传给压降保持、满/空端点、OCV 和 ETA。
  删除 `isCHG()`；不新增跨轮状态，不改变五种策略顺序、阈值、时间、积分、显示或诊断内容。

SH 正常 OFF 路径由原三次 SPI 回读减为一次；首次读失败仍执行尽力 OFF 并再次确认。
非零写入的必要写后确认、20 秒续期和失败后的 OFF pending 保留。失败分支仍允许额外查询真实硬件状态。
DVC 删除公共层成功后的重复回读；其必要写后确认和硬件均衡超时续期保持。
独立复核发现并修复 DVC 的最终确认缺口：写后验证匹配后，最后一次状态刷新仍可能读到不同掩码。
`dvc_write_balance_hw()` 现在比较这次已有回读与请求；不一致保持失败、真实报告和原续期时间。
新增非零丢位/OFF 反弹回归使用真实 write helper：修改前断言失败（exit 3），修改后通过；没有新增读取。

这些次数由生产函数故障注入中的寄存器 peer 计数确认，不是实板 SPI/I2C 波形或最坏执行时间。

## SOC 和休眠的必要性裁决

| 机制 | 本轮决定及独立作用 |
|---|---|
| 满充端点 | 保留。真实充电和整包电压资格支持唯一向上锚定；满充锁存支持显示到达 100% |
| 强制空电 | 保留。已滤波 UVP 与当前低电压约束；充电恢复时不清掉已充入容量 |
| 放电末端 | 保留。负载电压、容量推导步长和压降保持，与静置 OCV 不同 |
| 静置空电 | 保留。电流、温度、故障、低电压及连续等待确认 |
| 静置 OCV | 保留。斜率/压差/曲线置信上界，仅向下修正 |
| 空电锁存 | 保留。其条件写入关联满充锁存的清除，不能当作纯写入状态删除 |
| 低压、AFE 异常、过放计时 | 保留。资格、清除证据和期限不同；无效采样不能证明电池恢复 |
| 普通 Suspend 门禁 | 保留。采样、UART/总线事务、OTA/Flash 和活动电流各有实际消费者 |
| 强制深睡提交 | 保留。命令/ACC/开关/保护到期独立于普通 Suspend；只收尾一次，不能重新进入业务 |

没有新增低功耗入口，未改变各产品 GPIO、watchdog、采样 timer、唤醒或物理保护恢复。
本轮仅解除 Guard 的具体后端分发依赖；AFE profile、校准、产品 GPIO 与 MCU PM 的具体适配仍存在，
不能据此声称新增 MCU/AFE 已完全不需要修改所有公共模块。

## 可核对的规模变化

仅统计 Git 版本化的 `bms/` C/H 文本，基线 `515c9262` 与本轮终态：文件 117→118
（SH 私有诊断头 +1），物理行 26250→26074（-176），函数式宏 155→127，条件编译入口 740→726。
Guard 单文件 596→488 行，函数式分发宏 28→0，条件编译入口 20→9。
这些是文本统计，包含多产品条件分支，不是预处理后、ELF 存活函数或全部状态数量。
确定删除的是五个辅助成员和四个旧 helper；必要安全长期状态没有按数量指标强行裁剪。

## 验证与资源

基线为迁入前三项后的 clean `515c9262`，终态为本轮源码修改；产品参数、编译优化和源码清单相同。
35 个相关 product/check 组合全部通过：DVC 恢复/NTC/ADC、四产品 feature/诊断、
SH backend+Guard+SW 安全链、SPI/FLAG/物理移除故障注入、SH/D008 PM，及 D014 多 TU 安全闭环。
相关组包含静态契约和生产函数行为测试，不能把组数解释为完整覆盖率。
四产品各自产生的七天 SOC 轨迹均为 10081 行（含表头），前后逐字节一致：
`e040f614d41b8106f4c1d02363c4b8c02b7f937fe68ebb4be8ee60ebb41f3827`。
各产品的 SOC 场景还包含 30 天 Monte Carlo 和 90 天浅循环。
四产品 development sources/link/resources 完成，编译 0 error/0 warning；只产生 ELF/MAP/LST。

| 源码配置 | Flash 基线→终态 B | RAM 地址跨度基线→终态 B |
|---|---:|---:|
| D008 20S LFP custom | 118788→118868 | 22884→22884 |
| D011 10S LFP | 116324→116452 | 22480→22464 |
| D013 10S LFP | 112964→113076 | 22396→22380 |
| D014 8S LFP | 115428→115540 | 22476→22460 |

这轮收益是减少重复运算、无效数据和芯片耦合，**不是 Flash 缩减**：
TC32 -O2 的显式参数和公共固定 policy 增加少量代码。D008 development 余量 8108 B，
比 8 KiB 少 84 B（基线本已少 4 B）；资源工具按当前规则告警，APP 槽未溢出。
RAM 地址跨度含 ramcode/对齐，不是实测峰值栈。

完整机器报告、日志、轨迹和 ELF/MAP 放源码树外；本页保留简明结论，未新增大型机器审计 JSON。
四产品 production ELF 均链接成功、0 error/0 warning；Flash / 余量分别为：
D008 114228 / 12748 B、D011 111732 / 15244 B、D013 108340 / 18636 B、D014 110804 / 16172 B。
全部满足生产 8 KiB 余量。这里只检查当前源码装配，未宣称其它 D008 profile 已验证。
额外验证 `BMS_SERVICE_DIAG_ENABLE=1` 的 D008/D014 development ELF：Flash 为 126244 / 123252 B，
余量 732 / 3724 B，APP 槽内但均低于 8 KiB；仅证明该开发分支可链接，不构成量产镜像。
上述资源已在 clean `3a8984d1b9941279c4dc13c01b0e506428acf33f` 上重新确认：
四开发、四生产及两个服务诊断配置的收据均为该提交、dirty=0、image_generated=false。

35 组合清单与命令以树外证据中的 `host-final/results.json` 为准。最小复现入口包括
`tests/dvc_adc_events_host_check.py`、`tests/d008_power_soc_host_check.py`、
`tests/sh_recovery_host_check.py`、`tests/sh_safety_chain_host_check.py`；以 `BMS_PRODUCT` 选择产品。
规模变化按版本化文件逐个 `splitlines()` 计数；条件统计 `#if/#ifdef/#ifndef/#elif`，不计 `#else`。
正式证据交付到用户 `Documents/CodexOutputs/telink-new-sdk-b85/refactor-phase2-20261017/`。

## 独立复核

按 code-review skill 分别执行两条只读审查：Standards 最终 0 项明确违规/未处理建议，
Spec 最终 0 项未解决发现。两者都发现上述 DVC 最终确认缺口，修复及新增回归后再次复核关闭。
未配置独立 issue-tracker 文件，本轮直接以用户提供的对话要求和本文范围作 Spec 来源，未改造项目 tracker。
未执行完整回归、ASan/UBSan、Cppcheck、BIN、烧录、OTA、实板、功耗或最坏执行时间测试。
物理 Gate/WDT、持续 SC/OCD/OCC、均衡电流/关闭、Flash 掉电与低功耗唤醒仍需实板验收。

原厂边界：DVC1124-2 RM V1.2 原 PDF 第 6、26 页的模式和 I2C watchdog；
SH36735XX CV1.0A 原 PDF 第 18、34 页的 MOS/Sleep 和 WDT。5000/35000 ms 是项目保留的
软件等待余量，不是原厂指定软件值。D008 完整器件后缀/装配与 SH 板级资料缺口见
[AFE 使用指南](AFE_REFERENCE_GUIDE.md) 和 [硬件验证](HARDWARE_VALIDATION.md)。
