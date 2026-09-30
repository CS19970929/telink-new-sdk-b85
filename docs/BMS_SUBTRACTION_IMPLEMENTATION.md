# BMS 减法式重构实施记录

## 范围与行为边界

2026-10-01 用户确认实施前三批：低风险清理、SH 遗留实现退出、缩短阅读路径。四个 common 分支分别验证和提交；当前主工作区 d014-485-test 的未提交改动不参与。

保护阈值/时序、恢复、MOS、SOC 算法、协议、Flash schema 和低功耗行为保持。SH 休眠固定 UART 与 OWC idle 条件冲突、D011/D013 guard 启动资格差异留作独立问题。测试日志入口不是正式诊断；正式事件与诊断窗口保留。

## 第一批：低风险清理

- 删除无调用/注册的 test_log_app、test_log_balance_first 及废弃 app 声明/注释调用。
- SH 删除仅赋值、不读取的 s_balance_effective；硬件实际 balance mask 仍每次读回。
- ProductionInfor 收为 Modbus 内部 static；WriteProID_Default 与所有协议字段保持。
- 删除两个 KV 名称别名头，调用真实 bms_config_store / bms_state_store；存储实现、记录格式和错误传播不变。
- Time_T 字段未删除；第一批不改变结构布局。

## 验证方法

改前改后采用相同 TC32 O2/ABI、固定 source_order、MCU_STARTUP_8251，全部 C/汇编编译并直接链接 ELF/MAP，输出到用户级临时区，不调用 objcopy 或 BIN 包装。运行各产品既有 Host contracts；资源/源码/实板证据分别记录。

改前已知 Host 失败：D008 profile selection 的默认值预期与当前 profile 不一致；D014 production policy 的 debug LED 预期、sleep fixture 缺少 modbus_uart_tx_active 桩。不得通过修改产品默认值消除测试失败。

实板尚未验证，本次结果不关闭 HARDWARE_VALIDATION.md。

### 第一批验证结果（D014）

全部 100 个编译输入、ELF 链接通过，无编译诊断。text/data/bss：改前 109376/3980/6588 B，改后 109360/3980/6592 B。

Host：除前述改前失败外通过；D008 storage harness 去除别名头替换形成的重复头拼接后，掉电/参数/SOC/诊断用例通过。无固件 BIN、无烧录。
