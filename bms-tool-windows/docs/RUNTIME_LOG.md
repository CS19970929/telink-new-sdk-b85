# 运行日志

固件在 `codex-bms-monorepo` 维护；Windows 工具继续在
`feature/windows-afe-hw-protection-editor-v2` 维护，不需要迁入 monorepo。

客户版和内部测试版均新增“运行日志”页。先连接串口或 BLE，再点击“读取运行日志”。
固件需要编译启用 `BMS_DEBUG_LOG_ENABLE=1`，推荐调试使用 `BMS_DEBUG_LOG_LEVEL=4`。
默认和量产关闭，界面会说明关闭或旧固件不支持。

- 可按等级、模块过滤；这是本地过滤，不修改固件参数。
- “每秒采集”默认关闭；启用会增加通信和设备功耗，测低功耗时应停止。
- “停止”取消当前采集并关闭自动采集。“清空显示”仅清界面，不修改设备或倒退 cursor。
- 最多保留最近 2000 条；“导出当前记录”导出保留的全部记录，含未显示的过滤项及原始 event/arg0/arg1。
- “本次缺失”是此次采集期间缺失的事件数；“RAM累计覆盖”是设备环形缓存的总覆盖次数。
- OTA 开始前会取消并等待正在执行的日志采集，5 秒内未退出则此次 OTA 不启动。

固件以 64 条 RAM 环形记录存储，不写 Flash、不主动插入串口文本。
普通 suspend 保留，deep sleep/reset/掉电丢失；最后的深睡前事件可能无法及时取出。
设备计时为原始 32K tick，约 37.3 小时回绕，不是墙钟时间。
启动 tick/build/序号用于识别常见重启，tick 不是唯一会话 ID；怀疑快速重启碰撞时断开重连。
MOS 请求/驱动缓存不等于物理 Gate 反馈。

`Shared/BmsClient.RuntimeLog.cs` 通过现有 `ReadRegistersAsync` 请求锁读取只读 Modbus 窗口
`[0x3000, 0x3290)`，u32 高 word 在前。每批最多 4 条，校验各记录序号与采集前后的会话。
出现覆盖会报告缺失，不把新槽位当旧事件展示。通信错误、取消或会话变化不推进 cursor。
`Shared/MainWindow.RuntimeLog.cs` 两个 WPF 入口共用；CLI 的 Core 也包含客户端 API，但未新增 CLI 命令。
固件事件 schema 的完整定义见 monorepo `docs/RUNTIME_DEBUG_LOG.md`。

验证：客户版、内部版、CLI Release 编译均零错误/零警告。
`test-runtime-log.ps1` 使用真实 BmsClient、CRC 和碎片传输，覆盖首次/增量读取、丢失、覆盖竞争、
序号回绕、重启、旧固件、日志关闭、非法 header 与取消；`test-ota-protocol.ps1` 通过。
测试构建在用户临时区，不写项目临时目录。尚未进行实际串口、BLE、OTA 或 UI 交互验收。
