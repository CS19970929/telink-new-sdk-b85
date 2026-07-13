# 故障注入矩阵

| 注入项 | 掩码位 | 注入点 | 预期故障 | 预期安全动作 | 主机可测 | 硬件必测 |
|---|---:|---|---|---|---|---|
| CPU 寄存器 | 1 | CPU 测试返回值 | CPU_REGISTER | 禁止输出、致命锁存、停喂狗 | 构建检查 | 是 |
| CPU 标志 | 2 | 标志/分支返回值 | CPU_FLAG | 同上 | 汇编链接检查 | 是 |
| 控制流 | 3 | 签名比较 | CONTROL_FLOW | 同上 | 算法测试 | 是 |
| Flash Manifest | 4 | 结构校验 | FLASH_MANIFEST | 同上 | 解析/字段测试 | 是 |
| Flash CRC | 5 | CRC 完成比较 | FLASH_CRC | 同上 | BIN 损坏测试 | 是 |
| RAM | 6 | March/透明测试结果 | RAM | 同上 | 算法/配置 | 是 |
| 栈 | 7 | guard 判定 | STACK | 同上 | 布局检查 | 是 |
| 时钟 | 8 | 时基判定 | CLOCK | 同上 | 状态机测试 | 是 |
| 中断丢失 | 9 | Timer0 计数窗口 | INTERRUPT_LOST | 同上 | 状态机测试 | 是 |
| 中断频繁 | 10 | Timer0 计数窗口 | INTERRUPT_FREQUENT | 同上 | 状态机测试 | 是 |
| 心跳丢失 | 11 | 自检调度/喂狗门控 | WATCHDOG_HEARTBEAT | 不喂狗并保持安全 | 状态机测试 | 是 |
| ADC | 12 | 连续越界计数 | ADC | 关闭 MOS/均衡/RF | 状态机测试 | 是 |
| AFE 通信 | 13 | AFE 状态报告 | AFE_COMM | 关闭 MOS/均衡/RF | 状态机测试 | 是 |
| AFE 配置 | 14 | 配置回读比较 | AFE_CONFIG | 关闭 MOS/均衡/RF | 配置检查 | 是 |
| MOS 反馈 | 15 | 反馈一致性 | MOS_FEEDBACK | 关闭 MOS/均衡/RF | 状态机测试 | 是 |
| retention | 16 | retention 启动入口 | RETENTION | 禁止输出、致命锁存 | 构建检查 | 是 |
| 内部正反码 | 17 | 关键变量/调度注入 | INTERNAL_DATA | 致命锁存 | 算法测试 | 是 |

测试构建示例：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_selftest.ps1 -TestBuild -FaultMask 0x20
```

量产约束：只有 `BMS_TEST_BUILD=1` 与 `BMS_FAULT_INJECT_ENABLE=1` 同时存在才允许注入；生产默认值为 0，脚本和单元测试检查该约束。注入命令不暴露在 Modbus/BLE，避免现场远程开启危险测试功能。
