# ble_sample BMS MCU 自检认证实现报告

日期：2026-07-02
工程：`tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample`
需求：SSR-MCU-001、SSR-MCU-002

## 1. 实现范围

本次在 B85 `ble_sample` BMS 工程中增加 MCU 安全自检：

- 启动期自检：位于 `boot/B85/cstartup_825x.S`，由 `825x_ble_sample/boot/B85/subdir.mk` 传入 `-DBMS_MCU_STARTUP_SELFTEST_ENABLE=1` 后启用。执行点在 Flash wakeup 之后、`.bss` 清零和 `.data` 拷贝之前。
- 运行期自检：位于 `vendor/ble_sample/bms_mcu_selftest.c/.h`，在 `vendor/ble_sample/main.c` 的 `while (1)` 每轮进入 `main_loop()` 前执行。
- 失败处理：启动期失败进入 `bms_startup_selftest_fail_safe_loop`；运行期失败进入 `bms_mcu_selftest_fail_safe_loop(reason)`。

## 2. SSR-MCU-001 启动期自检

启动期顺序与需求一致：

1. CPU 内部寄存器：对 R0-R7 写入 `0xAAAAAAAA/0x55555555` 交叉搬移、比较，并检查比较标志位跳转。
2. 程序计数寄存器：通过强制分支路径和递增签名验证 PC 控制流是否按预期到达。
3. RAM 寄存器：使用 `SRAM_SIZE - 16` scratch 地址写入/读回 `0xAAAAAAAA/0x55555555`。
4. 内部 Flash：读取向量区 `0x00000008` 的 Telink magic `0x544C4E4B`。
5. 时钟：读取 system tick，延时后再次读取，若 tick 未变化则失败。

任一项失败后，启动汇编不会进入 C 初始化阶段，直接：

- PA/PB/PD 组输出拉低，覆盖 `MCC_C_PIN`、`AFE_CTL_PIN`、`ADC_BUSEN_PIN`、`ADC_EN_PIN`、`RF_EN_PIN`、`AFE1_PRO_EN_PIN` 所在组；
- PB4 输出周期性脉冲作为启动期故障报警；
- 死循环保持失败态。

## 3. SSR-MCU-002 运行期自检

运行期每次主循环执行顺序为：

1. CPU 内部寄存器：调用 `bms_mcu_selftest_cpu_regs_asm()`。
2. 程序计数寄存器：调用 `bms_mcu_selftest_pc_asm()`。
3. 内部时钟：检查 `clock_get_system_clk() == SYS_CLK_TYPE`，并确认 `clock_time()` 递增。
4. 内部 Flash：检查向量区 magic 和镜像尺寸字。
5. RAM 寄存器：对 `g_bms_mcu_selftest_ram_probe` 执行双图案读写。
6. ADC 数模转换：检查 ADC 供电 GPIO 输出态、SAR ADC power 位和 24M-to-SAR 时钟位。
7. 中断自检：检查全局中断使能、Timer0 IRQ mask 和 Timer0 enable。

运行期失败后记录 `g_bms_mcu_selftest_last_error`，关闭全局中断，拉低 BMS 输出和外设供电脚，闪烁 `BMS_MCU_SELFTEST_ALARM_PIN`，并在失败循环中喂狗保持锁定失败态。默认报警脚为 `ADC_NTC_PIN`，如量产硬件有独立报警脚，可在工程配置中覆盖 `BMS_MCU_SELFTEST_ALARM_PIN`。

说明：当 `sys_time.low_power_mode` 已进入低功耗准备态时，ADC/中断资源可能被业务逻辑主动关闭，运行期自检跳过，唤醒恢复正常运行后继续逐轮检查。

## 4. 测试步骤

通用准备：

1. 使用 B85 `825x_ble_sample` 工程编译并烧录固件。
2. 连接示波器或逻辑分析仪到启动期 PB4 报警脚，连接输出控制脚 `MCC_C_PIN`、`AFE_CTL_PIN`、`ADC_BUSEN_PIN`、`ADC_EN_PIN`。
3. 正常上电，确认无报警脉冲，固件进入 BLE/BMS 主流程。

CPU 寄存器测试：

1. 在启动汇编 CPU 测试段将 scratch 图案定义为 `0xAAAAAAAA`。
2. 将测试地址或图案加载到 R0。
3. 将 R0 指向内容加载到 R1。
4. 再将 R0 指向内容加载回 R0。
5. 比较 R0 与 R1，比较结果写入程序状态标志位。
6. 修改其中一次读回值或使用 `BMS_MCU_SELFTEST_FORCE_FAIL_ITEM=1` 编译运行期故障版本。
7. 预期：固件进入失败循环，输出脚拉低，报警脚闪烁，`main_loop()` 不再执行。

PC 测试：

1. 保持 `bms_mcu_selftest_pc_asm()` 正常路径，确认运行期不报警。
2. 将 `BMS_MCU_SELFTEST_FORCE_FAIL_ITEM` 设置为 `2` 后编译烧录。
3. 预期：运行期第一轮主循环进入失败循环，输出关闭并报警。

RAM 测试：

1. 正常版本确认 `g_bms_mcu_selftest_ram_probe` 双图案读写通过。
2. 将 `BMS_MCU_SELFTEST_FORCE_FAIL_ITEM` 设置为 `5` 后编译烧录。
3. 预期：进入失败循环。

Flash 测试：

1. 正常版本读取 `0x00000008 == 0x544C4E4B`。
2. 使用调试器临时断点改写读回值，或将 `BMS_MCU_SELFTEST_FORCE_FAIL_ITEM` 设置为 `4`。
3. 预期：进入失败循环。

时钟测试：

1. 正常版本确认 `clock_time()` 连续递增。
2. 使用调试器冻结 system tick，或将 `BMS_MCU_SELFTEST_FORCE_FAIL_ITEM` 设置为 `3`。
3. 预期：进入失败循环。

中断测试：

1. 正常版本确认 `reg_irq_en != 0`、`reg_irq_mask & FLD_IRQ_TMR0_EN`、`reg_tmr_ctrl & FLD_TMR0_EN`。
2. 在调试器中清除 `reg_irq_en` 或 Timer0 mask，或将 `BMS_MCU_SELFTEST_FORCE_FAIL_ITEM` 设置为 `7`。
3. 预期：进入失败循环。

ADC 测试：

1. 正常版本确认 `ADC_BUSEN_PIN/ADC_EN_PIN` 输出高、SAR ADC 未 power down、24M-to-SAR 时钟开启。
2. 使用调试器清除 `FLD_CLK_24M_TO_SAR_EN` 或拉低 ADC 供电脚，或将 `BMS_MCU_SELFTEST_FORCE_FAIL_ITEM` 设置为 `6`。
3. 预期：进入失败循环。

## 5. 验证状态

- 已新增源级静态测试，覆盖启动自检位置、运行期调用、检测顺序、失败关断动作和工程构建接入。
- 当前执行 `make all` 时，本机环境缺少 `tc32-elf-gcc`，未能完成固件级编译。需在安装 TC32 工具链并加入 PATH 后重新执行 `make all`。
