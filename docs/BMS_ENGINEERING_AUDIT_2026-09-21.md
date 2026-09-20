# BMS 工程审核与实际修复（2026-09-21）

## 范围与证据

本轮以 `git fetch origin` 后的本地源码、固定 source order、实际 TC32 输出为依据。不是全系统认证，也没有刷板或操作真实 MOS。优先修复能够在主机确定性复现的跨模块问题，没有重组目录、增加动态内存或统一板级驱动。

| 工作对象 | 审核起点 |
|---|---|
| D008 `refactor/d008-common-bms-features` | `2a6b6ab` |
| D011 `refactor/d011-common-bms-features` | `581ead3` |
| D013 `refactor/d013-common-bms-features` | `f41193c` |
| D014 `refactor/d014-common-bms-features` | `851197d` |
| Windows `feature/windows-afe-hw-protection-editor-v2` | `7aae902` |

最新提交已经包含统一 SOC Core、Runtime v3、Windows SOC Record/Replay 与只读诊断会话。因此没有另建上位机或第二套 SOC 算法。

D008 原工作树已有 `app.c`、`d008_product_profile.h`、`todo.md` 修改，全部保留。其中 16S profile 的 cell count 被改为 24，但 profile 名称仍为 16S；本轮没有擅自改回。D008 构建使用独立工作树中的已提交 16S 基线加本轮补丁，不作为这份未提交 24S 配置的验收。审核期间 Windows 工作树出现另一批 SOC 输入录制修改，本轮没有修改或提交这些文件。

## 结论：主要问题在模块边界与验证入口

公共代码已经存在；当前最有价值的工作是验证调用方与公共核心之间的物理量、有效性、状态生命周期契约。相同算法文件不代表接入正确，字符串检查也不能证明状态机能走到恢复出口。

实际归一化行尾比较：四产品 `SocEnhance.c/.h`、`storage_record.c`、`bms_state_store.c` 相同；SH 三产品 `bms_features.c` 相同；`bms_sw_protection.c` 在 D008/D011/D014 相同，D013 仍为另一实现。暂不引入运行时 AFE factory 或跨板 GPIO 抽象。

## 已修复的具体问题

### 1. SH 电流方向与新 SOC Core 相反

SH `publish_measurements()` 把负电流发布为放电、正电流发布为充电；新公共 `soc_current_direction()` 使用负充、正放。三个 SH 分支的 `app_sample_task` 原样传入，导致充放电积分、端点和 ETA 方向错误。D008 不存在该方向错误。

在 `bms_afe.h` 增加很小的编译期单位边界函数 `bms_afe_current_to_soc_ma()`，仅在进入 SOC 时转换；`INT32_MIN` 做饱和处理以避免取负溢出。SH 实际 ADC 换算范围远小于该边界。保留 SH 采样、软件保护和公开实时电流的原有符号。

Runtime word 194 保留原始电流；word 196 修正为实际传给 SOC 的有效电流，供既有 Windows recorder 使用。未移动字段、增加协议版本或修改持久化布局。

新增 `tests/sh3673510_soc_direction_host_check.py`：执行真实转换函数和真实 SOC 方向判断，覆盖正负电流、401 个死区输入、超过 14000 个正负有效输入、失效输入和整数边界，并检查 app/diagnostics 实际接线。

### 2. SH 短路恢复计数每帧被清零

`publish_hw_status()` 每次看到锁存的 SC 就将 `s_short_release_count` 清零，而 `service_short_recovery()` 要求 10 帧连续 LOADOFF。SC 保持置位时恢复计数只能到 1，正常移除负载也无法走完清除流程。

修复为首次锁存时初始化；后续样本保留资格计数。仍然要求连续 LOADOFF、成功 clear、下一帧 SC 消失且 LOADOFF 保持，才解除软件锁存。

### 3. 待确认的 SC clear 跨越负载重新接入

原 pending 分支在 SC 已清而负载重新接入时保留 pending；再次 LOADOFF 可跳过完整稳定窗口。现在 SC 仍置位或负载重新接入都撤销 pending，重新资格确认。

### 4. 恢复计数跨越无效采样

SH 通信失败原先仅重置输出资格，不重置硬件保护恢复计数；休眠也未重置 SC pending/count。现在通信失败、AFE 重配置和休眠丢弃资格计数，但不以此解除故障锁存。

同类检查发现 D008 `dvc_clear_recovered_hw_latches()` 的 COV/CUV 计数也可能跨无效采样继续累积。局部增加有效性与 32k 时间检查：无效样本、不可读 profile 或超过两个采样周期的间隔重启资格，保留原阈值、滤波时长和 readback。正常 tick 回绕不会误复位。

