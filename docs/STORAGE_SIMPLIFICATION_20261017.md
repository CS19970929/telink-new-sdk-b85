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

## 验证证据与限制

基线完整 host 回归执行 128 组，123 通过、5 失败：四产品诊断夹具编译失败及 D011 旧函数签名断言。
四产品生产 `link/resources` 通过，未生成 BIN。基线生产 ELF 投影：D008 16S LFP 112324 B、
D011 110244 B、D013 106868 B、D014 109300 B；这不是实板验证或动态栈水位。

本轮详细命令、失败日志及后续收据在源码树外
`%LOCALAPPDATA%/CodexTemp/bms-storage-20261017/`。每轮结果随实施补充，不把历史结果作为新格式验收。

实板 Flash 掉电、擦写耗时、Gate/Vgs、保护恢复、低功耗电流/唤醒、真实 OTA 和动态栈水位仍需独立验收。
本任务不生成 BIN，不烧录，不执行 OTA，也不将 RAM Flash 故障注入当作物理掉电试验。
