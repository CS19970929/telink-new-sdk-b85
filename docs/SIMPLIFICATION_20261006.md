# 第二轮简化：一条主流程、显式产品差异

审查起点为 `codex-bms-monorepo` 的 `f8047d168afcdc4844f6228a713cc447631d3536`。实际工作树是 `D:/telink/bms-monorepo`，四产品实际编译清单未改变。原厂 AFE 资料、硬件参数、当前 CFG2/State/Event schema、九类更新编号、wire 地址、Boot/APP 边界和既有 MCU reset/watchdog/OTA 重启策略保持。

## 复杂度与本轮减法

最大的偶然复杂度是 SH 三份近乎相同的配置、app 两份主循环、无消费者的公共接口，以及测试对函数文本/历史 SHA 的绑定。SOC 的主要复杂度属于实际算法与持久状态，不能按默认开关判断是否可删除。

- `bms/products/sh3673510_defaults.h` 只保存三块 SH 板共同接线和 AFE 默认；产品文件先显式给出串数、Rsense、balance/heater/NTC 能力，再直接 include 一次公共默认。没有宏覆盖继承链。D011 加热 PB4/PB5 仍只在 D011 文件。
- `app.c` 共用 `main_loop()`、`mos_update()`、FIFO 定义和 event 秒门禁；D008 关机保持、UART/SIF 复用、各板 GPIO 和低功耗流程继续独立。
- 将相同的 CHG/DSG 配置无效、open-wire 硬阻断查询合并为 `bms_features_outputs_blocked()`；heater 的方向阻断继续由后端处理，不混入硬阻断。
- SOC 删除始终与 `g_soc_input_valid` 同步的 `g_soc_input_ready`；`set_calsoc()` / `set_dispsoc()` 收回模块内部。
- 删除无业务消费者的 `storage_record_format()`、`storage_record_crc32()`、`bms_state_store_write_learning()`、`bms_features_charge_blocked()`、`bms_features_get_openwire_result()`、`SH3673520_SetCellCount()`、`SH3673520_ReadStatus()` 及其数据类型、`DVC1124_ReadCoreOtThresholdCode()`、`DVC1124_ClearCoreOtEventLatched()`。初始化串数仍由真实 SCONF4 配置写入；CRC、学习元数据事务和核心过温 RC 事件锁存保留。
- 删除只包含未使用标记和重复声明的 `dvc1124_boot.h`；后端原语的边界仍为 `bms_afe_driver.h`。SH Modbus 不再编译四个虚假的 DVC no-op 函数。guard 删除当前两个后端均不可能执行的第三后端条件分支。
- 测试共用既有 `validation_support.function()`，删除十份独立函数体扫描。工具单测内部已枚举产品配置，因此由四次相同执行改为一次；真实产品行为测试仍按产品执行。

## 当前产品差异

| 产品 | AFE / 串数 / Rsense | 保留的板级差异 |
|---|---|---|
| D008 | DVC1124；16S LFP / 20S NMC / 24S LFP；200 µΩ | I2C、PB1 负载恢复证据、AUTO_DIODE、LDO/shutdown、UART/SIF 复用、heater/balance/NTC |
| D011 | SH3673510；10S；250 µΩ | balance、TS3 heater、TS4 MOS NTC、PB4/PB5 安全约束 |
| D013 | SH3673510；4S；100 µΩ | 无 balance/heater/MOS NTC；板级映射及原图/BOM待验证 |
| D014 | SH3673510；8S；667 µΩ | balance、无 heater/TS3、实装 TS4 MOS NTC及图纸差异 |

预处理核对包含四产品 414 个相关宏：413 项 token 相同，D013 debug LED 的 `0` 与公共 `0u` 数值相同。三 SH 产品的真实默认构造、校验和板级能力由行为测试核对；没有更改继承容量或未签核阈值。

## 维护者需要掌握的调用链

