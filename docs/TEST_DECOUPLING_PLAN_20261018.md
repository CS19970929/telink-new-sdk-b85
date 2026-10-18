# 测试与源码解耦计划（批次 A）

本文是批次 A 的基线与验收口径：把"按函数名抽取生产源码文本"的夹具逐批改成
"链接真实 TU + 端口替身"，把"断言源码文本"的契约测试改成"断言可观察行为"。
批次 A 不改变任何生产行为；它的价值是让后续 B/C/E 的重构不必每次同步改测试。

## A0 基线（2026-10-18，HEAD `1fe39bca`）

| 指标 | 数量 |
|---|---:|
| 使用 `function()/extract_function()` 抽取生产源码的测试文件 | 23 |
| 抽取调用总数 | 91 |
| 含 `assertIn/assertNotIn/assertRegex`（多为源码文本断言）的测试文件 | 6 |
| 上述断言总数（含非源码断言） | 328 |
| `tests/fixtures/**/*.c` 夹具 | 28 |

抽取最集中的文件：`sh_power_host_check.py`(20)、`dvc_adc_events_host_check.py`(13)、
`d008_power_soc_host_check.py`(6)、`d014_defaults_host_check.py`(6)、
`app_scheduler_host_check.py`(5)、`dvc_ntc_scenarios_host_check.py`(5)、
`dvc_startup_protection_host_check.py`(5)。

文本断言最集中的文件：`d008_configuration_check.py`(116)、
`d008_backend_contract_check.py`(84)、`core_contract_check.py`(63)、
`flash_quick_check.py`(35)。

## A1 第一批（按收益/风险排序）

1. **`sh_recovery_counters_host_check.py` → 链接真实 TU**
   已使用 `selected_source(bms/afe/sh3673510/sh3673510_bms.c)`，只需补齐端口/Flash/时钟替身，
   删除按名抽取的 2 个函数。
2. **`sh_recovery_host_check.py`**：把 13 个按名抽取函数改成整文件链接；状态区切片
   （`#define SH3510_SAMPLE_MS` 到 `restart_sampling`）随之删除。
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
