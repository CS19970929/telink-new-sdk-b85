# DVC1124-2 后续任务

本文件只保留当前未完成工作和验收条件。已完成的阶段性提交由 Git 历史追溯，不在文档中重复罗列 commit 清单。

## 已建立的基线

- [x] V1.2 寄存器地址、mask、shift 和关键 access 语义集中到 `dvc1124_reg.h`。
- [x] R86、OC1/OC2 delay `+1`、0x6D、GP N/A 等已知真值问题已修正。
- [x] RC/W0C/self-clear 访问集中到 `dvc1124_special.c`，普通 raw read 不消费 0x01/0x76 状态。
- [x] Core OT 使用专用访问并保留 COTF software sticky latch。
- [x] 字段写入在编码前检查范围，禁止 mask 静默截断。
- [x] BMS requested protection 继续由 `g_tParam.protect` / cold KV 唯一持有。
- [x] DVC 专属配置使用独立 4-sector KV，并在 reset/重启后恢复。
- [x] BLE 与 UART 共用 `modbus_on_frame()` 和同一语义配置服务。
- [x] raw write 受 Factory 门禁，并经过 semantic candidate、validation、apply/persist。
- [x] 单字段配置在持久化失败时尝试回滚；multi-write 在原子事务实现前明确拒绝。
- [x] 应用使用 `bms_afe.h`；旧 `App_AFEGet`、`MTPWrite`、虚拟 GPIO/ADC 和 SDK 宏 alias 已删除。
- [x] host contract tests 覆盖寄存器真值、特殊访问、配置服务、协议入口和 Flash 布局。

以上“完成”只代表源码/host contract 基线；固定 TC32 build 和实板测试见 `HARDWARE_VALIDATION.md`。

## P0：构建和实板闭环

- [ ] 固定 TC32 clean rebuild、MAP/BIN `<=124 KB`、官方固件检查和 manifest verify。
- [ ] 按 `HARDWARE_VALIDATION.md` 完成 I2C/测量/保护/MOS/低功耗/OTA 顺序测试。
- [ ] 验证配置 apply 成功、persist 失败且 rollback 也失败时，进入明确的 config-inconsistent/AFE fault 状态。
- [ ] 定义 AFE 连续通信失败时 MOS 保持/关闭/限制策略，并用异常注入验证。

## P1：配置完整性

- [ ] 补齐 R52/R53/R54 所有公开 RW bit 的语义字段。
- [ ] 补齐 0x6A..0x6C 的 PKM/LDM/CTM/V1P8M 与 cell/measurement mask 模型。
- [ ] 检查 CADC/CC1/VADC/CP/GP/WDT/timed wake/int mask 完整性。
- [ ] 完整更新 `dvc1124_config_catalog.json` 的 access、side effect、unit、range、persistent 和 factory-only metadata。
- [ ] 让所有 semantic/raw read 失败返回协议异常，禁止用 `0xFFFF` 冒充真实数据。

## P1：原子配置事务

- [ ] 实现 staged `Begin/GetPending/SetPending/Validate/Commit/Rollback`。
- [ ] apply 后 readback，persist 失败后验证 rollback。
- [ ] 为 Modbus 0x10 的 DVC semantic range 提供真正的多字段原子写。
- [ ] 明确 OC1/OC2 hardware delay 与现有单一 BMS filter 的关系，不能伪造两套独立 requested 参数。

验收标准：任一字段失败不会留下半更新 AFE 或 Flash；成功响应代表 live AFE、内存请求值和持久化值一致。

## P1：保护与 MOS 状态机

- [ ] 充电过流后检测有效放电电流，受控恢复 CHG 通道。
- [ ] 放电过流后检测有效充电电流，受控恢复 DSG 通道。
- [ ] 为方向恢复定义 threshold、debounce、latch、retry 和 timeout。
- [ ] 与 0x53/0x54 mask、Body Diode、OC/SCD latch 和实际 FET readback 协同。
- [ ] 故障日志记录触发源、硬件/软件状态和恢复原因。

验收标准：不存在“硬件关断、软件立即重开、硬件再次关断”的循环。

## P2：Driver / BMS 边界

- [ ] `dvc1124.c` 不再直接依赖 `g_tParam` 或 `g_stCellInfoReport`。
- [ ] protection encoding 接收明确的纯数据结构，BMS 映射归 `dvc1124_bms.c`。
- [ ] 评估 `dvc1124.h` 中较大的 `static inline`，只把确有收益的实现移入 `.c`。
- [ ] 为其它 MCU 提取最小 bus/delay port；至少有第二个真实实现前不引入复杂 registry/factory。

## 执行规则

- 寄存器、时序和换算只以项目锁定的 DVC1124-2 V1.2 手册、HS-D008 原理图/BOM和实板证据为依据。
- P0/P1 安全问题优先于架构美化。
- 每次重构都要证明寄存器实际写值和外部协议没有无意改变。
- 不重写已经验证的 I2C/CRC/采样核心来追求风格统一。
- 未完成硬件验证的功能保持 `TODO_VERIFY_HW` 或默认禁用，不能标记为量产完成。