新增 D008 电压恢复 C 测试覆盖各断点、长间隔、tick 回绕、clear/read/profile 失败；既有 D008 电流恢复和 AUTO_DIODE 测试继续通过。

### 5. SH 直接关 MOS / 重配置与命令缓存不一致

`set_output_enabled(0)` 和 `service_afe_reconfiguration()` 直接关 FET，却没有使 `s_fet_command_valid` 失效。之后相同 ON 请求会因命中旧缓存而不下发，诊断也可能继续显示旧命令有效。现在这些路径明确失效缓存，直接 OFF 失败进入通信 inhibit；后续必须满足原有新鲜样本门禁。

新增 `tests/sh3673510_recovery_host_check.py` 和 C fixture，执行原生产函数，注入 SC 持续、负载抖动、clear 失败、clear 后再次 SC、各个断帧位置、休眠、OFF/ON、重配置和写失败。修复前 HW=0/1 分别报告 14/40 项断言失败，修复后均为 0。测试只模拟 MCU 接口，不模拟 AFE 内部电气行为。

### 6. 本地 CI 漏业务测试，旧契约未跟随最近提交

原 `bms.py ci` 只运行 tooling 单元测试与构建/静态分析，未运行业务 `*_check.py`。新 SOC 提交后的 schema、诊断版本、调用入口和算法字段变更使旧契约失效，却没有被该入口发现。

四产品增加 `tests/run_host_regression.py`，在本地 CI 中运行当前产品检查，失败或超时返回非零；Windows 子进程显式使用 UTF-8。SH 的 GitHub host job 也调用同一入口。D013/D014 明确使用自己的产品集成检查，不把遗留 D011 板级检查当成证据；D014 RS485 检查改为本产品引脚名。没有删除有效测试或关闭错误门禁。

### 7. SH HW=0 构建被未使用函数警告阻断

实际隔离模式构建发现硬件恢复函数在 HW=0 下仍编译但无调用，触发最近加入的零 warning 门禁。为这些硬件专属函数增加与调用方相同的编译条件；默认运行行为不变，保留零 warning 门禁。

## 验证结果与资源

| 产品 | Host 回归组 | 正式 BIN bytes | ELF data+bss bytes | `_ram_use_end_` | 扣除 600B 栈预留的地址余量 |
|---|---:|---:|---:|---|---:|
| D008（提交的 16S 基线） | 23 | 121092 | 11480 | `0x8461b8` | 7152 |
| D011 | 16 | 111012 | 10464 | `0x845cc8` | 8416 |
| D013 | 16 | 110228 | 10444 | `0x845db0` | 8184 |
| D014 | 16 | 110692 | 10464 | `0x845cc8` | 8416 |

四产品 `__SRAM_SIZE=0x848000`，实际 startup 为 TLSR8251。data+bss 不是全部 SRAM 占用；地址跨度还包含 RAM code 等，因此另外核对 `_ram_use_end_`。表中余量不是实测栈高水位。

- SW/HW 的 `1/1、1/0、0/1、0/0` 全部完成 TC32 clean build、check-fw、size、MAP、manifest、verify；最后恢复默认 `1/1` 产物。
- SH 三产品四组合全部 0 warning / 0 error。D008 默认/软件关闭为 26 个既有 warning，HW=0 为 35 个；主要来自遗留 BLE/SIF、未使用函数和宏重定义，本轮未扩大清理范围。
- 默认配置 cppcheck：D008 104 style，SH 各 114 style；没有 warning/error，应用编译单元 coverage gap=0。未执行 MISRA，不代表全 SDK 无缺陷。
- D008 还运行真实 Config/State/Event byte-cut 掉电注入、写失败/退避、OTA/session 互斥测试，及 16/20/24S 默认参数与启动门禁测试。
- 既有 SOC 生产 C host 与确定性 simulator/replay 测试通过。四产品 SOC 核心源文件一致；本轮额外覆盖了此前缺少的 SH 输入符号接入。
- Windows 诊断测试首次因 SOC recording 仍断言旧电流来源失败；并行开发更新后再次运行 `test-diagnostics.ps1`，SOC input CSV、诊断会话及协议/ZIP测试通过。独立 `test-ota-protocol.ps1` 通过。这些是本轮观察到的测试结果，不是本轮对上位机的修改，也不代表全部GUI/CLI构建已验收。

完整运行日志在本机 `%LOCALAPPDATA%/CodexTemp/bms-engineering-audit-20260921/`；各分支标准构建目录保留 manifest、MAP、check-fw、cppcheck 和 CI 报告。诊断 Build ID 在构建时指向审核起点并带 DIRTY 标记，不能把它当作 clean release 镜像。正式发布应从提交后的干净 checkout 重建。

