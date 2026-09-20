# BMS 上位机与 App 诊断/测试路线

## 统一边界

- `BmsTool.Core` 保存协议、诊断解码、健康评估、OTA 和后续测试用例模型。
- Windows 客户版只开放只读诊断、解释、报告、参数备份和受控 OTA。
- Windows 内部版开放主动测试、Factory Session、校准和原始证据。
- Android 面向现场连接、健康检查、OTA、诊断分享和后台监控，使用独立移动布局。
- CLI 是 AI、本地 CI 和 HIL 的无 UI 入口；JSON 与 exit code 是自动化契约。

任何客户端都不得仅凭重新连接认定 OTA 成功，不得把 AFE driver flag 命名成物理 MOS 反馈，也不得在只读诊断中发送参数或保护写命令。

## 已实现

- 共享 `BmsHealth`：根据诊断支持、快照/Trace 一致性、固件来源、AFE/参数初始化、Storage、采样有效性、三级保护、单体汇总、设备身份和 D008 capability 输出结构化结论。
- Windows“BMS 诊断/健康”页和 Android“设备健康检查”入口共用上述结果；两端诊断页均可直接运行 SOC 自动测试和完整诊断一致性测试并保存 JSON 报告。
- `bms-cli health` 输出机器可解析结果，并可同时保存含 `health.json` 的诊断 ZIP。
- `bms-cli test connection` 真实循环连接、probe、读取身份/Build ID/D008 capability、断开并统计成功率和耗时。
- 共享 `BmsTestEngine` 定义 `PASS / WARNING / FAIL / BLOCKED` 结果、检查项与逐轮证据；Windows 诊断页已可直接运行 10 组 SOC 自动测试或 3 轮完整诊断一致性测试。
- `bms-cli test soc` 检查读取完整性、SOC/SOH 范围、容量关系、Profile、ETA 字段一致性、Build ID 稳定性，并保留每个样本。
- `bms-cli test diag` 支持 quick/full 多轮快照，检查 Snapshot、Trace、Errors 和 Build ID；`--output` 保存 JSON 报告。
- `bms-cli compare before.zip after.zip` 离线逐字段比较身份、启动、存储、实时、SOC、保护、参数、AFE 与健康证据，不需要连接设备。
- `bms-cli parameters get/export` 只读抓取 D008 capability 和各参数块，可独立保存升级前后参数 ZIP。
- `bms-cli monitor soc --reconnect --output monitor.jsonl` 在采样失败后明确释放旧连接、重连并把成功样本与断线空洞一并留证。
- `bms-cli ota --evidence-dir DIR` 自动保存固件 SHA-256、升级前后身份/实时快照、完整诊断 ZIP、参数 ZIP、证据采集状态和最终 OTA 结论。

健康评估只使用当前源码明确提供的状态和一致性约束。未由产品参数定义的电压、压差或温度阈值不在客户端硬编码；是否触发保护以固件 protection 状态和导出的实际参数为准。

## 下一阶段

- 连接 soak 增加 RSSI、连接阶段、GATT status、首帧耗时、断开释放结果和长时间统计。
- Windows/Android 显示保护、MOS、Storage 与事件的统一时间线。
- 将 compare 结果生成面向 OTA/故障复现的 Markdown 报告，并按“身份/配置/运行态”分组过滤动态噪声。

## HIL 与主动测试阶段

- 继续为共享测试用例引擎增加 `SKIP`、前置条件、清理动作和工装原始证据；当前只读测试已经统一 `PASS/WARNING/FAIL/BLOCKED`。
- 内部版受控执行参数持久化、OTA 循环、休眠/唤醒、保护恢复和工厂校准。
- 本地 Windows HIL runner 接入电源、电子负载、万用表等工装；普通 CI 只做 host test/build，默认不自动 OTA。
- D008/D011/D013 按 capability 选择测试，不通过广播名或分支名猜测产品。

## 主动测试安全门禁

主动制造过压、欠压、过流、短路或温度异常前，必须确认目标板、工装、量程、供电、紧急停止、恢复路径和日志目录。软件字段只能证明请求/AFE 状态；物理 Gate、Vgs、电流和动作时间必须由实测证据确认。
