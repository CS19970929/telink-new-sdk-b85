# BMS 自检验证测试方法

日期：2026-07-06

本文档用于 Telink 825x/TLSR8251 BMS BLE 工程的自检验证。验证方法参考 Class B 思想进行自检验证，不表示已经获得 IEC、UL 或其他第三方认证。

本测试不引入 ST、STM32 HAL、SPL 或相关源码。

## 测试目的

1. 验证 BMS 自检模块在量产默认配置下不破坏 BLE、OTA、BMS 主流程。
2. 验证启动自检和周期自检能按预期更新状态结构、fail bitmap 和 last error。
3. 验证每个自检项在正常路径下不误报 FAIL。
4. 验证每个自检项在测试用故障注入宏开启时能稳定触发对应 FAIL。
5. 为 CPU 内部寄存器、PC、Flash、RAM 等难以稳定物理注入的故障提供软件故障注入验证方法。

## 测试范围

覆盖以下自检项：

| 自检项 | 启动自检 | 周期自检 | 故障注入方式 |
| --- | --- | --- | --- |
| CPU 内部寄存器 | 覆盖 | 覆盖 | 软件注入 |
| PC/控制流 | 覆盖 | 覆盖 | 软件注入 |
| 内部时钟 | 覆盖 | 覆盖 | 软件注入 |
| Flash | 覆盖 | 覆盖 | 软件注入；可选 bin 1 bit patch |
| RAM | 覆盖 | 覆盖 | 软件注入 |
| ADC | 覆盖 | 覆盖 | 软件注入；可选硬件边界输入 |
| 中断 | 覆盖 | 覆盖 | 软件注入 |

不覆盖：

- 第三方认证结论。
- 全 SRAM 破坏性 March 测试。
- BLE controller 内部 RAM、OTA 协议栈内部状态。
- 直接 MOS 保护动作，自检模块只记录状态。

## 测试环境

建议环境：

| 项目 | 要求 |
| --- | --- |
| 硬件 | Telink TLSR825x/TLSR8251 BMS 目标板 |
| 固件 | 当前工程生成的 `825x_ble_sample.bin` 或等效产物 |
| 编译工具 | Telink tc32 toolchain，需包含 `tc32-elf-gcc`、`tc32-elf-ld`、`tc32-elf-objdump` |
| 烧录工具 | Telink BDT 或项目当前烧录工具 |
| 调试工具 | 支持查看全局变量和内存的调试器，或串口/Modbus/BLE 诊断通道 |
| BLE 检查 | 手机 BLE 工具或自动化 BLE central |
| OTA 检查 | 当前项目 OTA 流程和 OTA 测试包 |

量产默认配置必须满足：

```c
#define BMS_SELFTEST_FAULT_INJECT_ENABLE 0
```

故障注入测试固件必须与量产固件分开命名、分开归档，测试结束后恢复全部注入宏为 0。

## 启动自检测试方法

1. 确认 `BMS_SELFTEST_ENABLE=1`、`BMS_SELFTEST_STARTUP_ENABLE=1`。
2. 保持 `BMS_SELFTEST_FAULT_INJECT_ENABLE=0`，编译并烧录正常测试固件。
3. 上电或复位目标板。
4. 在 `BMS_SelfTest_Startup()` 执行后读取：
   - `BMS_SelfTest_GetStatus()->startup_done`
   - `BMS_SelfTest_GetStatus()->startup_result[]`
   - `BMS_SelfTest_GetStatus()->startup_fail_bitmap`
   - `BMS_SelfTest_GetStatus()->last_error`
5. 验证 BLE 广播、连接、BMS 采样、基础充放电控制流程仍按原逻辑运行。

预期结果：

- `startup_done == 1`
- 正常路径不出现 `BMS_SELFTEST_RESULT_FAIL`
- `startup_fail_bitmap == 0`
- CPU 寄存器汇编覆盖未实现时允许 `BMS_SELFTEST_RESULT_UNSUPPORTED`
- 中断启动观察在特定时序下未增长时允许 `UNSUPPORTED`，周期自检继续验证中断活动

## 周期自检测试方法

1. 确认 `BMS_SELFTEST_ENABLE=1`、`BMS_SELFTEST_PERIODIC_ENABLE=1`。
2. 确认 `BMS_SELFTEST_PERIOD_MS` 为测试期望值，默认 1000 ms。
3. 正常运行设备至少 5 个周期。
4. 周期读取：
   - `BMS_SelfTest_GetStatus()->periodic_done`
   - `BMS_SelfTest_GetStatus()->periodic_result[]`
   - `BMS_SelfTest_GetStatus()->periodic_fail_bitmap`
   - `BMS_SelfTest_GetStatus()->test_counter`
   - `g_bms_selftest_irq_counter`
   - `BMS_SelfTest_GetStatus()->flash_last_checksum`
