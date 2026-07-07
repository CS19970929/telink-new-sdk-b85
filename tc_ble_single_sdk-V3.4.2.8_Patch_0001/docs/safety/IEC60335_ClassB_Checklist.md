# IEC60335 Class B Checklist

| 项目 | 当前状态 | 说明 |
| --- | --- | --- |
| CPU 寄存器测试 | 已接入 | TC32 汇编轻量测试，覆盖 `0xAA/0x55` 模式 |
| Program Counter 测试 | 部分完成 | 当前用流程检查覆盖，尚未做破坏性 PC 测试 |
| 控制流检查 | 已接入 | `flow_counter` + inverse |
| Flash 完整性 | 框架完成 | CRC32 与元数据格式已预留，默认不强制 |
| RAM 测试 | 部分完成 | 当前只测安全自有 RAM，完整 RAM March C 待 startup 阶段实现 |
| Clock 测试 | 部分完成 | 已检查主时钟配置和 tick 前进，双时钟交叉测量待补 |
| Watchdog 测试 | 部分完成 | 运行期检查 WDT enable，启动破坏性测试默认关闭 |
| Stack 检查 | 部分完成 | CPU 测试中有栈探针，尚无栈边界水位 |
| ADC 检查 | 已接入 | 宽范围合理性检查 |
| AFE 通信检查 | 已接入 | 连续通信异常计数触发 Fail Safe |
| MOS 反馈 | 未完成 | 当前板级未确认独立反馈输入 |
| Fail Safe | 已接入 | 关闭危险输出并等待 WDT 复位 |
| 故障记录 | 部分完成 | RAM 快照已实现，BLE/Modbus 映射待补 |
| 故障注入 | 已接入 | 宏开关支持主要故障类型 |
| 认证证据 | 未完成 | 需要台架记录、覆盖率、边界条件和第三方评审 |

## 结论

当前提交建立了 Class B 风格的软件安全框架，但还不能等同于已完成 IEC 60335 Class B 认证。认证前必须补齐完整 RAM/Flash/Clock/WDT 证据链、MOS 反馈硬件诊断，以及 BLE/Modbus 可读故障记录。
