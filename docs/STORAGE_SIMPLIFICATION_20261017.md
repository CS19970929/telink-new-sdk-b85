# 三域存储精简与验证记录

## 基线与对话审核

本轮从 `codex-bms-deep-simplification-20261016@405e01e1e7b98c7587c8f28b03adf8a9ac75f7ac`
开始，使用该分支已有且干净的 checkout。原 `codex-remove-openwire` checkout 的既有未提交修改没有带入。
用户授权按《审核提交并给建议》的存储方案优化、取消旧板存储兼容，并要求每轮单独中文提交。

审核接受五个持久域收敛为 Config/State/Event：低频参数一次提交，高频 SOC 状态和事件继续独立轮换。
不照搬对话中的独立分支建议；按用户最新指定的分支工作。外部 Modbus/BLE 地址、缩放、字节序、
事件 ID、100 条历史窗口及 APP/OTA/SDK 边界继续保留。旧编号只读接口有现有客户端，保留其契约，
不因取消旧 Flash 兼容而删除协议地址。

对话的 ACC 保存门禁属于过时文档：基线 `app_prepare_deep_sleep()` 已采用一次尽力保存，
D008 ACC/保护性关机不等待 State/Event 保存成功。本轮不改变该低功耗策略。

## 第一轮：安全启动与接口边界

- Config 只有整区确认为擦除态才可建立首次出厂默认。无有效记录时，损坏、不适配、产品不符及 I/O 错误
  均拒绝启动资格，不自动覆盖为默认。仍有完整旧记录时沿用 journal 最新有效记录恢复。
- journal 的空白判断补查整个区域，防止头部空白但 payload 或扇区尾部残留被当作新板。
- SOC profile 校验在缩窄前检查完整 chemistry；非法默认请求返回清零的无效候选，避免读取未初始化对象。
- 修复基线诊断夹具漏掉 Modbus 只读依赖及 D011 集成检查的旧签名断言；生产诊断和协议行为不变。

第一轮执行四产品存储、四产品 SOC profile、D011 集成共 9 组通过；诊断夹具补齐依赖后四产品 4 组通过。
首次定向回归中的 4 个诊断编译失败日志保留在 `round1/`，修复后结果在 `round1-diag-retry/`。

## 第二轮：Config 统一低频参数

Config 改为 `CFG4` / schema 4 / 414-byte payload：产品 tag、65-word 软件保护、三项系统字段、
35-word AFE 请求、BLE 后缀、业务/校准/SN、一次性 OTA 标记、完整 50-word SOC profile、四项休眠参数。
chemistry/profile 和三个 SOC 算法阈值只持久化一次；固定 AFE 异常休眠时间不写入 Flash。

删除 `bms_soc_profile_store.c/.h`，纯曲线默认、校验、word 映射放入 `bms_soc_profile.c/.h`。
默认曲线只定义一份。`bms_sleep_config.c` 保留纯业务默认/校验/休眠策略。
Config 是两者唯一 cache/journal owner；删除类型切换中的独立曲线提交和回滚写入，硬件应用/读回/
失败 inhibit 与三次新样本资格保留。原两块独立分区释放，APP/OTA/SDK 及 Config/State/Event 主分区不移动。

用户在实施中明确调整旧板政策：有效旧格式应整套恢复默认，而不是仅因格式不同禁止输出。
因此仅识别当前上一版 `CFG2/schema 3/322 bytes` 的完整 CRC/commit 和同产品 tag，不迁移旧字段。
新默认写入成功前保护旧有效 sector；中断后允许凭完整旧记录重试。已提交新格式 CRC 损坏、
旧格式 CRC 损坏、未知格式、异产品和 I/O 失败仍不自动放行。
恢复默认覆盖旧校准、SN、BLE 后缀、保护、曲线、休眠和业务参数，必须采用当前已签核的产品默认。

