# D008 / D011 / D013 软硬件保护参数分离与 Windows 串口上位机规划

日期：2026-09-14

## 1. 目标

软件保护和 AFE 硬件保护从“共用一套参数”改为两个独立配置域。

- 软件保护继续使用现有 `g_tParam.protect` / GPRAM 三级参数：First / Second / Third + Recover + Filter。
- AFE 硬件保护使用独立硬件档案，不设三级，只保存 AFE 真正需要的触发阈值、硬件延时、恢复条件、恢复稳定时间和硬件使能。
- 普通客户修改软件保护参数时，不再重写 AFE 硬件保护寄存器。
- AFE 硬件保护参数使用单独的工程页面。客户版默认隐藏；内部完整版默认可见；特殊客户需要时再显式开放。
- 软件 Third 和 AFE 硬件保护并行进入最终 MOS 仲裁。任一通道处于保护状态都能禁止对应 MOS；一方恢复不能清除另一方的保护状态。

## 2. 三级软件保护语义保持不变

- First：一级告警/报告，不直接关闭 MOS。
- Second：二级告警/策略升级预留，不直接关闭 MOS。
- Third：软件保护级，参与 CHG/DSG MOS 禁止。

软件保护仍由公共 `bms_sw_protection.c/.h` 处理；本次不改变现有三级参数地址和普通上位机页面。

## 3. AFE 硬件保护的独立语义

硬件档案只表达 AFE 能真正实现的保护，不把软件三级概念复制到硬件层。

通用语义包括：

- 单体 OV：触发值、硬件延时、恢复值；
- 单体 UV：触发值、硬件延时、恢复值；
- 放电 OC1 / OC2：触发值、硬件延时；
- 充电 OC1 / OC2（芯片支持时）：触发值、硬件延时；
- SC / SCD：硬件阈值或芯片等价配置、硬件延时；
- AFE 硬件温度保护（芯片/产品支持时）：充高、充低、放高、放低及恢复条件；
- OC/温度等 MCU 参与清锁存的恢复阈值和稳定时间；
- 硬件保护使能；
- requested 与 effective/quantized 值。

不同 AFE 的寄存器和步进保持在 backend 内，不为了“统一”强制使用同一寄存器编码。

## 4. 首次升级迁移规则

这是兼容已出货板的关键约束。

第一次运行带独立硬件档案的新固件时：

1. 如果设备尚不存在独立 AFE 硬件档案，按**当前已加载的软件保护参数 + 当前产品 AFE 静态配置**生成一次初始硬件档案；
2. 立即持久化并写入 migration marker；
3. 后续修改 `g_tParam.protect` 不再同步修改该硬件档案；
4. 固件升级、软件保护恢复默认、客户通信改软件参数，都不得覆盖已经存在的 AFE 硬件档案；
5. AFE 硬件档案只能通过专用硬件参数接口或显式工厂复位策略修改。

因此升级后的第一版行为与升级前保持一致，而后软件/硬件参数才真正解耦。

## 5. D008 / DVC1124

继续复用现有 `0x2800` DVC semantic window，不再创建第二套 DVC 协议。

- `0x2840..0x284D`：硬件 requested COV/CUV/OCD1/OCD2/OCC1/OCC2/SCD；改为读取/写入独立硬件档案，不再读写 `g_tParam.protect`。
- `0x2850..0x285D`：AFE effective/readback，保持只读。
- 新增 recovery 语义字段：COV recovery、CUV recovery、OCD recovery、OCC recovery、recovery stable time。
- `dvc1124.c` 的硬件寄存器编程从独立硬件档案取值。
- `dvc1124_bms.c` 的硬件锁存恢复也从独立硬件恢复参数取值。
- `0x2900` raw register window 保留为内部诊断，不作为日常硬件参数编辑接口。

DVC 的 I2C WDT、Body Diode、Current Wake、GPIO mode 等属于 AFE operating/debug 配置，不混入“硬件保护参数”日常页面，可放在更深一级的工程页。

## 6. D011 / D013 / SH3673510（SH36735xx 寄存器族）

新增版本化 `0x2500` AFE hardware profile window，参考现有 Windows 3520 参数页面的物理单位模型，但以当前 Telink D011/D013 实现为准，不照搬 STM32 默认值。

- 识别：magic + schema + AFE model + capability；
- requested 区：OV/UV、OCD1/OCD2、OCC、SC、硬件温度、恢复阈值、恢复稳定时间、enable mask；
- status/effective 区：当前已应用/待恢复、cell count、Rsense、WDT、量化后的实际阈值和实际延时；
- 写入采用整组事务：validate -> persist -> apply -> readback；
- AFE 应用失败时保持 fail-safe inhibit，不允许仅凭 Modbus ACK 判断“已经生效”。

D011 与 D013 共享协议/算法，产品差异只来自 Product Profile（串数、Rsense、NTC 通道、硬件使能等）。

## 7. Windows 上位机

以 `codex-mos-protection-coordination` 现有 `bms-tool-windows` 为基础，不重新做一套应用。

### 通信

- BLE 和直连串口共用 `BmsClient` / Modbus RTU 上层逻辑；
- 直连串口默认 19200 8N1，可选波特率；
- AFE 硬件参数写入允许串口，不保留旧 SH367309 页面“仅 BLE 可写”的限制；
- 串口分片、CRC、超时、重连继续复用 `BmsSerialTransport`。

### 页面

普通页面：

- 实时数据；
- 设备信息；
- 软件三级保护参数；
- 日志/OTA 等现有功能。

工程页面 `AFE 硬件保护`：

- 默认隐藏；
- 内部完整版直接显示；
- 客户版需工程解锁后才显示；
- 自动识别 DVC1124 / SH3673510；
- 只显示当前 AFE capability 支持的字段；
- Requested 与 Effective 分列；
- 写前重新读取并检测并发修改；
- 写后完整回读并验证；
- 对软件 Third 与硬件阈值关系给出警告，但不自动同步两套参数。

## 8. 安全约束

- UI 隐藏/密码不是安全边界，固件必须校验每一个值和寄存器能力。
- 不允许任意 raw register write 代替硬件参数接口。
- 不自动把软件 Third 写给硬件。
- 不自动把硬件值反写软件参数。
- 不自动修改尚未完成实板签核的 D008 SCD / WDT / Body Diode 等策略。
- 所有 AFE 写操作记录日志：设备、AFE、旧值、新值、requested、effective、结果。

## 9. 验收

1. 修改软件三级保护后，AFE hardware profile 与寄存器不变化；
2. 修改 AFE hardware profile 后，`g_tParam.protect` 不变化；
3. 断电重启后两套参数分别恢复；
4. 首次升级老设备只迁移一次，后续不再跟随软件值；
5. D008/D011/D013 都能通过串口读取软件参数和 AFE 硬件参数；
6. 硬件参数非法值被固件拒绝；
7. 写入后必须通过 readback/effective 验证；
8. AFE apply/readback 失败时 MOS 进入安全禁止状态；
9. Host contract + TC32 build + cppcheck 通过；
10. 实板完成 OV/UV/OC/SC/温度/通信故障与恢复矩阵后，才能标记为量产完成。
