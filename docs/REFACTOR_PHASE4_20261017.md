# 有效配置、SOC 与协议读取的四项简化

基线为 `b76b8a763dfe0a6141f3fd3b2ad03be0690d62e5`，分支 `codex-bms-deep-simplification-20261016`。本轮按用户授权分四次提交；保留在线 SOC 曲线编辑、放电末端补偿、现有上位机协议和安全行为。四产品 `sources.txt` 仍是编译归属依据。

## 1. DVC 有效保护配置

核心整组查询直接调用 DVC 后端整组回读，不再逐字段经过语义窗口。后端集中解码，旧 `0x2850..0x285D` 字段共用相同规则、保持原读取范围。成功整组读取由 12 次（启用 SC 时 14 次）变为三个连续块：`0x70..0x73`、`0x59..0x5C`、`0x5E..0x61`（SC 时至 `0x63`）。全部通过底层 CRC/有限重试后才填写实读字段；任一块失败不发布部分值，总线静默门禁保持。SH 查询仍只用已验证的缓存，不增加 SPI。

依据为项目原件 DVC1124-2 DS V1.1 PDF 第 23 页（连续读及 CRC），RM V1.2 第 18–20、23–24 页（R/W 配置、阈值、延时）。这三个块不包含 RC 状态；不改寄存器写入、SC/OCD/OCC 恢复、默认参数和 OTA 策略。型号后缀、板级装配与实测 Rsense 的未决项见 [AFE 资料指南](AFE_REFERENCE_GUIDE.md)。

新增 host 场景用原 RM 公式核对全部字节编码、四种 shunt（含零和饱和）、三个块逐次失败、总线静默和 35-word 未覆盖字段保持；三个 D008 profile × HW_PROTECT 0/1 均通过。相关 `dvc_register_scenarios`、`dvc1124_fixed_policy`、`dvc_startup_protection`、`afe_hw_transaction`、D008 `modbus` 共五组既有 host 入口通过；D008 development 目标链接为 0 error/0 warning。最终固定提交与四产品资源结果在完成后追加。

## 2. SOC 已提交曲线只有一个所有者

默认曲线、journal 与算法共用原 100-byte/50-word 配置表示，算法通过只读视图使用存储模块已提交对象，删除另一份运行 profile、15 点转换表及逐成员搬运。stage 候选仍单独保存，写入失败不发布；只读对象地址固定，generation 改变时仍撤销旧 OCV 资格。所有调用在主循环同步执行，不把该指针当作 ISR 并发快照。AUTO、类型不匹配及无可用曲线仍走原默认回退。

整套 Config 已提交后的 SOC 加载入口改为 `bms_soc_reload_configuration()`，直接读取、校验并发布，不再次调用 `bms_config_store_set_soc()`。已确认旧 `bms_soc_configure()` 唯一生产调用者就是该提交后路径；线上参数写入仍经过原 journal/授权事务。

四产品 SOC 长时回归、7 天 10081 行轨迹及每产品三种诊断模式的 91201 行业务轨迹均通过，16 个 CSV 与基线逐字节相同。新增同地址 generation 刷新、自定义曲线插值及读配置失败场景；真实 profile/journal TU 覆盖 LFP/NMC 各 132 个写入字节断点、候选隔离、写失败/重启保持旧曲线、100-byte little-endian payload、未使用点零值、重复保存去重、身份不匹配和读失败禁写。既有语义存储与更新后的源码契约检查通过。

另从固定基线 `git show` 提取旧默认构造与旧曲线数据，与新构造在四产品分别编译，LFP/NMC 全部 100 个 payload 字节完全一致；不只比较新实现自身编码/解码往返。阶段四产品 development 链接均为 0 error/0 warning；最终提交后再执行统一链接收据。

## 3. 参数块读取复用本请求快照

Modbus 先验证完整地址，再把连续业务参数交给 `bms_parameter_read_block()`。请求内按需读取 SOC profile、user、system、aux 和 sleep 各一次，包含读取失败结果；没有全局缓存或跨请求过期状态。协议字直接写入原响应缓冲区，保留普通寄存器与参数混读顺序、非法尾地址无部分读取、`0xFFFF` 未知值及大端字节序。删除唯一生产消费者原来的逐字参数入口；写入、授权、generation、50-word stage 和 commit 未修改。

现有客户端继续每次读 7 word、写 4 word。50-word 曲线一次读取由 50 次 100-byte 复制变为 1 次；7-word 读取由 7 次变为 1 次。SN 16-word 块只读取 user 一次。此项用少量请求内有效标记换取稳定块视图和减少重复复制，不宣称每个模块源码行数都下降。

从 `b76b8a76` 抽取原逐字函数，与新块函数在四产品分别比较全部 8036 个可读子块（正常、user/system/aux 三种失败条件），输出逐字节相同。新增回归检查 50-word 和 7-word 客户端分块、SN/校准/类型/两档休眠、tick 回绕、失效未知值、空指针、数量和地址回绕、非法尾地址无读取；四产品 Modbus、语义存储和源码契约入口通过。

## 4. SH 单轮恢复复用同一份证据

`sh3510_update_protection()` 已为软件保护采集本轮新鲜方向、物理释放许可和 TS1/TS2 温度，现在硬件恢复直接复用这份只读输入；软件保护不会修改该输入或这些测量。短路清除复用本轮充电证据，硬件 FLAG 恢复复用方向/温度；OCD 两级、温度四组分别只换算一次相同恢复延时。没有跨轮缓存或新增状态机。

