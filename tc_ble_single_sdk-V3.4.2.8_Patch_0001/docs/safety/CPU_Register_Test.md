# CPU Register Test

## 测试目的

检测 TC32 CPU 通用寄存器和基本状态寄存器访问是否存在 stuck-at 或读写异常。

## 测试原理

`safety_cpu_test_asm.S` 使用 TC32 汇编对 `r1-r6` 写入 `0xAA` 和 `0x55` 两种互补模式并立即比较。C 层再用自动变量探测当前栈可读写。

## 测试步骤

1. 上电进入 `Safety_CpuStartupTest()`。
2. 调用 `safety_cpu_register_test_asm()`。
3. 检查栈探针变量。
4. 运行期 `Safety_CpuRuntimeTest()` 执行轻量寄存器测试。

## 故障注入方法

```c
#define SAFETY_TEST_ENABLE 1
#define SAFETY_INJECT_CPU_FAULT 1
```

## PASS 条件

- 汇编例程返回非 0。
- 栈探针变量两种模式读写正确。
- 运行期测试不影响 BLE 主循环。

## FAIL 条件

- 任一寄存器模式比较失败。
- 栈探针读写失败。
- 故障注入后未进入 Fail Safe。
