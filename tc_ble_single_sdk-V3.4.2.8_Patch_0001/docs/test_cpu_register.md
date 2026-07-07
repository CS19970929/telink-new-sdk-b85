# CPU 寄存器启动自检测试说明

## 测试目的

验证 TC32 CPU 通用寄存器基本读写能力，避免寄存器 stuck-at 故障导致 BMS 保护逻辑运行在错误状态。

## 测试原理

启动阶段 `Safety_StartUpTest()` 调用 `Safety_CpuStartupTest()`，再进入 `safety_cpu_register_test_asm()`。汇编例程对 `r1-r6` 写入 `0xAA` 和 `0x55` 两种互补模式并比较；C 层额外用栈探针验证当前 stack 可读写。

## 测试步骤

1. 保持 `SAFETY_ENABLE=1`。
2. 编译并烧录固件。
3. 上电启动。
4. 观察系统是否能进入正常 BLE/BMS 主循环。
5. 打开 CPU 故障注入后重新编译烧录。
6. 再次上电，观察是否进入 Fail Safe。

## 故障注入方法

在 `safety_config.h` 中临时打开：

```c
#define SAFETY_TEST_ENABLE 1
#define SAFETY_INJECT_CPU_FAULT 1
```

## PASS 标准

- 未注入故障时系统正常进入业务初始化和主循环。
- 注入 CPU 故障时执行 `Safety_EnterFailSafe()`。
- Fail Safe 后 `AFE_CTL_PIN`、`MCC_C_PIN` 等危险输出被关闭。
- WDT 不再被喂狗并最终复位 MCU。

## FAIL 标准

- 未注入故障时误进入 Fail Safe。
- 注入故障后仍继续运行主循环。
- Fail Safe 后危险输出未关闭。

## 备注

更完整的认证级 CPU/PC 测试还需要补充 Program Counter 路径测试和状态寄存器破坏性测试。本文件对应当前已落地实现；总体安全架构见 `docs/safety/Safety_Architecture.md`。
