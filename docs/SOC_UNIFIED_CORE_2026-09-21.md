# D014 统一 SOC Core（2026-09-21）

D014 已按用户追加范围同步 D008/D011/D013 的纯算法 `SocEnhance.c/.h`。D014 继续使用本分支 SH3673510 采样、保护、IO 与产品配置，没有复制 D008 的 DVC1124 寄存器、低边 FET 或电源策略。

同步内容：

- 标准 `bms_soc_sample_t`，有效/无效帧都进入核心，检查重复时标、gap 和 source change；
- 库仑积分主线、OCV band 只向下纠偏、estimate/display 分离、Full/Empty endpoint、TTE/TTF；
- 默认关闭的容量学习，以及均衡、加热、charger/load 变化、Open-Wire、温度/保护/AFE/校准/reboot reject reason；
- State schema 3，约 60 s checkpoint、保存失败 5 s retry；
- Config schema 3；开发期不迁移旧 schema；
- Runtime Diagnostics v3，供现有 Windows 双版本与 CLI 的唯一 typed decoder 使用。

TC32 clean build：Flash `110472 B`、RAM `10464 B`，相对 baseline `+7400/+236 B`，0 warnings。软件 build 不能证明 D014 实板 SH3673510 电流、OCV、容量、温度、保护、休眠恢复或板级安全；仍为 `TODO_VERIFY_HW`。

跨产品 PC 仿真、Windows Record/Replay 和 5000 轮 fuzz 位于 D008/工具真源分支；D014 不另造通信栈或 Host SOC 实现。
