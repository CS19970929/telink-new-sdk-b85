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

## 第二批：SH 遗留实现退出

删除 SH 树内 5 个 DVC C、5 个 DVC H 和不适用的 DVC source contract；删除空 SIF/mux 的 2 C/2 H，并同步 source_order。原 ELF 不含 DVC 后端/服务可达符号，DVC Modbus window 的 count 原为0，因此删除其恒不命中的分支；未知地址读值/写异常继续由原 fallback 处理。D008 的真实 DVC/SIF/mux 不受影响。

UART 在 app 初始化中直接调用 modbus_uart_init，IRQ 仅保留 SDK 与 UART 的原顺序。两个 PM 条件用固定 SH3673510_FIXED_UART_BLOCKS_PM=1 表达原固定 UART 非 OWC idle 的结果；没有开启任何新睡眠入口。sleep host 默认检查该门禁，同时在独立测试场景隔离门禁以保留原潜在 PM 故障路径注入；测试场景不是支持的产品模式。

### 第二批验证结果

全部 93 个编译输入、ELF 链接通过，无编译诊断。text/data/bss：改前 109376/3980/6588 B，当前 108592/3976/6580 B。Host 25 组，剩余失败：production_policy_check.py。D014 production policy 为改前既有失败。