`main()` → normal/retention 初始化 → `main_loop()`：BLE SDK → Runtime → 到期 `app_sample_task()` → event 秒任务 → D008 mux/SIF → Modbus → State checkpoint → 板级 PM。

每 200 ms 到期只补采一次：`bms_afe_sample()` → guard/选定后端 → AFE 原始采集、校准、有效性和 SW/HW 保护 → `app_update_soc_from_sample()` → `mos_update()`。SH CADC 的转换完成与 400 ms 新鲜性窗口独立于轮询周期，不能把每次调用当作新 ADC。

MOS 链为应用请求 → guard 授权/通信/配置/样本门禁和双向硬阻断 → 后端方向保护/物理恢复/硬件命令 → AFE 反馈。请求、已发命令和物理反馈不能合并成一个 bool。SC/OCD/OCC 仍要求物理恢复证据，关 MOS 后零电流不能解除保护。

参数链为 Modbus/BLE 候选 → 参数或 AFE 所有者的校验/授权 → Config/State/Event → `storage_record` → Telink Flash。Config 候选与已提交缓存、State 待保存与已落盘值有不同掉电含义，继续保留；九类 OTA 更新编号仍按域处理，三域不是跨域原子事务。

## SOC 与其他必须保留的复杂度

SOC 积分、OCV、真实/显示 SOC、满空端点、ETA、校准及学习状态继续使用原算法。学习默认关闭，但当前 CFG2 可合法开启，已学习容量可影响有效容量，不能删除。新输入与上一个接受样本的电流/时间也不相同，方向切换和重复帧需要分别判断。本轮 SOC 仅减少一个同义状态、两个公开修改入口，未宣称算法大幅缩小。

AFE guard、器件后端、hardware profile 和特殊 RC/W0C 寄存器处理具有不同安全责任；不合并。Modbus parser 已按完整范围预检并限制单一状态所有者，本轮保留直接 if/switch。UART DMA/IRQ 所有权、RS485 DE 超时、SIF 双缓冲、BLE/OTA 与 Flash/PM 互锁也保留。

## 测试与证据

原 DVC 固定策略失败来自 Windows CRLF 与 Linux LF 的文本 hash 差异。核对当前原厂资料与寄存器实现后，删除历史 hash fixture，改为实际事务顺序、保留位、RC 事件、全部初值和逐访问失败/回读损坏断言，覆盖三 profile × HW on/off。没有改 hash 来掩盖业务差异。修复提交 `a7b5430a618b510c97f78f2f19a4f5d1fbdffd1d` 的 GitHub CI 已全部通过。

应用 MOS 请求以真实函数执行和参数 spy 验证，代替冻结 `chg_target` / `dsg_target` 局部变量名。SH BSTATUS1/BSTATUS2 的原字节向量与事务断言改走生产实际使用的 `ReadRegs()`；不是删除状态读取测试。仍需要提取函数的 SDK 相关夹具明确属于软件证据，不能冒充原始 TU 或实板。

本轮日志、预处理快照、ELF/MAP、资源结果和完整机器报告位于源码树外：`C:/Users/Administrator/Documents/CodexOutputs/bms-monorepo/simplification-20261006-f8047d16/`。日常阅读入口与历史 evidence 分开，原厂原件和历史认证/安全证据保留。

最终固定提交成绩和前后指标在完成全部回归后补录。计数口径为 `bms/**/*.c,h` 物理行；函数/状态/wrapper 是词法候选，不能当作编译后安全所有权数量或覆盖率。Flash 为 ELF 投影，RAM 为地址跨度，均不是实板栈水位或 BIN 实测。

实板仍须验证：四板真实 Gate/恢复窗口、C+/LOAD/PB1、ADC/NTC 时序和校准、watchdog 静默、重配置失败、reset/OTA、Flash 掉电、Sleep/Wake、RS485 DE/DMA 与 SIF 波形，以及产品容量/阈值签核。软件通过不解除 [硬件验收清单](HARDWARE_VALIDATION.md) 中的阻断项。