5. 同时观察 BLE 连接、OTA 入口、BMS 1 s 采样和 200 ms 任务没有明显阻塞。

预期结果：

- `periodic_done == 1`
- `test_counter` 按周期递增
- `periodic_fail_bitmap == 0`
- Flash/RAM 分片执行，不导致 BLE 断连或主循环长时间卡顿

## 每个自检项的正常路径测试

| 自检项 | 配置 | 操作 | 预期结果 |
| --- | --- | --- | --- |
| CPU 内部寄存器 | 注入关闭 | 上电并运行周期自检 | C ALU sanity 不 FAIL；tc32 汇编寄存器覆盖可为 `UNSUPPORTED` |
| PC/控制流 | 注入关闭 | 正常进入主循环并运行 5 个周期 | 启动 PC 签名 OK；周期 checkpoint OK |
| 内部时钟 | 注入关闭 | 正常运行，非低功耗状态读取结果 | clock tick 前进，结果 OK |
| Flash | 注入关闭 | 正常固件启动并运行至 Flash 分片完成至少 1 轮 | 启动 CRC 记录 `flash_fw_crc_checked=1`；正常固件 `flash_fw_crc_ok=1`；周期 checksum 可更新 |
| RAM | 注入关闭 | 正常启动并运行 8 个周期以上 | 启动 RAM 测试 OK；周期 RAM 分片 OK |
| ADC | 注入关闭 | 运行 BMS 1 s 采样 | ADC raw mV 在范围内，结果 OK |
| 中断 | 注入关闭 | 运行 timer IRQ 和主循环周期任务 | `g_bms_selftest_irq_counter` 增长，周期中断结果 OK |

## 每个自检项的故障注入测试

故障注入只用于测试固件。每次建议只打开一个单项注入宏，便于确认 fail bitmap 和 last error。

通用配置：

```c
#define BMS_SELFTEST_FAULT_INJECT_ENABLE 1
```

单项注入矩阵：

| 自检项 | 单项宏 | 启动预期 | 周期预期 |
| --- | --- | --- | --- |
| CPU 内部寄存器 | `BMS_SELFTEST_INJECT_CPU_REG_FAIL=1` | `startup_result[CPU_REG] == FAIL` | `periodic_result[CPU_REG] == FAIL` |
| PC/控制流 | `BMS_SELFTEST_INJECT_PC_FAIL=1` | `startup_result[PC] == FAIL` | `periodic_result[PC] == FAIL` |
| 内部时钟 | `BMS_SELFTEST_INJECT_CLOCK_FAIL=1` | `startup_result[CLOCK] == FAIL` | `periodic_result[CLOCK] == FAIL` |
| Flash | `BMS_SELFTEST_INJECT_FLASH_FAIL=1` | `startup_result[FLASH] == FAIL` | `periodic_result[FLASH] == FAIL` |
| RAM | `BMS_SELFTEST_INJECT_RAM_FAIL=1` | `startup_result[RAM] == FAIL` | `periodic_result[RAM] == FAIL` |
| ADC | `BMS_SELFTEST_INJECT_ADC_FAIL=1` | `startup_result[ADC] == FAIL` | `periodic_result[ADC] == FAIL` |
| 中断 | `BMS_SELFTEST_INJECT_INTERRUPT_FAIL=1` | `startup_result[INTERRUPT] == FAIL` | `periodic_result[INTERRUPT] == FAIL` |

预期附加结果：

- 对应 bit 在 `startup_fail_bitmap` 或 `periodic_fail_bitmap` 中置位。
- `BMS_SelfTest_IsHealthy()` 返回 0。
- `last_error` 对应当前注入项。
- BLE、OTA、BMS 主流程不应因为自检模块内部逻辑直接复位、死循环或关闭 MOS。

## Flash 1 bit Patch 验证

Flash 自检除软件故障注入外，可通过修改固件区 1 bit 验证 CRC/checksum 检测能力。

方法：

1. 使用正常配置编译生成 bin，并保留原始 bin 作为 baseline。
2. 复制一份 bin 作为 patch 测试文件。
3. 在 patch 测试文件的固件区选择 1 个非 CRC 字节，翻转其中 1 bit。
4. 不重新执行 CRC 生成或 `tl_check_fw` 修复。
5. 烧录 patch 测试文件。
6. 启动后观察：
   - `flash_fw_crc_checked`
   - `flash_fw_crc_ok`
   - `startup_result[FLASH]`
   - `flash_last_checksum`