九个恢复计数仍独立，SC 的 latch、clear_pending、LOADOFF 窗口、下一次实读确认仍保留。`FLAG1` 清除失败仍短路后续 `FLAG2`，失败仍向上进入原 inhibit/静默链。原厂 SH36735XX CV1.0A 第 15、17–18、34 页的温度清除、WDT/MOS 行为和 TS 使能边界保留；本轮不改寄存器编码或物理恢复策略。各板资料、实装及物理验收缺口见 [AFE 资料指南](AFE_REFERENCE_GUIDE.md) 与 [硬件验收](HARDWARE_VALIDATION.md)。

三个 SH 产品的既有 recovery（含 HW 0/1 和完整 BMS/guard O2/Os 场景）、真实 SW 安全链、后端契约，以及 D014 完整安全闭环通过。新增完整选中 SH TU 场景覆盖九组不同恢复窗口并发、独立计数失效、温度无效、缓存/新鲜方向、200/201 mA 死区、清 FLAG1/FLAG2 失败次序及配置读取失败；三个产品均通过。D011 和 D014 集成源码契约已按证据输入位置更新并通过。阶段四产品 ELF 链接为 0 error/0 warning。

## 最终提交与资源实测

四项实现分别提交，不合并压缩：

| 提交 | 内容 |
|---|---|
| `58f31521` | DVC 三块回读与集中解码 |
| `0271e909` | SOC 已提交曲线单一表示与取消重复保存 |
| `98643746` | 参数块按请求复用快照 |
| `73eb22db` | SH 单轮方向、温度和延时复用 |

最终验证源码为干净提交 `73eb22dbf54ed7e654a0ef2a11fb00bb3ce3759e`。四产品 `sources --check`、development 与 production `link/resources` 全部通过，目标编译均为 0 error/0 warning，资源 JSON 均为 `dirty=false`、`image_generated=false`。D008 为当前 CUSTOM 20S LFP；D011/D013 为 10S LFP、D014 为 8S LFP。四产品各自源码清单顺序未改，D008 102、其余各 101 个对象。D008 保持用户上一轮选择的五组 OTA 全保参策略，产品默认、Flash/schema/OTA 边界未改。

以下均为 B，Flash 为 ELF/MAP 投影（包含工具按原规则计算的对齐/CRC），RAM 为地址跨度，不能当作 BIN 大小或实际栈水位：

| 生产配置 | Flash 基线→本轮 | Flash 变化 | 本轮 Flash 余量 | RAM 基线→本轮 |
|---|---:|---:|---:|---:|
| D008 | 112516→112340 | -176 | 14636 | 22856→22772 |
| D011 | 110548→110148 | -400 | 16828 | 22164→22080 |
| D013 | 107156→106756 | -400 | 20220 | 22080→21996 |
| D014 | 109604→109204 | -400 | 17772 | 22160→22076 |

四产品 production 的 8 KiB 余量门禁均通过；RAM 地址跨度各减少 84 B。development 结果也如实记录：D008 Flash `116996→117012`（**+16**），D011 `115220→115076`（-144），D013 `111860→111716`（-144），D014 `114308→114164`（-144）；四者 RAM 均 -84 B。

额外检查 D008/D014 的 service-diag development：D008 Flash `125732→125780`（**+48**）、RAM `23484→23400`；D014 Flash `123348→123236`（-112）、RAM `23044→22960`。它们仍分别仅余 1196/3740 B，保留原先低于 8 KiB 的资源告警，不能视为生产余量合格。四项合计 `bms/` 生产源/头文件净减少 79 行（新增 319、删除 398），不含测试和文档；主要收益是所有者与数据转换减少、DVC I/O 收口及请求内复制减少，不把局部源码增长写成全处减少。

本轮 42 个唯一「产品×相关 host 脚本入口」通过；不同优化模式和场景由脚本内部展开，未冒称全量回归。Standards 与 Spec 两位独立只读复核均为 0 项待处理发现，复核者未代替主任务运行测试。初始 Windows 路径过长的基线链接、夹具编译及旧文本契约错误已纠正；最终通过收据与保留的失败日志分开记录。

正式证据在 [树外证据目录](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/refactor-phase4-20261017-73eb22db/>)：`identity.json`、`host-summary.json`、`target-summary.json`、`summary/resource-comparison.json`、`summary/soc-parity.json`、两个基线 golden 比较、20 份基线/终态资源收据及 ELF/MAP/LST、host 日志、源码差异和 `CHECKSUMS.sha256`。仅克隆 Git 不会获得本机证据包，移交时一并复制并校验哈希。最终验证后的文档补交不改变本轮生产代码，固定验证 SHA 保持上述提交。

## 兼容与验证边界

本轮外部地址、帧/CRC、字节序、缩放、未知值及既有客户端分段方式均保持，现有 Windows/Android 上位机无需配套修改。依据是源码、旧函数输出比对和 host 协议场景；没有运行客户正式 EXE/Android 与设备互通，不能把这些结果写成设备联调已验收。

本轮只执行用户授权的相关 host 回归及四产品 sources/link/resources，不生成 BIN。host 与 ELF/MAP 不能替代实板 I2C/SPI 电气、物理保护恢复、MOS Gate/Vgs、Flash 掉电、低功耗/WDT、OTA 及现有 EXE/Android 与设备联调验收。

未执行全量 host 套件、ASan/UBSan、Cppcheck、其它 D008 profile 的目标链接和实测栈水位；实板、电源切断、低功耗/唤醒、OTA 验收仍待执行。RAM Flash 的逐字节故障注入仅为软件掉电一致性模型，不是真实 Flash 掉电测试。