第二轮已执行：四产品 `sources --check`；四产品存储与 Core contract 共 8 组通过（`round2-upgrade/`）；
D014 联合生产 TU、四产品 Flash/参数块/曲线纯校验等 13 组通过。早期 4 个 Core 旧 schema 断言
已修正并复验，失败收据保留。存储夹具覆盖 10000 组独立 414-byte 编码向量、曲线/休眠完整
448-byte 写入切点、旧格式转换 449 个切点、16 种 OTA 更新组合及参数暂存不写 Flash。

第二轮最终生产四产品 `link/resources` 均通过，包含旧格式转换的追加实现；详细结果在
`round2-upgrade-link.log` 和 `round2-target/`。资源仅为 ELF 投影，不是动态栈验收。

## 第三轮：删除 Event 隐藏重复计数

Event 改为 `EV4 + product ID` / schema 4 / 202 bytes，仅保存写位置及 100 条 ID/时间码。
删除无生产消费者的 100 个重复计数、对应编解码和旧修订字段。60 秒内相同事件仍合并，
但合并不再将已提交事件标记 dirty。协议 ID、时间码、最新在前、100 条窗口不变。
每 4 KiB sector 从 9 槽增加到 17 槽，Event 区继续保留 16 sector；CRC/最终 commit、
最后有效 sector 保护、失败游标及有界重试保留。旧格式历史不迁移。

第三轮四产品语义存储及三产品 SH checkpoint/平台共 7 组通过（`round3/`），覆盖实际时间、
tick 回绕、checkpoint 后合并不写 Flash、236-byte 写入完整切点、失败保留、休眠尝试及协议解码。

## 第四轮：State 单一持久快照及一秒检查点

State 改为 `ST4 + product ID` / schema 4 / 24 bytes，仅保存 SOC、放电累计、cycle 和本域 OTA 标记。
删除旧修订/学习槽及长期 pending 对象；当前值来自 SOC 所有者，失败后的下一次检查重新取得最新业务值。
显式 SOC/循环设置仍先保存成功再发布算法值；休眠仍尽力一次保存，不因失败取消保护性断电/深睡。
统一显式保存为 `bms_state_store_save()`，周期检查为 `bms_state_store_poll()`。
后者与 Event 共用原一秒任务，正常 60 秒 checkpoint / 失败 5 秒退避及 tick 回绕规则保留。
假电流板测只跳过正常 State checkpoint，不跳过 `app_power_process()`。

第四轮四产品语义存储、Flash、Modbus 和 D014 联合生产 TU 共 13 组通过（`round4/`），
调度夹具在适配一秒 State 调用后单独复验 1 组通过（`round4-scheduler-retry/`）。
包含 56-byte State 写入切点、失败后 RAM/已提交值边界、OTA SOC_STATE 一次性重置及周期/休眠路径。

## 验证证据与限制

基线完整 host 回归执行 128 组，123 通过、5 失败：四产品诊断夹具编译失败及 D011 旧函数签名断言。
四产品生产 `link/resources` 通过，未生成 BIN。基线生产 ELF 投影：D008 16S LFP 112324 B、
D011 110244 B、D013 106868 B、D014 109300 B；这不是实板验证或动态栈水位。

本轮详细命令、失败日志及后续收据在源码树外
`%LOCALAPPDATA%/CodexTemp/bms-storage-20261017/`。每轮结果随实施补充，不把历史结果作为新格式验收。

实板 Flash 掉电、擦写耗时、Gate/Vgs、保护恢复、低功耗电流/唤醒、真实 OTA 和动态栈水位仍需独立验收。
本任务不生成 BIN，不烧录，不执行 OTA，也不将 RAM Flash 故障注入当作物理掉电试验。

## 四轮主体软件结果（固定提交）

四轮主体固定到 `92e32d8b511150762c2370f6ad9aef92b0f59242`；以下完整回归、16 组 sanitizer 和首批六配置资源均对应此提交。
随后追加第五轮槽位转换修复，见下节；该修复后的最终目标资源另行记录，不能把首批 ELF 当作追加修复后的产物。

