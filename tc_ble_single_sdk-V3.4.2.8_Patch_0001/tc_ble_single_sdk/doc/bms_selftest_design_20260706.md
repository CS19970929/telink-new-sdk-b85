# BMS 轻量功能安全自检设计说明

日期：2026-07-06

## 目标

在 Telink TLSR825x/TLSR8251 BMS BLE 工程中加入轻量自检框架，用于启动阶段和运行阶段记录 MCU/固件基础健康状态。实现参考 Class B 的分层思想，但不引入 ST/STM32 源码、HAL、SPL 或汇编实现。

当前阶段只负责检测和记录，不直接复位、不 `while(1)`、不关闭 MOS、不改变充放电策略。上层可通过 `BMS_SelfTest_GetStatus()` 或 `BMS_SelfTest_IsHealthy()` 查询结果后自行决定策略。

## 文件

新增文件：

- `vendor/ble_sample/bms_selftest.h`
- `vendor/ble_sample/bms_selftest.c`
- `vendor/ble_sample/bms_selftest_port.h`
- `vendor/ble_sample/bms_selftest_port.c`

修改文件：

- `vendor/ble_sample/app_config.h`
- `vendor/ble_sample/app.c`
- `vendor/ble_sample/main.c`
- `project/tlsr_tc32/B85/825x_ble_sample/vendor/ble_sample/subdir.mk`

## 宏开关

集中放在 `vendor/ble_sample/app_config.h`：

- `BMS_SELFTEST_ENABLE`：总开关，置 0 后接口保留但不执行检测。
- `BMS_SELFTEST_STARTUP_ENABLE`：启动自检开关。
- `BMS_SELFTEST_PERIODIC_ENABLE`：周期自检开关。
- `BMS_SELFTEST_CPU_REG_ENABLE`
- `BMS_SELFTEST_PC_ENABLE`
- `BMS_SELFTEST_CLOCK_ENABLE`
- `BMS_SELFTEST_FLASH_ENABLE`
- `BMS_SELFTEST_RAM_ENABLE`
- `BMS_SELFTEST_ADC_ENABLE`
- `BMS_SELFTEST_INTERRUPT_ENABLE`
- `BMS_SELFTEST_FLASH_ENFORCE`：默认 0。固件 CRC 未通过时只记录非强制状态，不把系统拉入 fail。
- `BMS_SELFTEST_CPU_REG_ASM_ENABLE`：默认 0。tc32 寄存器汇编测试未启用。
- `BMS_SELFTEST_PC_ASM_ENABLE`：默认 0。tc32 PC/跳转汇编测试未启用。
- `BMS_SELFTEST_PERIOD_MS`：周期任务间隔，默认 1000 ms。

## 启动位置

`BMS_SelfTest_Init()` 放在 `user_init_normal()` 的基础硬件初始化末尾、Flash 保护回调注册之后。

`BMS_SelfTest_Startup()` 放在 `adc_init_common()` 和 `app_timer_test_init()` 之后、`bus_mux_init()`/`Runtime_Init()`/MOS 输出开启之前。这样启动自检可使用 ADC 和 timer IRQ 条件，同时不改变后续输出控制流程。

## 周期位置

`BMS_SelfTest_PeriodicTask()` 放在 `main_loop()` 尾部、`blt_pm_proc()` 之前。周期任务按 `BMS_SELFTEST_PERIOD_MS` 节流，Flash/RAM 均为分片执行，不阻塞 BLE 主循环。

PC 检查点目前放在：

- BLE loop 和 `Runtime_Poll()` 之后：`BMS_SELFTEST_PC_CHECKPOINT_LOOP_ENTRY`
- 1 秒 BMS 采样任务之后：`BMS_SELFTEST_PC_CHECKPOINT_BMS_1S`
- 主循环尾部：`BMS_SELFTEST_PC_CHECKPOINT_LOOP_END`

