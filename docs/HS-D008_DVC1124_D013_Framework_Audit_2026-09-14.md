# HS-D008 / DVC1124 对齐 D013 框架与寄存器配置审计

> 完整审核日期：2026-09-14。为便于 GitHub 在线阅读和后续逐章维护，原始完整 Markdown 报告按章节拆成四卷；内容合并后与本次生成的完整 Markdown 报告一致。当前云端实现状态以本分支最新 commit、`docs/ARCHITECTURE.md` 和 CI 结果为准。

## 完整报告

1. [Part 1：审核基线、结论、硬件基线、D013/D008 框架差异、目标分层与 Product Profile](audit/HS-D008_DVC1124_D013_Framework_Audit_Part1.md)
2. [Part 2：DVC I2C/CRC/地址、逐项寄存器审计、关键寄存器问题、保护量化、采样换算、MOS/Fault Recovery](audit/HS-D008_DVC1124_D013_Framework_Audit_Part2.md)
3. [Part 3：Balance/Open-Wire、Sleep/Wake/WDT、Flash 事务、BLE/UART/Raw、SOC、默认配置与代码迁移清单](audit/HS-D008_DVC1124_D013_Framework_Audit_Part3.md)
4. [Part 4：自动化测试、实板验证矩阵、发布阻断、实施顺序、最终目标架构与附录](audit/HS-D008_DVC1124_D013_Framework_Audit_Part4.md)

## 本轮云端实施

本分支从 D008 `refactor/bms-template-phase1` 创建，继续保留 **HS-D008 + DVC1124-2** 硬件目标。D013 只作为通用 BMS 软件框架与 fail-safe 行为参考；没有把 D011/SH3673510 的 GPIO、SPI 或寄存器配置复制到 D008。

本轮首先落地低风险、证据充分的框架与寄存器修正：compile-time AFE backend、公共 AFE safety guard、通信失效 inhibit、连续 3 帧完整有效 snapshot 后恢复资格、DVC current-latch 恢复去除错误板级 GPIO 假设，以及 HSFM/CAES/INT mask 默认值修正。SCD、DVC I2C WDT、Body Diode 等未完成实板安全验证的功能仍保持显式关闭，不能把该状态解释为量产安全策略。

> DOCX 版是本次审核的排版快照；GitHub 维护以 Markdown 分卷为主，避免二进制文档与源码审计脱节。