| 轮次 | 提交 | 内容 |
|---|---|---|
| 一 | `b2292785` | 安全启动、整区空白检查与基线夹具修复 |
| 二 | `5b211ce9` | Config 统一曲线/休眠、旧 CFG2 原子默认恢复 |
| 三 | `1243d3ef` | 删除 Event 隐藏重复计数 |
| 四 | `92e32d8b` | 精简 State、统一一秒检查点 |

最终完整 host 执行 128 组：127 组直接通过；1 组 `d014_defaults_host_check` 因夹具仍用旧 system
结构编译失败。改为抽取生产三字段结构，并链接真实 `bms_soc_profile.c` 检查 chemistry/profile 后，
定向 1 组复验通过，原产品/装配/化学体系默认矩阵不减项。
这是“全量结果 + 夹具修复后的定向复验”，不是声称某次完整运行 128/128 全绿。
`final-host/report.md` 保留失败，`final-defaults-retry/report.md` 保留复验，运行期间输入均未改变。

ASan/UBSan 针对存储四产品、纯曲线四产品、SH 存储三产品、Modbus 四产品和 D014 联合生产 TU
共 16 组通过，见 `final-sanitizer-retry/`。使用 Clang 23.1.3（LLVM-MinGW 20261006），
`-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer`；
工具运行对 typedef 重复和既有 unused-but-set-global 提示放宽，不改 TC32 规则。
关闭 LSan；app scheduler 普通 host 已执行，未计入这 16 组 sanitizer。
首次 sanitizer 目录 `final-sanitizer/` 是 CC 路径反斜杠被 shlex 处理导致的编译器启动失败，修正路径后复验；不当作软件断言通过。

四产品 `sources --check` 已执行。六种 production 配置均完成 `link` 和 `resources`，无错误/警告，
资源收据记录干净源码 SHA，且 `image_generated=false`；未执行 BIN 转换、打包、烧录或 OTA。

| 产品 / profile | Flash 投影 B | APP 槽剩余 B | RAM 地址跨度 B |
|---|---:|---:|---:|
| D008 16s-lfp | 110708 | 16268 | 22364 |
| D008 20s-nmc | 110708 | 16268 | 22364 |
| D008 24s-lfp | 110724 | 16252 | 22364 |
| D011 | 108628 | 18348 | 21672 |
| D013 | 105252 | 21724 | 21588 |
| D014 | 107684 | 19292 | 21668 |

APP 槽为 126976 B，最小剩余 16252 B，满足当前至少 8 KiB 门禁。
相对本轮实测基线，D008 16S LFP、D011、D013、D014 各减少 Flash 1616 B；
RAM 地址跨度分别减少 340/344/344/344 B。D008 其他两 profile 这里只列绝对值，不伪造基线比较。
RAM 地址跨度包括 RAM code/cache/IRQ stack；3072 B 主栈只是预留预算，未测动态高水位。
六配置 ELF/MAP 和资源 JSON 在 `final-target/a25269d0c116/`，汇总日志为 `final-link.log`。

## 减法结果与保留理由

- 五个 journal 收敛为三个；曲线与休眠只有 Config 一个持久所有者，取消跨域配置补救写入。
- State payload 44 → 24 B，Event 404 → 202 B；Config 因接管完整曲线及休眠由 322 → 414 B。
- 删除长期 State pending、Event repeats、重复选型/废学习槽；默认曲线集中为一份常量。
- 相对起点生产 C/H 净减 218 行，C/H 文件净少 1 个；没有用改名虚增文件减量。
- 原两块 8 KiB 业务区释放，三主分区和 APP/OTA/SDK 边界保持；释放的 16 KiB 不增加 APP 槽位。
- 旧修订只读接口有现有客户端，保留 `bms_parameter_policy.h` 的外部契约；不再参与参数更新决策。
- 平台全局 Flash 退避、各域 checkpoint/失败预算承担不同职责，保留 CRC/最终 commit/轮换/回读及故障门禁。

