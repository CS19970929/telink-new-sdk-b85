# D013 统一 SOC Core（2026-09-21）

D013 已同步 D008 审核后的纯算法 `SocEnhance.c/.h`，但继续使用本分支 SH3673510 采样、保护、IO 和产品配置。没有移植 DVC1124 寄存器、D008 低边 FET 或 D008 电源管理。

主要变化：标准 `bms_soc_sample_t`；有效/无效帧统一推进；库仑主线 + 保守 OCV band；estimate/display 分离；Full/Empty endpoint；TTE/TTF；默认关闭并带完整 reject reason 的容量学习；State schema 3（60 s checkpoint、5 s retry）；Config schema 3；Runtime Diagnostics v3。

本项目处于开发期，schema 变化不迁移旧记录；掉电一致性、独立参数域和错误传播保持由现有 store owner 负责。既有协议 offset、SH3673510 驱动、保护/MOS 与产品 IO 未因统一 SOC 而改变。

TC32 clean build：Flash `109884 B`、RAM `10440 B`，相对 baseline `+7416/+228 B`，0 warnings。该结果不证明实板电流精度、实际 OCV/容量、温度、保护或休眠恢复；仍为 `TODO_VERIFY_HW`。

跨产品 PC 仿真、Windows Record/Replay 和 5000 轮 fuzz 位于 D008/工具真源分支。D013 使用相同 Runtime v3 decoder，不创建第二套上位机协议。
