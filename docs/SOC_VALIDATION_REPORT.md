# SOC 重构验证报告（2026-09-21）

## 1. 结论

四个固件分支已经使用同一生产 SOC core；D014 与 D008/D011/D013 同步。生产 C host tests、27 个确定性场景、5000 个 Monte Carlo/fuzz 场景、四个 TC32 clean build，以及 Windows 客户版/内部版/CLI build 均通过。没有实板和外部电量计证据，硬件验收仍为 `TODO_VERIFY_HW`。

## 2. 自动仿真结果

固定 seed `20260921`：

| 项目 | 结果 |
|---|---:|
| 标准场景 | 27/27 pass |
| Monte Carlo/fuzz | 5000/5000 pass |
| 总场景 | 5027 |
| 总生产 C 样本 | 1,764,269 |
| SOC/display 越界 | 0 |
| 容量不变量失败 | 0 |
| 最大未解释 display 跳变 | 1 percentage point |
| 实板 golden trace | 0（待采集） |

代表性数值：标准放电 max/RMS error 为 1.00/0.58%，TTE 平均误差 0.25 min；标准充电为 6.47/1.48%，TTF 平均误差 1.71 min；Ebike max error 1.01%；24 h 150 mA 死区漏电终值误差 3.60%；大倍率 sag max error 1.00%；方向快速切换 max error 0.04%。

`ocv_large_deviation` 故意用 true=45%、Firmware seed=85% 开始。45 min 只发生一次 1% 向下修正，误差 40% -> 39%，这是“10 min qualification 后每 30 min 最多降 1%，且普通 OCV 不向上”的设计结果，不应按快速收敛算法解释。

`capacity_aging` 使用 85 Ah truth、100 Ah nominal，而学习默认关闭，因此最终 full capacity 仍为 100 Ah、capacity error=15 Ah，SOC max/RMS error=16.0/9.29%。这验证了“默认不自动学习”边界；学习的 candidate、一致性、5% 步进、低质量 endpoint、重启/均衡/加热/校准变更拒绝由生产 C contract tests 单独覆盖。

随机场景最大绝对误差不作为精度判据，因为 seed 可故意与 truth 相差并且场景很短；随机 pass 判据是越界、容量、单步跳变和状态结构不变量。绝对精度必须由命名场景和实板 ground truth 评估。

## 3. 构建与资源

| 产品 | baseline Flash/RAM | 本轮 Flash/RAM | 增量 |
|---|---:|---:|---:|
| D008 | 120048 / 11448 B | 120832 / 11504 B | +784 / +56 B |
| D011 | 103376 / 10228 B | 110776 / 10464 B | +7400 / +236 B |
| D013 | 102468 / 10212 B | 109884 / 10440 B | +7416 / +228 B |
| D014 | 103072 / 10228 B | 110472 / 10464 B | +7400 / +236 B |

D008 最终 MAP：`_ram_use_end_=0x8461C0`，保留 600 B stack 后 headroom 7144 B，slot A headroom 5992 B。D011/D013/D014 最终 clean build 均为 0 warnings；四分支 source-order、`tl_check_fw2`、MAP、manifest/CRC/object verify 和 Cppcheck 都通过，CI 状态为 `PASS_WITH_FINDINGS`（应用层历史 style findings 保留评审，`SocEnhance.c` 为 0 finding）。D008 的 26 条编译 warning 来自未修改的 Telink SDK 源；SOC 应用文件没有新增编译 warning。Windows `BmsTool.Windows`、`BmsFactoryTest.Windows` 与 `BmsTool.Cli` Release build 均为 0 warnings/0 errors。

## 4. 已验证的软件不变量

- SOC estimate/display 始终在 0..100；容量非负且 remaining 不超过 effective；
- 无效、重复、gap、reset/restore 不补算未知时长；
- 普通 OCV 只能向下，均衡/加热/故障/source change 清除资格；
- 满电上拉要求充电方向，UVP safety anchor 与 learning endpoint 分离；
- display 平滑变化；保护强锚点导致的大跳变被 endpoint 原因解释；
- TTE/TTF 只在稳定方向输出，taper/波动/静置不伪造有效时间；
- 学习默认关闭；启用后的 accepted capacity 每组最多改变 5%。

## 5. 尚未证明

- D008/D011/D013/D014 实板电流符号、offset/gain、采样周期与 32 kHz 时基误差；
- 指定电芯在多温度下的 OCV 曲线、有效容量、内阻、满/空 cutoff；
- BLE/串口长时间记录完整性、插拔/重连、保护时序、休眠/复电、Flash 掉电；
- 真实均衡/加热/Open-Wire 与 SOC sample 的同步关系；
- Gate/Vgs、MOS 物理状态与板级安全。

实板验收至少要采集 `tests/soc/traces/README.md` 列出的工况，并用独立电量计/充放电台架建立 ground truth。CI/Host/TC32 build 不能关闭这些 `TODO_VERIFY_HW`。
