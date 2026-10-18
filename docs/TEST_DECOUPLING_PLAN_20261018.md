# 测试与源码解耦计划（批次 A）

本文是批次 A 的基线与验收口径：把"按函数名抽取生产源码文本"的夹具逐批改成
"链接真实 TU + 端口替身"，把"断言源码文本"的契约测试改成"断言可观察行为"。
批次 A 不改变任何生产行为；它的价值是让后续 B/C/E 的重构不必每次同步改测试。

## A0 基线（2026-10-18，HEAD `1fe39bca`）

| 指标 | 数量 |
|---|---:|
| 使用 `function()/extract_function()` 抽取**生产源码**的测试文件 | 21 |
| 生产源码抽取调用总数 | 68 |
| 仅抽取夹具自身文本的调用（不构成耦合） | 4 |
| 含 `assertIn/assertNotIn/assertRegex`（多为源码文本断言）的测试文件 | 6 |
| 上述断言总数（含非源码断言） | 328 |
| `tests/fixtures/**/*.c` 夹具 | 28 |

`sh_recovery_counters_host_check.py` 与 `sh_safety_chain_host_check.py` 已用
`selected_source()` 整文件链接，生产抽取为 0，不需要改造；`sh_recovery_host_check.py`
仍有 4 处生产抽取。

抽取最集中的文件：`sh_power_host_check.py`(20)、`dvc_adc_events_host_check.py`(13)、
`d008_power_soc_host_check.py`(6)、`d014_defaults_host_check.py`(6)、
`app_scheduler_host_check.py`(5)、`dvc_ntc_scenarios_host_check.py`(5)、
`dvc_startup_protection_host_check.py`(5)。

文本断言最集中的文件：`d008_configuration_check.py`(116)、
`d008_backend_contract_check.py`(84)、`core_contract_check.py`(63)、
`flash_quick_check.py`(35)。

## A1 第一批（按收益/风险排序）

> 试改结论（2026-10-18）：把 `sh_recovery_host_check` 直接改成整文件注入会与夹具自身
> 定义的类型冲突（`bms_afe_aux_measurements_t` 重复定义），并因生产头 `bms_afe.h`
> 把 SDK 头（`bsp.h` → `compiler.h`）带进主机编译而失败。整文件链接前必须先做两件事：
> 夹具不再重复定义生产类型（改为包含生产头），且主机编译要提供 SDK 头的稳定入口
> （或在夹具里用 `-include` 固定包含顺序）。因此 A1 的实际第一步调整为“消重类型 + 固定包含顺序”，
> 不是直接替换注入文本。

1. **`sh_recovery_host_check.py`**：先让夹具包含生产头而不是自建同名类型，
   再把 4 处生产抽取与 13 个按名抽取函数改成整文件链接；状态区切片随之删除。
2. **`sh_power_host_check.py`**（9 处）与 **`dvc_adc_events_host_check.py`**（13 处）：
   同法处理，分别覆盖 SH 与 DVC 的宿主场景。
3. **`core_contract_check.py` 的 3 条源码形状断言**改为行为断言：
   - `soc_step_down_to(g_soc_runtime.ocv_high)` → 给定高压静置，断言 SOC 不上升；
   - `soc_step_up_to` 不出现在 OCV 函数 → 由行为断言覆盖（同上）；
   - `s_fet_command_valid` 三元组顺序 → 由"重复请求不重写"的行为用例覆盖。

每项独立提交，提交信息记录"删除了哪些抽取/断言、用什么行为替代"。

## A2 第二批

- 去掉夹具里写死的样本数（SOC 已用 `soc_ocv_prepare_ticks()`）；
  同法处理 SH 恢复窗口、DVC 稳定窗口与 `12000/3000` 这类裸数字。
- 目标：改动静置/窗口参数时不再需要同步夹具计数。

## 验收

- 每批：相关 host 检查全部通过；四产品 `sources --check`、`link`、`resources` 通过；
  生产代码零改动（批次 A 只改 tests 与文档）。
- 完成判据：使用 `function()/extract_function()` 的测试文件从 23 降到 ≤10；
  `core_contract_check` 中源码形状断言降到 0；夹具样本数不再依赖裸数字。

## 边界

- 不改生产代码；如必须改，单独说明并另起提交。
- 不删除测试覆盖；只更换断言方式。
- 行为断言必须保留原断言想证明的安全性质（单向 OCV、命令缓存不重写等）。
