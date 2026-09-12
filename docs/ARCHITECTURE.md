# BMS 架构与 AFE 移植

## 1. 依赖方向

```text
app.c / BMS 业务 / 通信
             |
             v
         bms_afe.h                 稳定的编译期 AFE 边界
             |
             v
       dvc1124_bms.c               BMS 单位、故障和状态映射
             |
             v
dvc1124.c / dvc1124_special.c      寄存器、转换、命令和采样
             |
             v
        Telink B85 I2C
```

一个产品固件只编译一种 AFE，因此这里采用直接函数接口，不使用运行时 ops table、factory 或 registry。这样能保留静态检查能力，也避免函数指针、初始化顺序和额外代码体积。

## 2. 模块职责

| 模块 | 职责 | 不应承担 |
|---|---|---|
| `bms_afe.h` | AFE 初始化、采样、休眠、保护参数应用、FET、输出门控和辅助测量契约 | 器件寄存器、协议、全局业务状态 |
| `dvc1124_reg.h` | DVC1124 V1.2 地址、mask、shift、access/side-effect 真值 | 板级默认参数 |
| `dvc1124_project_config.h` | 当前板的 cell/GP/ADC/WDT 等默认配置 | 运行状态 |
| `dvc1124.c` | I2C、寄存器编解码、配置应用、采样快照 | BLE/UART 协议 |
| `dvc1124_special.c` | RC/W0C/self-clear 等特殊访问 | 普通寄存器镜像 |
| `dvc1124_bms.c` | DVC 快照到 BMS 报告/故障/单位的映射 | 通信帧解析 |
| `dvc1124_config_service.*` | transport-neutral 语义配置和事务 | BLE 或 UART 专属逻辑 |
| `bms_state.*` | BMS 报告、系统状态、分级故障历史和通用查表的唯一所有者 | AFE 寄存器、通信帧 |
| `bms_error.h` | 有类型的跨模块错误 API；计数在 `bms_state.c` 中饱和 | 公开可跨文件任意修改的错误结构 |

当前仍有一项已知债务：`dvc1124.c` 还直接读写部分 `g_tParam` 和 `g_stCellInfoReport`。继续移植前应把这些业务映射逐步移到 `dvc1124_bms.c`，但不得重写已经验证的 I2C/CRC 核心路径。

## 3. 更换 AFE 的最小步骤

1. 根据目标器件的官方资料实现一个新的 `bms_afe_*` adapter。
2. 在 adapter 内完成原始码到统一单位的转换；变量名带单位，例如 `cell_voltage_mv`、`current_ma`、`temperature_decic`。
3. 明确保护请求值、硬件量化值和实际 readback 值，禁止静默截断。
4. 把目标 AFE 的寄存器真值、板级默认值和 BMS 映射分开管理。
5. 在 `bms_tools/source_order.txt` 中用新实现替换 DVC 文件并审核链接顺序。
6. 扩展 host contract tests，确认应用层没有包含目标 AFE 的寄存器头。
7. 重新验证保护、MOS、故障恢复、休眠/唤醒、配置掉电恢复和通信失败路径。

不要为了适配新 AFE 重新引入 `#define gpio_*`、`#define adc_*`、虚拟引脚或旧器件函数名 alias。这些做法会把真实硬件访问隐藏在预处理器后面，降低审查和移植可靠性。

## 4. 状态和配置所有权

- `g_tParam.protect` / cold KV：BMS 通用 requested protection 的唯一来源。
- DVC AFE config KV：只保存 DVC 专属工作配置，不复制 COV/CUV/OCD/OCC。
- `g_stCellInfoReport` / `g_bms_system_status`：对外测量、故障和系统状态；唯一实体由 `bms_state.c` 持有。
- error / fault history：只能通过 `bms_error_*` / `bms_fault_history_*` 访问，通信层不得直接读写底层数组。
- SOC KV：只保存真实 SOC、累计放电量和 cycle；显示 SOC 不持久化。
- runtime：独立记录工厂/正常模式累计运行时间。
- event log：只记录事件边沿，不作为实时故障状态源。

配置、测量、运行态和持久化镜像必须保持不同生命周期；不得再合并为一个可被多个文件任意写入的大结构体。

## 5. 安全边界

- 应用层不得直接写 AFE 寄存器。
- 普通 raw diagnostics 不读取 RC 寄存器，也不能写 reserved/read-only 位。
- FET、Balance、Open Wire、Calibration、Sleep/Shutdown 是运行命令，不属于持久配置。
- AFE/I2C 操作必须有超时和明确错误传播。
- 保护配置成功响应必须代表 live AFE 与持久请求值一致；不能先 ACK 再异步尝试应用。
- 新 AFE 未完成实板验证前，硬件保护和软件保护均不得被无依据禁用。