## 产品差异及剩余问题

| 产品 | 板级/软件差异 | 仍需关注 |
|---|---|---|
| D008 | DVC1124-2/I2C；提交默认16S LFP；低边 AUTO_DIODE；ACC 独立深睡与 PC4 断电；充电会话由可信电流建立 | 本地24S编辑的 profile 身份一致性；Gate/Vgs、shutdown/供电、COW、WDT 实测；既有 warning 清理 |
| D011 | SH3673510/SPI，10S/250µΩ，RS485，支持 heater；PB5 fuse 必须安全低 | SC/OCD 与 LOADOFF、反向续流、温度/BOM、通信电源和 wake |
| D013 | 源码4S/100µΩ，直接 UART；原理图/BOM 缺失，继承 D011 identity/IO | 不能视为 D011 缩串版；软件保护仍分叉，First/Second 恢复与温度资格语义不同 |
| D014 | SH3673510/SPI，8S/667µΩ，RS485；heater/TS3 禁用，MOS NTC 等待 RN4 BOM | 容量和阈值未签核，TS4/RN4、独立 numeric product ID、硬件恢复 |

### 下阶段优先项

1. **SH 低功耗失败传播**：`sh3673510_control_sleep()` 在 wake active、balance/FET/SPI 写失败时直接返回，返回类型为 void；app 调用 `bms_afe_sleep()` 后仍准备 MCU deep sleep。源码不能证明 AFE sleep 成功。后续应先明确每板失败后的供电/唤醒策略，再让结果可观测并补完整 PM 故障注入，不能只改一个返回值就宣称整机低功耗安全。
2. **D013 保护分叉**：`bms_sw_protection.c` 与另三产品不一致，旧实现把 Recover 用到 First/Second，温度门禁组织也不同。先以生产默认参数和边界向量证明差异，再决定哪些属于产品需求；本轮没有把 D011 的温度方向策略强灌给缺 BOM 的 D013。
3. **Boot Current Zero Calibration**：当前 SH 编译输入/采样路径没有历史分支的启动零点校准调用链，实际直接使用 CADC 换算。不能把 deadband 当校准成功，也不能拿历史分支提交当当前实现。需要真实无流条件、样本新鲜性、漂移/方差、超时和 MOS-safe fallback 的专门闭环；本轮不猜 SH 的校准寄存器。
4. **恢复状态与重初始化**：本轮保留 SC latch，尚需对其它 HW/SW latch 在 AFE reset、MCU reset、WDT 后的产品恢复授权做完整故障矩阵，特别是不能因“0A”推断持续过载已消失。
5. **MCU Self-Test**：当前 source order 没有可独立追踪的完整 MCU self-test owner。SDK checksum/固件校验不能替代 CPU、RAM、运行时 Flash 与 watchdog 自检的周期和失败策略。认证分支应单独审核资源、ISR 与启动影响，不能直接整包移植。
6. **加热/均衡与硬件异常**：已经有输入可信门禁、Open-Wire 互锁、requested/actual 分离；仍需真实断线、温度失效、关断写失败、采样扰动、反向电流和持续空载的板级证据。
7. **长期 SOC/Flash/OTA**：主机证明算法及 journal 不变量，不证明电流温漂、容量、写寿命或无线 OTA 断电恢复。以同板同固件身份绑定的真实记录、参考电量计与定点断电试验补齐。

## 上位机到回归的工作闭环

继续使用工具分支 `BmsTool.Cli`/共享 `BmsClient`：明确 MAC/串口 → 读取身份/Build ID/参数 → 只读诊断会话及原始帧 → 保存完整性、断流和时间边界 → 提取合格输入 → 生产 C replay/故障注入 → 补产品回归 → TC32/check-fw/MAP/static → 指定设备实板复测。

`BmsDiagnosticSession` 已明确保存 `replayReady=false`、`atomicSnapshot=false` 和 reset uncertainty，这是正确的证据边界。稀疏 UI 观测、不同事务的电压/电流、缺少保护/charger/load 字段的数据，不能补成“正常/不存在”后宣称精确 replay。当前 SOC CSV/simulator 的缺省输入处理与观测频率应作为后续工具审核重点；并行增加的输入录制尚未由本轮验收。

复现入口：

```text
python tests/run_host_regression.py
python bms_tools/bms.py ci -j 4
python bms_tools/bms.py static --no-report
```

隔离构建使用 `EXTRA_DEFINES` 指定本产品的 `*_SW_PROTECT_ENABLE` / `*_HW_PROTECT_ENABLE`，每个组合必须 clean rebuild 并核对实际编译命令。非 `1/1` 镜像仅用于台架，不是发布产物。
