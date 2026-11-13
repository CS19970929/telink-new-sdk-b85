# C060 参数审核修复

2026-11-06，用户授权与 C060 固件审核问题一并实施。

- `NormalizeCurrentGroup` 与固件 `ShortCur_Normalize` 对齐：短路延时原始单位 0.1us，档位 700/1000/2000/4000；阈值比较保留 0.1A 精度，以宽乘法比较，选档后再整除。无法表示的零/溢出比例拒绝；输入数组及其余字段不修改。
- `ProtectionParameter.TryParseEditedValue` 在转换前拒绝 NaN/Infinity，不再抛 OverflowException。
- 唯一跨端样例位于固件 `tests/c060_short_current_vectors.csv`。双方运行同一份230组边界、边界±1、非整数比例和非法配置，避免另复制期望表。

验证：`test-c060-parameter-contract.ps1 -FirmwareRoot <C060源码目录>`、compatibility、572项draft通过；客户WPF/内部WPF/CLI及Android Internal/Customer Compile通过。客户WPF有原有CS0649 `_socRecording` 警告，其余0警告；没有改协议、升级或实际设备。

所有中间文件在用户TEMP，不打APK、不连接/烧录实板。测试中的寄存器有效值仍不能证明实际硬件短路动作门限合格。
