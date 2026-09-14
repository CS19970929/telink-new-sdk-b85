# BMS 架构与 AFE 移植

## 1. 依赖方向

```text
app.c / BMS 业务 / 通信
             |
             v
         bms_afe.h                 稳定的编译期 AFE 边界
             |
             v
      bms_afe_guard.c              通信健康、输出 inhibit、请求状态仲裁
             |
             v
   dvc1124 backend adapter         DVC 快照、BMS 单位/故障映射
             |
             v
dvc1124.c / dvc1124_special.c      寄存器、转换、命令和采样
             |
             v
        Telink B85 I2C
```

一个产品固件只编译一种 AFE，因此这里采用直接函数接口，不使用运行时 ops table、factory 或 registry。这样能保留静态检查能力，也避免函数指针、初始化顺序和额外代码体积。`bms_afe_backend.h` 只负责**编译期选择**，当前 HS-D008 固定选择 `BMS_AFE_BACKEND_DVC1124`。

D008 从 D013 借用的是架构边界和安全恢复策略，不复制 SH3673510 的 SPI、寄存器、GPIO 或板级语义。DVC 后端实现源在编译时被重绑定为 `dvc1124_backend_*`，公开 `bms_afe_*` 入口由 `bms_afe_guard.c` 统一持有。

## 2. 模块职责

| 模块 | 职责 | 不应承担 |
|---|---|---|
| `bms_afe.h` | AFE 初始化、采样、休眠、保护参数应用、FET、输出门控和辅助测量契约 | 器件寄存器、协议、全局业务状态 |
| `bms_afe_backend.h` | 编译期选择产品 AFE；D008 默认 DVC1124 | 运行时切换、寄存器配置 |
| `bms_afe_guard.c` | 缓存上层 FET 请求；通信失败立即 inhibit；连续有效快照后恢复资格；保持产品 output gate 独立 | DVC 寄存器、产品阈值、协议帧 |
| `dvc1124_reg.h` | DVC1124 V1.2 地址、mask、shift、access/side-effect 真值 | 板级默认参数 |
| `dvc1124_project_config.h` | 当前板的 cell/GP/ADC/WDT 等默认配置 | 运行状态 |
| `dvc1124.c` | I2C、CRC、寄存器编解码、配置应用、采样快照 | BLE/UART 协议 |
| `dvc1124_special.c` | RC/W0C/self-clear 等特殊访问 | 普通寄存器镜像 |
| `dvc1124_bms.c` | DVC 快照到 BMS 报告/故障/单位的映射；硬件保护 latch 的恢复判据 | 通信帧解析 |
| `dvc1124_config_service.*` | transport-neutral 语义配置和事务 | BLE 或 UART 专属逻辑 |
| `bms_state.*` | BMS 报告、系统状态、分级故障历史和通用查表的唯一所有者 | AFE 寄存器、通信帧 |
| `bms_error.h` | 有类型的跨模块错误 API；计数在 `bms_state.c` 中饱和 | 公开可跨文件任意修改的错误结构 |

当前仍有一项已知债务：`dvc1124.c` 还直接读写部分 `g_tParam` 和 `g_stCellInfoReport`。继续移植前应把这些业务映射逐步移到 `dvc1124_bms.c`，但不得重写已经验证的 I2C/CRC 核心路径。

## 3. AFE 通信健康与 FET 仲裁

D008 采用与新 D013 框架一致的 fail-safe 原则：

1. `bms_afe_set_fets()` 只记录上层请求，并经统一 guard 下发；
2. 任意一次完整 snapshot 无效，立即进入 `comm_inhibit` 并请求 CHG/DSG 全关；
3. inhibit 期间，上层即使持续请求开 MOS，也不能绕过；
4. 需要连续 **3 个完整有效 snapshot** 才恢复通信资格；
5. 恢复通信资格不等于无条件开 MOS，最终仍受 `output_enabled` 和 DVC 后端保护判断约束；
6. 进入 AFE sleep 前重新 inhibit，避免休眠前旧 snapshot 在唤醒后继续授权；
7. 保护配置写入失败同样进入 inhibit。

