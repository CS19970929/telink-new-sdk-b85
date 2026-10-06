# BMS 软件验证报告

- 源码：`5418a9faf9b64ad1ae137d815e90525aeb9791f6`；输入指纹：`5a8a994bad9e1cc0d17a8e7637098dda257a70a0a2c470302604c14195a97d42`
- 结论：**软件验证通过**；计划 178 组，完成 178 组。
- PASS 仅表示已列出的软件断言成立；硬件验收未由本报告证明。

| 产品 | 完成 | 失败 |
|---|---:|---:|
| d008 | 41 | 0 |
| d011 | 41 | 0 |
| d013 | 39 | 0 |
| d014 | 55 | 0 |
| shared | 2 | 0 |

## 各领域执行证据

| 产品 | 领域 | 通过/完成 | 证据类别 |
|---|---|---:|---|
| d008 | protection | 5/5 | 完整 feature 与真实板级能力、源码 contract、生产 TU 场景 |
| d008 | configuration | 5/5 | 产品编译分支/源码 contract、真实默认构造与校验 |
| d008 | afe | 12/12 | 原厂范围独立向量与真实校验函数体、实际DVC读取/采样函数体与原厂RC事件向量、提取函数故障注入、源码 contract |
| d008 | soc | 6/6 | 源码 contract、生产 K/B 与独立数学 oracle、生产 SOC 函数体与环境替身、生产 SOC/部分 PM 与环境替身 |
| d008 | storage | 2/2 | 生产 journal/语义存储与 RAM Flash |
| d008 | protocol | 4/4 | UART DMA/IRQ 软件模型、提取函数场景、真实 parser/CRC 与寄存器替身、真实封包与固定 wire 向量 |
| d008 | diagnostics | 3/3 | 源码 contract、生产代码与环境替身 |
| d008 | power | 2/2 | 提取函数场景 |
| d008 | tooling | 2/2 | 实际编译预处理、工具单测 |
| d011 | protection | 6/6 | 完整 feature 与真实板级能力、源码 contract、生产 TU 场景、真实 backend/guard/SW 联合场景 |
| d011 | configuration | 2/2 | 板级源码 contract、真实默认构造与校验 |
| d011 | afe | 13/13 | 原厂SCT四位枚举与完整control函数体、原厂范围独立向量与真实校验函数体、完整 control 函数体与总线模型、实际SH测量发布函数体与明确协议范围、提取函数故障注入、源码 contract、生产函数体与环境替身 |
| d011 | soc | 5/5 | 提取函数场景、源码 contract、生产 K/B 与独立数学 oracle、生产 SOC 函数体与环境替身 |
| d011 | storage | 4/4 | 提取函数故障注入、生产 journal/语义存储与 RAM Flash |
| d011 | protocol | 4/4 | UART DMA/IRQ 软件模型、固定 RS485 源码 contract、提取函数场景、真实 parser/CRC 与寄存器替身 |
| d011 | diagnostics | 3/3 | 源码 contract、生产代码与环境替身 |
| d011 | power | 2/2 | 生产函数体与环境替身 |
| d011 | tooling | 2/2 | 实际编译预处理、工具单测 |
| d013 | protection | 6/6 | 完整 feature 与真实板级能力、源码 contract、生产 TU 场景、真实 backend/guard/SW 联合场景 |
| d013 | configuration | 1/1 | 真实默认构造与校验 |
| d013 | afe | 13/13 | 原厂SCT四位枚举与完整control函数体、原厂范围独立向量与真实校验函数体、完整 control 函数体与总线模型、实际SH测量发布函数体与明确协议范围、提取函数故障注入、源码 contract、生产函数体与环境替身 |
| d013 | soc | 5/5 | 提取函数场景、源码 contract、生产 K/B 与独立数学 oracle、生产 SOC 函数体与环境替身 |
| d013 | storage | 4/4 | 提取函数故障注入、生产 journal/语义存储与 RAM Flash |
| d013 | protocol | 3/3 | UART DMA/IRQ 软件模型、提取函数场景、真实 parser/CRC 与寄存器替身 |
| d013 | diagnostics | 3/3 | 源码 contract、生产代码与环境替身 |
| d013 | power | 2/2 | 生产函数体与环境替身 |
| d013 | tooling | 2/2 | 实际编译预处理、工具单测 |
| d014 | protection | 8/8 | 22 个原始生产 TU 与 SPI/Flash 端口模型、完整 feature 与真实板级能力、提取函数场景、源码 contract、生产 TU 场景、真实 backend/guard/SW 联合场景 |
| d014 | configuration | 4/4 | 提取函数场景、板级源码 contract、源码 contract、真实默认构造与校验 |
| d014 | afe | 16/16 | 22个原始生产TU与配置回滚故障、公共芯片驱动/量化场景、原厂SCT四位枚举与完整control函数体、原厂范围独立向量与真实校验函数体、完整 control 函数体与总线模型、实际SH测量发布函数体与明确协议范围、提取函数故障注入、源码 contract、生产函数体与环境替身 |
| d014 | soc | 5/5 | 提取函数场景、源码 contract、生产 K/B 与独立数学 oracle、生产 SOC 函数体与环境替身 |
| d014 | storage | 5/5 | 提取函数场景、提取函数故障注入、生产 journal/语义存储与 RAM Flash |
| d014 | protocol | 4/4 | UART DMA/IRQ 软件模型、固定 RS485 源码 contract、提取函数场景、真实 parser/CRC 与寄存器替身 |
| d014 | diagnostics | 3/3 | 源码 contract、生产代码与环境替身 |
| d014 | power | 2/2 | 生产函数体与环境替身 |
| d014 | tooling | 8/8 | 完整可移植核心 TU、实际编译预处理、工具单测、工具行为/源码 contract、故意变异检验、缓存回归 |

