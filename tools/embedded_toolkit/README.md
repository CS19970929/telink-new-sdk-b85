# Embedded Engineer Toolkit — 第一批可运行工具

当前版本是 40 项路线的第一批交付，入口统一为 Python CLI。生产固件不依赖此目录，`source_order.txt` 不增加任何工具源文件。

## 快速运行

在仓库根目录运行，Python 3.10+；仿真另需本机 C 编译器。Windows 支持自动发现 Visual Studio C++ tools；Linux/macOS 使用 `cc`、`gcc` 或 `clang`，也可设置 `CC`。`HOST_CFLAGS` 用于 sanitizer 等额外编译选项。

```powershell
python -B -m tools.embedded_toolkit --help
python -B -m tools.embedded_toolkit test --samples 20000
python -B -m tools.embedded_toolkit simulate tools/embedded_toolkit/examples/ocd_recovery.json
python -B -m tools.embedded_toolkit hil tools/embedded_toolkit/examples/ocd_recovery.json
python -B -m tools.embedded_toolkit scan . --source-order bms_tools/source_order.txt --source-root tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk
python -B -m tools.embedded_toolkit diff HEAD~1 HEAD
python -B -m unittest discover -s tests -p test_embedded_toolkit.py -v
```

默认报告在用户 Documents/CodexOutputs/embedded-toolkit；通过 `--output` 指定正式证据目录。编译、测试输入和中间文件统一放用户临时区，进程退出后清理。`-B` 防止 Python 在工作树生成缓存。

扫描其他工程：

```text
python -B -m tools.embedded_toolkit scan /path/to/repository --compile-commands /path/to/compile_commands.json --output /path/to/report
```

不提供编译文件表时，扫描结果只是目录清单，`listed_source=null`，不会假定所有 C 文件参与产品构建。当前 Telink source order 有 C 和汇编条目；扫描器只统计列出的 C/C++ 文件。

## 已实现

- 完整编译当前 `bms_sw_protection.c`，覆盖 12 类三级软件保护。
- JSON 场景；YAML 可选，需要安装 PyYAML，使用 `safe_load`。
- 电芯/pack、电流、电池多路温度、MOS 温度、传感器有效性、AFE 阻断、通信失败、请求使能、短路状态、物理释放资格和逻辑重启。
- Fake HIL 共用场景执行和报告入口。
- 阈值、滤波、回差、恢复、参数损坏、多故障、方向、时间回绕和随机属性检查。
- HTML/JUnit/Markdown/JSON 报告，记录源码 commit、dirty、SHA256、编译器和场景 hash。
- 通用 C/C++ 词法架构扫描、include/call/global 候选关系、include cycle、风险候选、GB18030 兼容、源码 SHA256。
- AI_CONTEXT.md / ARCHITECTURE.md / MODULE_INDEX.md / SYMBOL_INDEX.json，以及 Git 文件级 diff。
- Windows/Linux/macOS Actions matrix；Linux Clang ASan/UBSan job。远端 Actions 是否通过需实际运行后确认。

## 行为基线和限制

这还不是已迁移到 MCU 的新 BMS Core。Host 通过自动生成 `param.h` 声明接入现有单实例模块，生产保护实现逐字节复制到外部临时区编译，没有 Python 保护算法副本。当前模块仍有全局状态，单进程一次模拟一个设备，重复 run 会启动独立进程。

软件保护输出对应真实 C；最终 MOS 和短路释放是 **host 保守参考策略**。上电、逻辑重启、通信恢复或传感器恢复后需三个有效样本才允许输出。尚未仿真 SH 寄存器/WDT、真实 MOS、共口反向/body-diode、guard 的总线静默/reinit、AFE rollback。这些不能据此宣称生产恢复安全性已验证。

SOC、Sleep/Wake、真实 Flash 掉电恢复、传输帧 fuzz、真实 HIL、桌面 GUI 和安装包尚未接入第一批入口。逻辑 reboot 仅重置 volatile 状态并保留场景参数，不等同 Flash/EEPROM 仿真。短路 active 和 physical_release 必须由场景或将来的硬件 backend 明确提供，不从零电流推导。

charger_present/load_present 已接受并校验，但当前软件保护不使用这两个检测信号。场景作者必须显式设置 physical_release；检测状态不能自动冒充 AFE 的释放资格。

## 场景格式 v1

`parameters` 每项为 `[First, Second, Third, Recover, Filter_10ms]`，均采用生产参数的原始整数单位：

| 项目 | 原始单位 |
|---|---|
| cell_ov / cell_uv / cell_delta | mV |
| pack_ov / pack_uv | 10mV |
| charge_oc / discharge_oc | 100mA |
| charge_ot/ut / discharge_ot/ut / mos_ot | `(°C+40)*10` |
| Filter | 10ms；内部 ceil 到 200ms 样本 |

测量输入采用 `cells_mv`、`pack_mv`（可选，默认电芯之和）、`current_ma`（正充电/负放电）、`battery_temp_decic`、`mos_temp_decic`。Pack 和电流转换截断到旧单位；小于一个单位的差异不能当成算法阈值变化。不存在的电芯不放入数组，外部协议的 `61001` 槽位必须在接入处过滤。

`steps[].at_ms` 从零开始按 200ms 网格严格递增；空隙自动补当前输入样本，无实时时间等待。0ms 本身是第一个有效样本，因此三个坏样本在 0/200/400ms 触发，不应描述为精确 600ms 物理延迟。旧代码的 leaky debounce 也保留：正常电压/电流样本将累积数减一，未必立即归零。

`expect` 支持 first/second/third、charge_on、discharge_on、temp_break、short_latched、accepted。Fault mask 是工具独立格式，不能当成固件 Modbus bitfield：

```text
bit 0 cell_ov       bit 1 cell_uv
bit 2 pack_ov       bit 3 pack_uv
bit 4 charge_oc     bit 5 discharge_oc
bit 6 charge_ot     bit 7 charge_ut
bit 8 discharge_ot  bit 9 discharge_ut
bit 10 mos_ot       bit 11 cell_delta
```

未配置的保护默认关闭，供独立测试；这不是 D014 默认产品参数。损坏的参数使 host 命令关闭，生产模块/guard 的对应实际行为需要独立验证。时间原点 `tick_origin_ms` 可接近 UINT32_MAX，逻辑时间仍单调，送 C 时转换到 uint32。

退出码：0 通过；1 断言/属性失败；2 输入、编译或工具错误。失败断言也产生全部四种报告。编译失败明确返回错误，不产生虚假的通过报告。

## 扫描器证据边界

词法解析不执行预处理，不解析函数指针调用，不能证明 dead code、栈上界、数组安全或硬件语义。宏函数可能漏识别；extern/global 和同名符号只能提供候选关系，局部同名变量可能产生误报。多个 include 同名候选保持未解析。图示只显示前 60 条 include 边，JSON 保留完整关系。`diff` 当前仅 Git 文件级变化，不宣称已完成函数级风险推导。

完整路线、生产迁移和后续删除条件见 [ROADMAP](../../docs/embedded-toolkit/ROADMAP.md) 和 [当前耦合审计](../../docs/embedded-toolkit/D014_COUPLING_AUDIT.md)。
