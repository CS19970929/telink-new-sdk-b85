# TLSR8258 BMS 自检与安全降级框架

## 认证边界

本目录记录基于 IEC 60335-1 / IEC 60730-1 Class B 思路建立的软件安全机制和验证证据。它不是认证证书，也不表示产品已经满足任何标准；最终结论必须由项目方、实验室结合原理图、器件手册、板级故障注入和独立评审给出。

## 设计目标与架构

目标链路为：复位后先关闭 CHG/DSG/PCHG、均衡、CTLC 与 RF_EN，再执行 CPU/控制流自检；板级初始化后验证 Flash、保留 RAM、栈、时钟活动、AFE 配置；正常运行中分时复测并仅在 AFE、保护、MOS 与自检心跳齐全时喂狗。故障统一进入锁存安全态，普通任务不能重新开启危险输出。

模块位于 `vendor/ble_sample/bms_selftest`：

- `bms_selftest.c`：启动/运行时调度和心跳门控。
- `bms_failsafe.c`：安全态、正反码关键变量、复位后故障标记。
- `bms_selftest_cpu_tc32.S`：TC32 通用寄存器、标志和分支/调用返回测试。
- `bms_selftest_flash.c`：Manifest 校验和分块 CRC-32。
- `bms_selftest_ram.c`、`bms_selftest_stack.c`：专用测试区和栈水位保护。
- `bms_selftest_clock.c`、`bms_selftest_interrupt.c`：时基活动与 Timer0 中断监控。
- `bms_selftest_application.c`：ADC 合理性、AFE 通信/配置、MOS 反馈诊断。
- `bms_diag.c`：只读 Modbus 诊断窗口 `0xD1E0`～`0xD1FF`。
- `bms_fault_inject.c`：仅测试构建可用的编译期故障注入。

## 启动与运行时流程

`cstartup_825x.S` 在 BSS 清零、DATA 拷贝后调用 `BMS_SelfTest_EarlyBoot()`；`main.c` 完成时钟初始化后调用 `BMS_SelfTest_BoardInit()`。正常启动在 SH367309 配置和首帧读取后调用 `BMS_SelfTest_Startup(0)`，只有全部门条件通过才允许原应用开启输出。deep-retention 路径预留 `BMS_SelfTest_Startup(1)`，但当前 `PM_DEEPSLEEP_RETENTION_ENABLE=0`。

`BMS_SelfTest_Process()` 每 100 ms 执行短时 CPU/控制流/RAM/栈/时钟/中断/应用诊断，并以小块扫描 Flash。OTA 活动或低功耗场景下保守暂停非紧急完整性扫描；保护与 AFE 任务优先。

## 配置宏

配置见 `bms_selftest_cfg.h`。生产默认 `BMS_FAULT_INJECT_ENABLE=0`；启用注入还必须显式定义 `BMS_DIAG_TEST_BUILD=1`，否则编译报错。故障掩码通过 Make 参数 `BMS_TEST_BUILD=1 BMS_FAULT_INJECT_ENABLE=1 BMS_FAULT_INJECT_MASK=<mask>` 设置，严禁将测试镜像用于产品。

## 构建、CRC 与烧录

在仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_selftest.ps1
```

流程依次 clean build、生成 Manifest、调用 Telink `tl_check_fw2.exe`、校验两套 CRC、检查 ELF/MAP 布局并运行主机测试。最终镜像为 `project/tlsr_tc32/B85/825x_ble_sample/825x_ble_sample.bin`。烧录仍使用工程原有 Telink 下载工具和板卡流程；烧录前必须核对目标板 Flash 容量及 OTA 分区，烧录后必须读回校验。

自定义 CRC 使用标准 reflected CRC-32：多项式 `0xEDB88320`、初值/终值异或 `0xFFFFFFFF`。范围跳过 Telink 可变头 0x00～0x1F 及 64 字节 Manifest 本身；Telink 原生工具继续保护最终镜像格式。

## 测试、故障注入与诊断

主机测试见 `tests/selftest/README.md`。硬件验证必须按 `03_certification_test_plan.md` 和 `04_fault_injection_matrix.md` 执行并将波形、串口、BLE、寄存器读数、固件哈希和实际时间填入报告。

只读诊断窗口返回状态、故障原因、执行/通过/失败位图、CRC、心跳、IRQ 计数和栈余量。致命故障时仍可读，但所有写请求不提供解除安全态或开启 MOS 的能力；日志不在中断中发送。

## 低功耗、OTA 与维护

当前工程关闭 deep-retention，因此只验证了编译与控制流接入，未完成 retention 唤醒板测。OTA 时不对正在更新的区域做运行时扫描；新镜像在后处理和下一次启动时完整验证。修改链接布局、OTA 头、工具链、Flash 分区、启动文件或安全输出极性后，必须重新生成 Manifest、重跑全部测试并更新追踪矩阵。

## 安全注意与已知限制

当前代码无法证明独立时钟源频率，无法覆盖全部应用 RAM，不能替代硬件看门狗超时、ADC 开短路、AFE 断线、MOS 粘连和电源瞬变测试。PD7 `AFE1_PRO_EN` 的安全极性、实际板卡 Flash 容量、复位原因寄存器语义和硬件输出反馈链尚需原理图/芯片资料确认；默认采用“不主动驱动未知引脚、禁止危险输出”的保守策略。完整清单见 `07_residual_risk.md`。
