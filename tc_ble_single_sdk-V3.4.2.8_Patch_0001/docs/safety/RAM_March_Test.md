# RAM March Test

## 测试目的

检测 RAM stuck-at 和部分 coupling fault，保护安全状态变量和关键运行数据。

## 测试原理

当前实现对安全框架自有 RAM 缓冲区执行 March 风格模式测试：

- `0x00000000`
- `0xFFFFFFFF`
- `0xAAAAAAAA`
- `0x55555555`

运行期每次只检查一小段，避免阻塞主循环。

## 测试步骤

1. 启动阶段调用 `Safety_RamStartupTest()`。
2. 运行阶段每 100 ms 调用 `Safety_RamRuntimeTask()`。
3. 观察是否误触发 Fail Safe。

## 故障注入方法

```c
#define SAFETY_TEST_ENABLE 1
#define SAFETY_INJECT_RAM_FAULT 1
```

## PASS 条件

- 安全 RAM 区域所有模式读写一致。
- 运行期分片测试不影响 BLE/AFE 周期任务。

## FAIL 条件

- 任一模式读回不一致。
- 故障注入后未进入 Fail Safe。

## 剩余限制

认证级完整 RAM March C 需要在 startup 汇编阶段实现，并排除栈、`.data`、`.bss`、retention 和 BLE SDK 保留 RAM。本次实现先避免破坏现有业务内存。