这个 guard 只处理“AFE 数据/通信是否可信”。短路、过流、OV/UV、温度等保护状态仍由 DVC 后端和 BMS fault 层各自负责，不能用通信恢复自动清除保护 latch。

## 4. D008 当前 DVC 默认配置边界

本轮按 HS-D008 原理图与 DVC1124-2 Reference Manual V1.2 修正三个容易反向理解的默认值：

- `HSFM=1`：D008 使用 GP5/GP6 低边 CHG/DSG，因此屏蔽未验证的高边 CHG/DSG 输出；
- `CAES=0` 且 `CWT=0`：当前未签核休眠电流唤醒阈值，不能出现“引擎开启但阈值关闭”的半配置状态；
- `INT_MASK=0xFF`：0x79 是**屏蔽位**，0 表示允许 1 ms 中断脉冲；D008 当前没有消费 DVC GP 中断，因此默认全部屏蔽。

SCD、Body Diode current wake、DVC I2C watchdog 仍保持显式关闭，直到阈值、恢复动作和实板故障注入完成。关闭是“尚未验证”的开发基线，不代表量产安全策略。

## 5. 更换 AFE 的最小步骤

1. 根据目标器件的官方资料实现一个新的 backend adapter，而不是让应用层包含寄存器头。
2. 在 adapter 内完成原始码到统一单位的转换；变量名带单位，例如 `cell_voltage_mv`、`current_ma`、`temperature_decic`。
3. 明确保护请求值、硬件量化值和实际 readback 值，禁止静默截断。
4. 把目标 AFE 的寄存器真值、板级默认值和 BMS 映射分开管理。
5. 在 `bms_afe_backend.h` 选择 backend，并让实现源重绑定到 backend-private 符号；公开入口仍由 guard 持有。
6. 在 `bms_tools/source_order.txt` 中审核新增/替换实现和链接顺序。
7. 扩展 host contract tests，确认应用层没有包含目标 AFE 的寄存器头，并覆盖通信失效/恢复契约。
8. 重新验证保护、MOS、故障恢复、休眠/唤醒、配置掉电恢复和通信失败路径。

不要为了适配新 AFE 重新引入 `#define gpio_*`、`#define adc_*`、虚拟引脚或旧器件函数名 alias。这些做法会把真实硬件访问隐藏在预处理器后面，降低审查和移植可靠性。

## 6. 状态和配置所有权

- `g_tParam.protect` / cold KV：BMS 通用 requested protection 的唯一来源。
- DVC AFE config KV：只保存 DVC 专属工作配置，不复制 COV/CUV/OCD/OCC。
- `g_stCellInfoReport` / `g_bms_system_status`：对外测量、故障和系统状态；唯一实体由 `bms_state.c` 持有。
- error / fault history：只能通过 `bms_error_*` / `bms_fault_history_*` 访问，通信层不得直接读写底层数组。
- SOC KV：只保存真实 SOC、累计放电量和 cycle；显示 SOC 不持久化。
- runtime：独立记录工厂/正常模式累计运行时间。
- event log：只记录事件边沿，不作为实时故障状态源。
- `bms_afe_guard.c`：只持有临时 FET request / output gate / comm qualification，不持久化。

配置、测量、运行态和持久化镜像必须保持不同生命周期；不得再合并为一个可被多个文件任意写入的大结构体。

## 7. 安全边界

- 应用层不得直接写 AFE 寄存器。
- 普通 raw diagnostics 不读取 RC 寄存器，也不能写 reserved/read-only 位。
- FET、Balance、Open Wire、Calibration、Sleep/Shutdown 是运行命令，不属于持久配置。
- AFE/I2C 操作必须有超时和明确错误传播。
- 保护配置成功响应必须代表 live AFE 与持久请求值一致；不能先 ACK 再异步尝试应用。
- 通信恢复只能恢复“允许评估输出”的资格，不能清除 SCD 等尚未验证恢复条件的硬件保护。
- 新 AFE 或新硬件默认值未完成实板验证前，硬件保护和软件保护均不得被无依据禁用。
