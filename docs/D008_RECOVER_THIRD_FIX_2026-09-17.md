# D008 Recover 仅用于三级保护：启动门禁修复

## 根因与证据边界

用户明确确认：Recover 只用于三级保护。原 `bms_sw_protection` 却对 First/Second/Third 都检查 Recover，并让三层状态机共用恢复值。合法的三级回差因此被错误拒绝。

例如默认充电高温 First/Second/Third = 40/50/55 °C、Recover = 50 °C。它满足三级的 50 < 55，但被原一级 50 >= 40 检查拒绝。16S 默认值有 10 组触发原检查：总压 OV/UV、充放电 OC、充放电 OT/UT、MOS OT、压差。无需改动这些参数值。

实板 `D008_diag_20260917_111835.zip`：512 KiB、boot=0x20000、layout=1；Config/State/Event 各初始化一次成功；program=28、verify failures=0；参数结果 INVALID，参数/升级门禁均关闭。该包不包含软件参数，Build ID 未知，因此不能证明板上每个参数都等于源码默认值。

旧代码使用真实 C 校验器可复现默认值失败。真实 Config/State/Event/Journal/param 与 RAM Flash 组合可复现两次 `BMS_ERROR_EEPROM_STORE`：Config revision 校验失败一次，LoadParam 校验失败一次。硬件 AFE/SOC 参数验证在该隔离测试中为 stub，不声称测到实板 Flash 故障。

## 修正

- Recover 合法性仅相对启用的 Third 检查；三级次序、温度编码范围和错误门禁仍保留。
- First/Second 高向告警在 value >= trip 时进入、value < trip 时解除；低向相反。解除仍经过原滤波，等于阈值时不会交替触发/清除。
- Third 保留原 Recover 回差、恢复滤波和温度故障触发方向资格；电流消失不自动解除已有温度保护。
- 不修改任何默认阈值、Flash 格式、分类 revision、AFE 硬件参数或控制授权。
- 上位机完整诊断新增现有 `0x2100/65 words` 只读采集，ZIP 增加 `software_protection.json`，包含原始 65 words 和地址。读取失败保留其余诊断并写入 Errors；原始请求响应随包保存。无密码/写入帧。

## 回归与实板验收

`python tests/sw_protection_defaults_check.py --series-num 16 --boot-check`（另测 20/24）使用实际 param.h 默认值、实际校验和滤波函数。启动集成使用实际存储模块、LoadParam 和 revision 代码，构造完整有效 journal 记录。修正后预期：errors=0、Config attempts=1/result=OK/defaults=0、param_result=OK、gates=3。

新增告警阈值等号/解除、三级恢复滤波、低向恢复、温度无电流保持、禁用阈值和 uint16 边界测试。CI 接入 16/20/24S 默认值与启动测试，弥补旧存储测试把校验器 stub 成仅非空检查的覆盖缺口。

TODO_VERIFY_HW：更新与当前装配一致的固件后重启，读取新诊断包确认参数结果成功、两项启动门禁通过；如仍失败，用新增 65 words 核对实际参数。检查一级/二级告警与三级实际触发恢复、Gate/Vgs。未自动烧录、未清空 Flash；Host 通过不代表板上故障已经消失。

## 验证完成记录

- 固件功能提交 `bd617648`，Windows 功能提交 `721b0b1`，均为本地提交，未 push。
- 16/20/24S 实际默认值 + 启动链通过；另外构造 Third 恢复值等于触发值的非法记录，仍产生两次错误并保持门禁。普通 SaveParam 修正参数不能解除启动门禁，重启通过升级流程后才恢复有效。
- 14 项 Host/contract、22 项构建工具单测、source-order 通过。当前工作区原有 16S/SW=0 不满足“24S/SW=1 默认产品”静态断言；同提交干净默认配置的 24 项 framework 断言全部通过，未为通过检查改动台架配置。
- 四种 SW/HW 组合及原工作区 16S/SW=0/HW=1 均 clean rebuild/check-fw/MAP/manifest/verify 通过。默认构建诊断 Build ID 为 `bd617648`；台架包含原有脏配置/LED 修改，Build ID 保持 0，用交付 SHA-256 和清单识别。
- cppcheck：31 个 C 单元，coverage gaps=0，0 error/warning、94 style。默认 text/data/bss 为 100984/3996/7056 bytes，和上版诊断固件相同；不是栈高水位或实时性证明。
- 两版 Windows net8/win-x64 自包含 EXE 发布成功，无编译 warning/error；实际 BmsClient 模拟传输的参数采集、原始帧、ZIP、失败保留、超时/取消测试通过。
- 正式交付位于 `outputs/d008-recover-third-bd617648/`，包含两版 EXE、四组合与 16S 台架 BIN/ELF/MAP/manifest、日志和 SHA-256。临时编译输入/缓存均位于用户级 CodexTemp。