外部参数协议 v2、地址、数量、字节序、缩放、100 条事件窗口和 61001 无效串位不变。
旧格式恢复整套默认不等于硬件已允许输出：仍须配置提交成功、原 AFE 初始化/应用/回读及新样本资格。
所有证据是源码、host 故障模型或 TC32 ELF/MAP；物理 Flash 掉电、Gate/Vgs、WDT/低功耗、真实 OTA
和动态栈水位仍未验收。原 D: 主目录的 14 个既有修改没有纳入本轮提交。

## 第五轮：旧格式不同槽长的掉电边界

独立提交 `abef6d48` 修复 CFG2 的 356-byte 槽与 CFG4 的 448-byte 槽网格不一致问题。
原转换只继承旧最新地址，新网格查找可能将旧记录的 FF payload 尾部当作空槽，
在新默认提交前覆盖仍有效的旧数据。已使用固定的修复前生产函数复现旧最新记录字节被修改；
证据在 `final-geometry-red/storage_host_check-d014/old-record-overlap.log`。

现在转换显式从旧最新记录所在 sector 的下一 sector 起写，跨区域尾部回绕也不触碰最新旧 sector。
序号、CRC/commit、物理失败预算及后续失败游标保持；不增加长期兼容缓存。
新增四个 sector 全部 44 个旧槽位置、FF payload、首次写中断及重启重试场景。
原 449 个完整写入切点继续执行。三个域和外部接口均未改变。

修复后四产品存储和 D014 联合生产安全链共 5 组通过（`final-geometry-green/`）；
四产品存储 ASan/UBSan 共 4 组通过（`final-geometry-sanitizer/`）。两次运行期间输入均未改变。
不重复完整 128 组：本轮新增代码仅在识别旧格式后的首次写游标设置中执行，相关语义/断电/安全链已重测。
本次收口还同步当前存储、配置、类型切换、校准和产品文档，以及 AGENTS 的分支/schema 约定；
固定历史审计和原厂资料保留原提交事实。上节资源仍仅代表第四轮固定提交，包含第五轮修复的最终资源以下节为准。

## 最终目标资源与交付

六配置重新从干净 `67df8b0e7b834f4560b5eebc44847c2709876e9b` 链接，包含第五轮旧槽网格修复。
四产品 `sources --check` 再次通过；六配置 `link/resources` 均无错误/警告，资源报告记录
`git.dirty=false`、`image_generated=false`。本节覆盖上节首批资源作为最终交付数据。

| 产品 / profile | Flash 投影 B | APP 槽剩余 B | RAM 地址跨度 B | Flash 相对基线 B | RAM 跨度相对基线 B |
|---|---:|---:|---:|---:|---:|
| D008 16s-lfp | 110756 | 16220 | 22364 | -1568 | -340 |
| D008 20s-nmc | 110772 | 16204 | 22364 | 未测基线 | 未测基线 |
| D008 24s-lfp | 110788 | 16188 | 22364 | 未测基线 | 未测基线 |
| D011 | 108692 | 18284 | 21672 | -1552 | -344 |
| D013 | 105300 | 21676 | 21588 | -1568 | -344 |
| D014 | 107748 | 19228 | 21668 | -1552 | -344 |

最小 APP 槽余量 16188 B；有本轮基线的四产品 Flash 减少 1552～1568 B，
RAM 地址跨度减少 340～344 B。释放的 16 KiB 业务区不计入 APP 槽增量。
完整目标日志为 `final-link-after-geometry.log`，ELF/MAP 与六个资源收据在
`final-target-after-geometry/a25269d0c116/`，均位于本文前述用户临时区。