IRQ hook 放在 `irq_handler()` 中，仅递增 `volatile` 计数器。

ADC 观察点放在 `app_adc_multi_sample()` 读取 `ADC_NTC_PIN`、`ADC_NMOS_PIN`、`ADC_VBUS_PIN` 后。

## 自检项状态

| 项目 | 启动自检 | 周期自检 | 当前限制 |
| --- | --- | --- | --- |
| CPU 内部寄存器 | C 级 ALU sanity 检查，tc32 汇编寄存器覆盖为 `UNSUPPORTED` | 同启动 | 未实现真正通用寄存器保存/恢复测试 |
| PC | 固定 C 控制流签名检查 | 检查主循环检查点是否按期出现 | 未实现 tc32 汇编级 PC/LR/跳转覆盖 |
| 内部时钟 | 确认 system tick 在短延时内前进 | 同启动，低功耗状态跳过为 OK | 没有独立时钟源，不能判断频偏 |
| Flash | 调用 SDK `flash_fw_check(0xffffffff)`，默认不强制 fail | 按片读取当前固件并滚动 checksum，结果写入 `flash_last_checksum` | 周期 checksum 当前无外部 golden 值，只能用于在线摘要和读路径覆盖 |
| RAM | 对模块内部 64 word 测试区做 0/1/55/AA 模式检查 | 每周期检查 8 word | 不覆盖全 SRAM 和栈/堆/业务全局变量 |
| ADC | 启动读取 `ADC_VBUS_PIN` raw mV 并检查范围 | 观察三路 raw mV 范围和完全卡死计数 | 不做外部参考源精度校准 |
| 中断 | 启动阶段观察 timer IRQ hook 是否增长，未增长返回 `UNSUPPORTED` | 连续多周期无增长返回 `FAIL` | 依赖当前 timer/IRQ 配置和低功耗状态 |

`UNSUPPORTED` 不置 fail bitmap。只有 `FAIL` 会置位 `startup_fail_bitmap` 或 `periodic_fail_bitmap`。

## 查询方式

可在调试器中查看：

- `g_bms_selftest_irq_counter`
- `BMS_SelfTest_GetStatus()->startup_result[]`
- `BMS_SelfTest_GetStatus()->periodic_result[]`
- `BMS_SelfTest_GetStatus()->startup_fail_bitmap`
- `BMS_SelfTest_GetStatus()->periodic_fail_bitmap`
- `BMS_SelfTest_GetStatus()->flash_fw_crc_checked`
- `BMS_SelfTest_GetStatus()->flash_fw_crc_ok`
- `BMS_SelfTest_GetStatus()->flash_last_checksum`
- `BMS_SelfTest_IsHealthy()`

结果枚举：

- `BMS_SELFTEST_RESULT_NOT_RUN`
- `BMS_SELFTEST_RESULT_OK`
- `BMS_SELFTEST_RESULT_FAIL`
- `BMS_SELFTEST_RESULT_UNSUPPORTED`

## 关闭方式

将 `app_config.h` 中 `BMS_SELFTEST_ENABLE` 置 0 即可保留接口但禁用执行。也可以单独关闭某一项，例如 `BMS_SELFTEST_FLASH_ENABLE` 或 `BMS_SELFTEST_ADC_ENABLE`。

## 后续建议

1. 确认 tc32 ABI 和汇编语法后，补齐 CPU 通用寄存器保存/恢复测试和 PC/branch 覆盖测试。
2. 为 Flash 周期 checksum 引入可信 golden 值或双区版本元数据，避免周期测试只能作为摘要。
3. 如需要更强 SRAM 覆盖，可在启动早期增加 linker 指定测试段，并谨慎避开栈、retention、BLE controller RAM。
4. ADC 建议结合产测基准、电阻分压容差和温度范围，定义项目专属阈值。
5. 后续若要进入 fail-safe 策略，应在独立模块中消费自检结果，不要在自检模块内部直接控制 MOS 或复位。
