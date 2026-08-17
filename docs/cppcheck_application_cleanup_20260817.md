# ble_sample 应用层 Cppcheck 问题清理记录（2026-08-17）

## 目的与边界

本次工作用于认证资料准备，目标是在不修改 Telink 官方 SDK、不添加 Cppcheck suppression、不改变现有业务意图的前提下，清理 `vendor/ble_sample/` 应用层的 Cppcheck 发现项。

- 实际固件构建输入：`bms_tools/source_order.txt`，共 81 个源文件（79 个 C、2 个汇编）。
- Cppcheck 正式分析输入：真实构建输入中位于 `vendor/ble_sample/` 的 18 个 C 翻译单元、21 个应用层头文件。
- SDK 处理：SDK 头文件仅用于解析应用代码，不输出 SDK 诊断；SDK C 文件、汇编、预编译库不纳入 Cppcheck 结果。
- 配置来源：由 `bms_tools/bms.py static` 从真实 TC32 构建命令自动提取 Include Path、宏和 C 标准，不维护另一套手工 `-I/-D` 列表。
- 工具：Cppcheck 2.21.0；目标 C 语言配置为真实构建使用的 `gnu99`。

## 清理结果

清理前应用层共有 126 个去重发现项，主要包括 `variableScope` 37 项、`unusedFunction` 29 项、`knownConditionTrueFalse` 16 项，以及未使用变量、可 const 化参数、恒真/恒假边界判断等。清理后重新扫描结果为 0 项，范围覆盖缺口为 0。

处理原则如下：

1. 删除没有调用点、没有外部入口语义的死函数、调试接口和未启用测试数据；同步删除对应头文件声明。
2. 缩小仅在分支或循环中使用的局部变量作用域，并对只读缓冲区或参数补充 `const`。
3. 删除由无符号类型或编译期常量决定的冗余条件，但保留原有有效分支、计算顺序和返回处理。
4. 对启动汇编/中断向量调用的 `irq_handler` 增加显式只读入口引用，记录其真实外部调用语义，不伪装成普通 C 调用。
5. 保留现有充放电电流整数换算公式；没有启用原先未调用的 64 位替代算法。
6. 未修改 `vendor/common/`、`drivers/B85/`、`common/`、`boot/`、链接脚本、官方库或其他 SDK 内容。
7. 未增加任何 suppression，也未把问题批量标记为误报或自动批准 Deviation。

因此，本轮 126 项均通过应用源码澄清或移除不可达/未使用实现关闭，最终报告中不存在待整改项或 Deviation 候选项。

## 验证证据

- `python bms_tools/bms.py rebuild --jobs 4`：通过；TC32 GCC 4.5.1-tc32-1.3 完成 81 个对象的编译链接并生成 ELF/MAP/LST/raw BIN/官方后处理 BIN。
- `python -m unittest discover -s tests -v`：19/19 通过。
- 应用层 `tests_*.py`：37/37 通过。
- `python bms_tools/bms.py manifest` 后执行 `python bms_tools/bms.py verify`：通过；81 个对象、源码顺序、构建脚本、链接脚本、官方库哈希及 Telink CRC/trailer 均匹配。
- 修改后尺寸：text 86140、data 4524、bss 5248；修改前基线为 text 86124、data 4524、bss 5260。最终 BIN 增加 16 字节，BSS 减少 12 字节。
- 修改前 BIN SHA-256：`c77f74de9d44bd7170a595c407c1509f2ae0ed180d84bb45f4751088b5c8e951`。
- 修改后 BIN SHA-256：`e8e37dd11978c3a0919998bc509f022d385d6d20b498d3d5df1b203c28b43411`。

二进制不是逐字节一致。源级审查和自动测试表明变化来自死代码/死数据清理、恒定分支表达式等价化及静态分析语义澄清；但这些检查不能替代目标板验证。认证或发布前仍应在真实 TLSR8251 板上执行 BLE、Modbus、AFE 采样、充放电控制、休眠唤醒、Flash 参数和 OTA 冒烟/回归测试，并由人工记录结论。

## MISRA 与认证结论边界

本轮执行的是 Cppcheck 原生规则，没有执行 MISRA addon，因而 MISRA Rule 数量为 0，含义是“本次没有执行 MISRA 规则”，不是“完整 MISRA-C 合规”或“所有 MISRA 规则均通过”。认证报告必须保留该限制说明。

自动生成的 Excel 报告可填写工具版本、Git 状态/提交、检查日期、真实配置、分析范围、文件数量、发现项统计、原始证据路径和初步结论。Reviewer、Approver、责任人、批准日期、最终 Deviation 批准和最终签核必须由人工填写。

## 重复执行

在仓库根目录执行：

```powershell
python bms_tools/bms.py static --jobs 4
```

该命令会每次重新读取真实工程配置、重新执行应用层 Cppcheck，并基于当次结果生成新的机器可读证据、CSV/JSON 汇总和一份新的已填写 Excel 报告。`static` 不编译固件，也不生成或更新 BIN；需要固件时另行执行 `python bms_tools/bms.py rebuild --jobs 4`。