验收证据合并口径：四轮主体完整 host 128 组中的 127 组通过，1 个默认矩阵夹具修复后定向通过；
主体 ASan/UBSan 16 组通过；第五轮另执行相关 host 5 组、存储 ASan/UBSan 4 组通过。
未把定向复验称为一次全量通过，也未把第五轮以前的 ELF 当作最终产物。
此后的收据提交只改本文，不改变生产源码、参数或构建配置；资源固定提交保留上述 SHA。

五轮实现分别提交，文档/夹具和最终收据另行提交，没有 squash。
指定分支 `codex-bms-deep-simplification-20261016` 使用已有 checkout，未新增 worktree。
原 `D:/telink/` checkout 仍处于 `codex-remove-openwire@cf0f7664`，14 个既有修改未带入。
交付没有 BIN、烧录、OTA 或实板操作；Flash 物理断电和保护/低功耗/栈水位验收仍是开放边界。


## 后续变更：取消 CFG2 读取并新增显式出厂初始化（2026-10-17）

本节晚于上述历史验证固定提交。旧版 CFG2 识别/转换/槽位修复留在历史记录中，
**当前源码已删除对应生产路径**，不代表本节重新执行了旧测试。

新固件使用独立 `factory_reset` 更新位（mask bit 9），默认关闭。出厂包用固定一次性 ID
持久化 CFG4 `pending`，重置 State/Event，再持久化 Config `done`；
失败时不取得输出资格，重启使用同一出厂包继续执行。
普通固件仅空白业务区自动使用新默认；非空白旧格式/损坏配置一律拒绝输出。

有线烧录可擦除完整业务区（512 KiB 型号 `0x40000..0x5FFFF`，1 MiB、2 MiB 见 STORAGE）。
`flash-help` 给出需要手动确认的安全范围，不执行真正硬件擦除。
生产改动、构建工具与新夹具分别由中文提交记录；**本节不声称已运行新的 host/ELF/硬件验证**。


## 出厂恢复评审修复与相关回归（2026-10-17）

基于远端 `f360168c94f756f680126f6347987c21ddbc46a7`，先复现启动夹具四个缺失符号、
旧快照清零执行 ID 导致幂等性断言失败，以及四产品 `SaveOnly factory_reset` 参数绑定失败。
本次只修改工具入口和验证夹具，生产存储事务、schema、分区和寄存器策略不变。

- 启动夹具补齐四个接口替身，保留普通 State/Event 降级检查，并覆盖出厂 State、Event、
  Config 完成提交失败的短路、输出闭锁及重试完成；普通在线提交不能解除启动失败。
- 幂等性测试通过真实 `get_system/set_system` 修改容量，确认执行 ID 保留；原容量/SOC 及
  普通保参镜像重启断言保留。
- PowerShell `ValidateSet` 接受 `factory_reset`；四产品真实 `SaveOnly` 连续保存两次，
  校验独占组、源码摘要、ID 不变和不触发镜像构建。
- 工具回归进一步暴露旧 UI 夹具的五控件假设；同步为六控件并区分两个第 3 行选项，
  保留普通五组保存断言，增加恢复出厂与普通组双向互斥事件检查。

实际结果：相关 host **10/10**，工具单测 **56/56**，四产品开发配置
`sources --check` / `link` / `resources` 全部通过，目标链接 **0 error / 0 warning**。
存储回归包含三种编译布局/版本配置下的 1189 个字节切点、同包幂等、旧格式拒绝与 pending 闭锁。
本次是相关范围回归，不是全量 host 验收；host 完成后仅调整未被这 10 组执行的工具测试期望，
随后重新执行完整工具单测。生产 C 与 host C 夹具自通过后未变。

链接收据基于 `f360168c` 修复工作树（`dirty=1`），未冒充新提交的生产镜像收据。
Standards/Spec 复核均无未关闭问题。日志、修复前失败和输入指纹在
`%USERPROFILE%/Documents/CodexOutputs/telink-new-sdk-b85/factory-reset-review-fixes-20261017/`。
未生成固件 BIN、烧录、执行 OTA 或实板操作；物理 Flash 掉电和输出/低功耗验收仍独立。