## 失败与复现

已完成的测试无失败。


相同保护输入已在 4 个产品运行；结构化输出比较：已执行产品一致。

## 配置与行为比较

与指定基线的结构化变化：0 项；未指定基线时不作历史一致性结论。


| 产品/profile | 串数 | 容量(0.1Ah) | SW CUV3(mV) | AFE CUV(mV) | AFE mask |
|---|---:|---:|---:|---:|---:|
| d008/16s-lfp | 16 | 78 | 2200 | 2200 | 63 |
| d008/20s-nmc | 20 | 78 | 2200 | 2200 | 63 |
| d008/24s-lfp | 24 | 78 | 2200 | 2200 | 63 |
| d011/default | 10 | 116 | 3000 | 3000 | 223 |
| d013/default | 4 | 116 | 3000 | 3000 | 223 |
| d014/default | 8 | 116 | 3000 | 3000 | 223 |

65 个软件字段、35 个 AFE requested 字段及两类 AFE 寄存器实写轨迹保存在 report.json；它们是开发配置，不是产品签核。

## 风险与盲区

- **高 / 完整系统 SIL**：D014 已连接真实参数/journal/SH SPI/feature/guard/SW 到 FET 命令；完整 MCU 调度、DVC 联合链、物理 Gate 和跨 MCU reset 的软件 OC 锁存策略仍未闭合
- **高 / AFE 物理行为**：RAM 寄存器不模拟硅片比较器、ADC 误差、转换时延或 watchdog 断总线后的 Gate；官方手册/板级读回仍需独立核验
- **高 / 产品参数**：D013 缺受控原理图；容量/阈值未全部签核；D008 20S NMC 仍使用公共软件保护默认且 SC 默认关闭
- **高 / SOC/电流**：长时算法场景使用受控 100Ah 环境；开机零点的真实零电流、SH 新鲜温度标志、C+ 判据和整板标定仍需台架
- **高 / 存储/升级**：逐字节故障模型不等于 Flash 擦除掉电、电源瞬态或实际 OTA；三域分别提交不构成跨域原子事务
- **中 / 协议**：parser 随机测试的寄存器所有者为替身；BLE SDK/分片/RF、UART 任意粘包吞吐、PHY 仍需硬件；CAN 未进入当前四产品真实源码清单
- **中 / 调度/并发**：host 不证明中断最坏时延、栈水位、TC32 ABI/栈溢出、实际 Sleep 电流；需 ELF/MAP 和实测
- **中 / 覆盖指标**：不把测试组数或断言数当行/分支覆盖率；未测量的路径显式保留未知，不给虚构覆盖百分比
