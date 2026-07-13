# 构建与自动测试证据

执行日期：2026-07-13；环境：Windows PowerShell，TC32 GCC `C:\TelinkSDK\opt\tc32\bin`，Make `C:\qp\qtools\bin\make.exe`，Python 3.12。

## 最终生产 clean build

- 一键命令：`powershell -ExecutionPolicy Bypass -File .\scripts\build_selftest.ps1`
- 结果：成功，退出码 0；Manifest `test_build=false`。
- BIN：96,580 B；SHA-256：`759A3EC4C71029EE75E1E5B9EA2C09B913ED3086232083A0E0F5C8F83E0C9274`。
- ELF size：text 91,960 B，data 4,524 B，bss 5,824 B，总计 102,308 B。
- Manifest：偏移 95,820 (`0x1764C`)，镜像 CRC 3,279,475,775，实算一致；Manifest CRC 2,788,418,491。
- Telink 尾部 CRC：3,737,935,463，实算一致。
- 布局：Manifest 64 B、符号同址、应用槽边界、RAM guard 顺序、600 B 栈保留检查全部通过。
- 反汇编：LST 中存在启动调用和 `BMS_SelfTest_EarlyBoot`、`BMS_SelfTest_Tc32CpuRegPatternAsm`、`BMS_SelfTest_Tc32ControlFlowAsm` 的全局符号及指令体。

构建器共报告 81 个 warning，均落在原工程既有源文件/头文件（典型为 `#pragma pack`、未使用变量、逗号表达式、隐式声明）；`vendor/ble_sample/bms_selftest` 新增模块 warning 数为 0，error 数为 0。基线构建已经存在这些类别；本任务未扩张到修复无关历史告警。当前环境未安装 Cppcheck，因此 MISRA/Cppcheck 结论保持“待执行”。

## 自动测试

`python -m unittest discover -s tests/selftest -v`：11/11 通过。覆盖标准 CRC 向量、Manifest/Telink 双 CRC、单比特损坏、Flash/Manifest 边界、MAP 和必需符号、控制流算法/汇编链接、正反码、参数双扇区选择、故障码/安全输出门控以及生产注入编译保护。

## 认证专用构建

执行 `scripts/build_selftest.ps1 -TestBuild -FaultMask 0x20` 成功；测试 BIN 96,500 B，Manifest `test_build=true`，双 CRC 和 11 项主机测试通过。该构建只证明测试开关和 Flash-CRC 注入路径可编译/可识别，未在目标板执行。随后再次 clean build 恢复上述生产镜像。

## 未执行

未执行烧录、实板启动、示波器检测时间、MOS/均衡状态、AFE/ADC 夹具、WDT 真实复位、BLE 并发、OTA 掉电、deep-retention、功耗和独立静态分析。对应条目不得据本报告判为通过。
