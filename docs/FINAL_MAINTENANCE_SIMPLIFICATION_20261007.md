# 最后一轮启动与电源职责简化

实施基线为 `cf04da12`，需求来自“完整审核与简化建议”最新对话。按当前[本机验证约定](../AGENTS.md#本机验证约定)，本轮只做源码与差异审查；未运行编译、host、ASan/UBSan、静态分析、资源检查或远端 CI。历史测试结果不代表本轮修改已通过。

## 启动与 CI 选择

`bms_parameters_init()` 仍独立尝试 Config、State、Event，再检查整体启动资格。`parameters_load_protection()` 仅从 Config 缓存读取软件保护；缓存不可用时加载 RAM 默认、报告错误并保持无效，不再次初始化 Config，也不在此写回默认。有效 State 的 SOC/放电累计/循环缓存仍可加载；在线保护提交仍不能清除启动失败门禁。

同步启动夹具和真实 journal/语义存储夹具：Config 硬失败时只尝试一次，首个失败诊断保留，State/Event 仍尝试。夹具已更新但未执行。

CI sanitizer 命令增加 `--only core_contract_check`，其产品归属由 `tests/validation_catalog.py` 定义为四产品。现有选择预期由 37 组增加到 41 组；这只是执行范围修改，实际执行数、结果和耗时需以后运行 CI 确认。上一轮证据中的本机 WSL 41 组与旧远端 workflow 的 37 组分别理解，不修改固定提交历史结果。

本轮没有镜像、烧录、实板、Flash 掉电、Sleep/Wake 或 OTA 验收。