预期：

- `flash_fw_crc_checked == 1`
- 1 bit patch 后 `flash_fw_crc_ok == 0`
- 量产默认 `BMS_SELFTEST_FLASH_ENFORCE=0` 时，Flash CRC 异常只记录，不强制置启动 FAIL。
- 若要验证启动 Flash FAIL 路径，可在测试固件中临时设置 `BMS_SELFTEST_FLASH_ENFORCE=1`，或使用 `BMS_SELFTEST_INJECT_FLASH_FAIL=1`。
- 周期 `flash_last_checksum` 在完整分片扫描后应与 baseline 固件不同。当前周期 checksum 没有内置 golden 值，需要人工或上位机比对 baseline。

说明：

- CPU 内部寄存器、PC、Flash、RAM 等无法稳定物理注入或物理注入风险较高的故障，采用软件故障注入模拟等效故障。
- patch bin 方式只用于实验验证，不得作为量产包流转。

## 测试步骤

1. 量产默认配置检查：
   - `BMS_SELFTEST_FAULT_INJECT_ENABLE=0`
   - 所有 `BMS_SELFTEST_INJECT_*_FAIL=0`
2. 编译正常固件，烧录目标板。
3. 执行启动自检正常路径测试。
4. 执行周期自检正常路径测试。
5. 验证 BLE 广播、连接、OTA 入口、BMS 采样、Modbus/业务通信按原流程运行。
6. 逐项打开一个故障注入宏，编译测试固件并烧录。
7. 对每个注入项分别记录启动和周期结果。
8. 可选执行 Flash 1 bit patch 测试。
9. 测试完成后恢复所有注入宏为 0，重新编译量产候选固件。
10. 对量产候选固件重复正常路径和 BLE/OTA/BMS smoke test。

## 预期结果

正常路径：

- 启动和周期自检不产生 FAIL。
- `BMS_SelfTest_IsHealthy()` 返回 1。
- Flash CRC 正常固件记录为 checked 且 ok。
- BLE、OTA、BMS 主流程不被自检阻塞或改变。

故障注入路径：

- 打开总注入宏和单项注入宏后，对应自检项稳定返回 FAIL。
- 对应 fail bitmap 置位。
- `last_error` 对应注入项。
- 自检模块只记录失败，不直接复位、死循环或控制 MOS。

## 通过判据

判定通过需同时满足：

1. 量产配置中 `BMS_SELFTEST_FAULT_INJECT_ENABLE == 0`。
2. 所有单项注入宏在量产配置中均为 0。
3. 正常路径启动自检和周期自检无 FAIL。
4. 每个单项故障注入均能稳定触发对应 FAIL。
5. Flash 1 bit patch 能让 CRC 记录显示异常，或在强制测试配置下触发 Flash FAIL。
6. BLE 广播/连接、OTA 流程入口、BMS 采样和主循环任务无回归。
7. 测试记录完整，可追溯固件版本、宏配置、测试人员、日期和结果。

## 测试记录表

| 日期 | 固件版本/Commit | 测试人员 | 测试项 | 宏配置 | 启动结果 | 周期结果 | BLE/OTA/BMS Smoke | 结论 | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  | 正常路径 | 全部注入关闭 |  |  |  |  |  |
|  |  |  | CPU 注入 | `FAULT_INJECT=1, CPU_REG_FAIL=1` |  |  |  |  |  |
|  |  |  | PC 注入 | `FAULT_INJECT=1, PC_FAIL=1` |  |  |  |  |  |
|  |  |  | Clock 注入 | `FAULT_INJECT=1, CLOCK_FAIL=1` |  |  |  |  |  |
|  |  |  | Flash 注入 | `FAULT_INJECT=1, FLASH_FAIL=1` |  |  |  |  |  |
|  |  |  | RAM 注入 | `FAULT_INJECT=1, RAM_FAIL=1` |  |  |  |  |  |
|  |  |  | ADC 注入 | `FAULT_INJECT=1, ADC_FAIL=1` |  |  |  |  |  |
|  |  |  | Interrupt 注入 | `FAULT_INJECT=1, INTERRUPT_FAIL=1` |  |  |  |  |  |
|  |  |  | Flash 1 bit patch | 注入关闭，patch bin |  |  |  |  |  |
