# SH3673510 恢复证据与采样资格

本轮基于 `c9dedf349ddcadbdee61ea0565acd6418cc0ed9c`，覆盖 D011 / D013 / D014 共用 SH 后端以及公共 guard 的 SH 分支。目标是补齐已经复现的硬件保护恢复和采样资格缺口，不搬迁目录、不新增框架，不调整协议、产品参数、Flash 布局或 D008 后端行为。

## 行为与状态所有者

调用链仍为 `app_sample_task()` → `bms_afe_sample()` → `sh3673510_bms_afe_sample()`。软件三级保护、AFE hardware profile、产品输出请求、实际驱动状态继续独立。

| 所有者 | 本轮职责 |
|---|---|
| `sh3673510_control.c` | 对 SCONF3 的 CRLD_EN 做读改写和读回验证；报告是否实际改变，保留 wake / open-wire 等其他位 |
| `sh3673510_bms.c` | 独占检测模式、等待时间、拔除证据、ADC 资格和硬件 flag 恢复；充电器查询仅返回缓存，不再另行读 SPI |
| `bms_afe_guard.c` | 维持三次新样本资格、输出 inhibit、两次失效后的 watchdog 静默和有界重初始化；正常等待转换不计成通信失败 |

所有新增状态均为模块内 `static`，由主循环访问；没有新增 ISR 状态、动态分配、callback、存储字段或协议地址。

## 硬件恢复

- OCC 同时要求电流低于恢复值与实际量化触发值，以及有效 C+ 拔除证据或 AFE 的 `DSGING` 反向状态。关 CHG 后电流为零不再单独允许清除。
- SC / OCD 优先选择 `CRLD_EN=10` 负载检测；其他阶段选择 `01` C+ ADC。二者互斥。每次实际切换、修正寄存器或重新建立检测状态后，至少等待 200 ms，再使用新模式下取得的状态。
- `LOADOFF` 只在负载检测已稳定、且没有同时报告 `LOADON` 时生效。SC 仍需 10 次稳定观察和清除后的下一次读回确认；OCD 仍同时检查电流与恢复滤波。
- C+ 只在对应模式已稳定且本次有 `VADC_FLG` 时采集。负数、切换期间、未建立状态的迟滞区均不是拔除证据。保留原有 900 / 2100 mV 板级迟滞策略；恢复用本次明确的低电压证据，不用“缓存 present 为 0”代替。
- SC 和 OCC 同时存在时先完成负载恢复和 SC 读回，再切回 C+，重新等待后取得充电侧证据。模式间不复用旧证据。
- 清 FLAG1 / FLAG2 的失败向上传递；清除事务结果不确定会锁定采样失效，直至完整初始化。下一轮不再用普通 ADC 读成功覆盖此错误，guard 可进入既有 35 s 静默窗口。实际 MOS 关断仍依赖硬件验证。

硬件恢复计数仅在新的电流/电压证据齐全时推进，正常等待不累计；发现重新连接会立即丢弃过流恢复窗口。因此配置的恢复时间可能延长，不能将样本计数称为精确墙钟定时。

## 采样有效性

先读具有 read-clear 副作用的 FLAG2，再读测量寄存器。只有 `CADC_FLG` 出现时更新电流与 `sample_tick_32k`；电压/温度报告只在 `VADC_FLG` 出现时更新。整组读取和 cell 范围检查通过后才发布测量。

200 ms 主循环比 normal-mode 250 ms CADC 周期快，因此区分：

1. **新测量**：电压和电流的本次完成标志均存在，可推进 guard 资格。
2. **等待转换**：成功读取但没有新的完整测量；已有快照最多保留 400 ms，电流时间戳不变。软件保护与 feature 的 200 ms 调用节奏保留，使用有界缓存；SOC 能识别重复时间戳。
3. **失效**：任一 ADC 超过 400 ms 没有完成标志、读取失败、非法 cell 或清 flag 失败；快照无效，进入既有 fail-safe 链。

初始化、AFE reset 重配置及 sleep/wake 后，先等待至少 1.2 s 的 normal-mode 温度扫描启动窗口，再接受完整新测量和三次资格。该等待不会阻塞主循环。时限使用无符号 tick 差处理回绕。

`sample_tick_32k` 表示 MCU 观察到新 CADC 数据的时刻，不是芯片内部精确转换时刻。器件没有本代码可用的逐温度通道完成标志；启动等待与 VADC 活性约束不能证明每次读取都得到新温度转换，也不能诊断独立温度转换逻辑冻结。

## 资料依据与边界

