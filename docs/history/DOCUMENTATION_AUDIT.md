> 历史记录：仅代表本文标注的日期和固定提交。当前实现见 [文档导航](../README.md)，当前验证以本轮报告为准。

# 文档审核与交接整理记录

日期：2026-10-05；工作树 `D:/telink/bms-monorepo`；分支 `codex-bms-monorepo`。
核对基线：`afb32d22139ac99b3cb83d47b8a96fe52c9b8ca7`，开始时工作区干净。本次仅修改 Markdown 文档，不改固件、参数值、协议、构建脚本或 CI 行为。

## 审核结论与处理

原文档不适合直接交接：虽有专题和历史验证，但入口、源码位置、存储说明与构建操作存在实质过时内容。

| 发现 | 处理 |
|---|---|
| README 没有覆盖全部文档，缺少首次接手路线 | 根 README 直接链接所有项目 Markdown，补上手与验收；SDK 附带资料独立标注 |
| 构建指南仍用旧 source_order、树内产物和默认 rebuild | 改为产品 sources.txt、外部输出、日常 link/resources，镜像流程独立说明 |
| 配置说明仅偏 D014，缺少“改什么、何时生效” | 增加四产品配置位置、单位、日志练习、容量/保护示例与更新编号 |
| SOC 描述旧 KV/schema、已删除 API、固定 200 ms 积分和旧曲线版本 | 按实际样本接口、gap/死区边界、schema 2、profile v2 重写 |
| 软件文档说 SOC Low 未使用；存储说 event latch 只在落盘后更新 | 按当前 SOC 消费点与 RAM 事件边沿行为修正 |
| 架构漏 D014、feature 路径错误、采样 API 名失效 | 按源码重写调用链、状态所有者与失败边界 |
| 资源说明仍要求跨分支复制、D008 默认 24S、手写 production 宏 | 改为单一源码、16S 开发默认、--production 和 ELF/BIN 分离 |
| D008 有缺失文档/硬件原件链接，旧阶段结论与后续实现混排 | 去掉失效链接与旧“尚未实现”清单，保留板级历史来源和当前电源路径 |
| D014 wire/storage ID 混淆、板级中性宏误称 D011 别名 | 分开 wire ID 2 与内部 tag 14，修正产品入口 |
| 硬件验收只覆盖 D014，CI 说明与六配置 ELF 流程不符 | 补四产品待办，按当前 YAML 说明 CI，标出遗留 workflow 限制 |

## 删除与保留

删除 `SOC_UNIFIED_CORE_2026-09-21.md`、`SH_RECOVERY_SOC_AUDIT_2026-09-21.md`、`BMS_SUBTRACTION_IMPLEMENTATION.md`。它们属于旧分支阶段报告，含已失效的 schema、路径、电流转换位置、函数签名或测试结果；当前有效规则已并入 SOC、架构、低功耗与构建说明。旧内容通过 `git log --all -- docs/<文件名>` 查找，不另建一套活动副本。

保留初次 monorepo 验证和后续整改记录，并明确固定提交边界；不把历史 host 数量、资源大小或远端 run 写成当前已测结果。保留产品硬件事实、协议字段、失败处理和未决项；未删除 SDK Vendor release/patch 文档。

## 验证

在上述基线加本文档修改的工作区验证：

| 检查 | 结果 |
|---|---|
| 根 README 文档覆盖 | 当前 39 份 Markdown，除 README 自身外全部有直接链接；含项目规则、测试说明和 SDK 附带文档 |
| 本地链接/源码导航 | 对项目 Markdown 的相对链接、围栏闭合及主要操作指南的具体源码路径执行检查，无缺失 |
| 四产品 `sources --check` | D008 100 项，SH 各 99 项，通过 |
| Windows 环境自检 | Python 3.12.10、TC32 GCC 4.5.1-tc32-1.3、GNU Make 4.2.1，本机工具/SDK 路径通过 |
| `tests/run_host_regression.py` | 108 组，0 失败；含既有 D008 文档 contract |
| 根 CMake / CTest | GNU host GCC 9.2.0、Debug/Ninja；3/3 通过 |
| D014 开发 `link` / `resources` | 99 个输入，0 error / 0 warning；ELF 预计镜像 118148 B，Flash 余量 8828 B；RAM 跨度 24164 B，主栈预算后余量 5532 B |

D014 构建发生于文档尚未提交的开发工作区（dirty=1），只用于复走上手命令；不是生产模式、最终文档提交的固件身份或实板结果。此次没有重跑其他产品 TC32 链接、远端 CI 或静态分析；文档改动不涉及固件源码和构建配置。

本机日志位于 `%LOCALAPPDATA%/CodexTemp/bms-monorepo/handover-20261005/`：`host/results.json`、`core/Testing/Temporary/LastTest.log`、`build/<checkout-hash>/development/d014/gen/` 和 `documentation-check.json`。这些是临时可复查输出，不是随 Git 分发的发布档案；新接收人按上手指南在自己的机器重跑。

## 尚未完成的外部交接

- 原理图/BOM/手册原件、参数签核人、板号和既有发布档案未包含在当前仓库；历史引用不是本次重新核验原件。
- D013 专属原理图/BOM 尚缺；四产品容量/保护阈值、实板保护/恢复、Flash 掉电、低功耗和 OTA 仍需验收。
- D008 20S NMC 仅切串数/SOC，保护默认未专门配套；SCD 当前默认关闭，不能把 profile 选择当安全签核。
- 旧 `afe-hw-split-ci.yml` 仍有手动入口及旧路径/隐式产品问题；本次只在运维文档标注，不修改 CI 执行行为。
- 本次未生成 BIN、未烧录、未访问真实设备；未把上位机维护分支或远端 CI 重新验证为通过。