核对的器件资料为本机原厂手册 `S03_SH36735XX_V1.0A.pdf`：§7.5 的锁存清除，§7.9～7.10 的 watchdog/MOS 状态，§7.13～7.14 的 VADC/CADC/温度周期，§8.5 / §8.7 的负载/C+ 检测，SCONF3、FLAG2 寄存器以及 tLOAD 电气特性。原件未复制到仓库；位置为 `C:/Users/Administrator/Downloads/通用BMS工程包_v0.2.0/Universal_BMS_v0.2.0/reference/manuals/`。

手册的 0.9～2.1 V 是 sleep/powerdown 充电唤醒比较器范围，不能据此声称 normal-mode C+ ADC 拔除阈值已获原厂保证。900 / 2100 mV 是沿用代码策略，必须在各板型、充电器残压、反灌和重新插接条件下签核。200 ms 检测等待及 1.2 s 启动等待也需要实测裕量。

本轮处理 **SH 硬件 flag 恢复**。公共 `bms_sw_protection.c` 的软件过流 Third 恢复仍按电流阈值/滤波执行，尚未接入独立的充电器/负载移除证据；不能把本轮结果描述为整个 BMS 的 SW→AFE→MOS→负载恢复已经闭环。后续应先确定四产品的软件过流恢复输入和 DVC 对应证据，再做该边界的独立修改与回归。架构文件整理、SOC 状态整理也不与本轮行为修复混在一起。

## 软件验证

新增 `tests/sh_recovery_evidence_host_check.py`：以真实选中产品的完整 SH BMS 单元、真实公共 guard 和真实 CRLD 寄存器控制函数分别验证公开入口。设备寄存器、时间、独立软件保护和 feature 策略由 host fixture 模拟；不声称覆盖真实整机。

覆盖 OCC 零电流误恢复、迟滞/负 ADC、检测模式切换、SC/OCC 并发、清除后读回、清除失败静默、ADC 无新数据/超时、正常 4 Hz CADC 节奏、tick 回绕、寄存器其他位保持及每个模式读写/读回失败。D011 / D013 / D014 分别用 host `-O2` / `-Os` 执行。

`sh_sample_atomic_host_check.py` 改为复用上述完整单元，继续覆盖逐次读取失败、全部有效 cell 的越界值、NTC 无效、reset 后失效、无 SPI 的快照 getter 和 61001 缺省串位。原短路、低功耗及产品 contract 测试保留并同步新返回值。

实现工作树已通过 115 组 host 回归，四产品 sources 顺序检查，以及四产品开发配置 TC32 ELF/MAP 链接（零错误、零警告）和资源检查。D014 Cppcheck 检查实际编译库中的 36 个应用翻译单元，仅有 103 个 style 项，无 error/warning/performance 项；SDK 仅作依赖解析，两个非 D014 的头文件覆盖缺口保留，未执行 MISRA。

开发配置的 Flash 余量分别为 D008 3084 B、D011 7180 B、D013 8188 B、D014 7324 B，均低于生产要求的 8 KiB，因此开发配置链接通过不等于生产资源门禁通过。

固定干净代码提交：`4d7f5268e4a7491374a5ba37d2fe06e124f2fc44`。执行 `python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp link --jobs 4`，四套生产 ELF/MAP 均为零错误、零警告，自动资源门禁全部通过；各 resources 收据均为上述 SHA、`dirty=false`、`image_generated=false`。后续文档证据提交不改变代码。Host 和开发构建在提交前的同一实现上执行，收据按当时工作树状态保留。

| 生产配置 | ELF 投影 Flash / B | Flash 余量 / B | RAM 地址跨度 / B | 相比修改前 Flash / RAM |
|---|---:|---:|---:|---:|
| D008 16s-lfp | 118516 | 8460 | 25000 | 0 / 0 |
| D011 | 114612 | 12364 | 24196 | +1168 / +28 |
| D013 | 113604 | 13372 | 24176 | +1168 / +28 |
| D014 | 114452 | 12524 | 24196 | +1168 / +28 |

对比基线为修改前 `c9dedf34` 的代码，其 BMS 源码与既有 `bb388fdb` 资源记录相同。Flash 是 ELF 加预期对齐/CRC 的投影，RAM 是地址跨度，不是 BIN 大小或实测栈水位。这轮修复增加了必要状态和边界检查，不能称为代码体积优化；D008 仍仅比生产余量门槛多 268 B。本次没有重复验证 D008 的其他生产 profile。

完整 host 日志、开发/生产 ELF/MAP、输入收据、资源 JSON 和 D014 静态结果归档于 `C:/Users/Administrator/Documents/CodexOutputs/bms-monorepo/sh-recovery-freshness-20261005/`，汇总为其中的 `evidence.json`。四产品源清单数量和顺序保持不变：D008 100 个对象，三个 SH 产品均为 99 个。

没有请求或生成 BIN，没有烧录、OTA 或实板操作；发布阻断项仍见 [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md)。
