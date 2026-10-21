# 当前规则：单一入口

本页只回答一个问题：**哪条规则现在仍然有效**。
它汇总当前结论并链接权威细节；具体寄存器、阈值、依据页码和验收边界仍以链接文档为准。
`docs/history/` 和带固定提交头的文档是当时的快照，不代表当前 HEAD。

规则变更时先改对应权威文档，再同步本页一行链接；本页不复制参数表。

## 1. 分层与所有权

| 层 | 当前职责 | 权威文档 |
|---|---|---|
| `bms/core` | 保护、SOC、参数与存储事务、诊断、协议解析、AFE 授权 | [ARCHITECTURE](ARCHITECTURE.md) |
| `bms/afe` | DVC1124 / SH3673510 两个后端的寄存器、测量、芯片配置与物理恢复 | [AFE_REFERENCE_GUIDE](AFE_REFERENCE_GUIDE.md) |
| `bms/platform` | GPIO/总线/Flash/时钟/PM/BLE 生命周期 | [BMS_MONOREPO](BMS_MONOREPO.md) |
| `bms/products` | 板级能力、参数与 `sources.txt` 编译归属 | [CONFIGURATION_AND_BUILD_GUIDE](CONFIGURATION_AND_BUILD_GUIDE.md) |
| `bms/app` | 固定顺序调度；不引入运行时驱动注册或动态分配 | [CODE_READING_GUIDE](CODE_READING_GUIDE.md) |

编译期只选一个 AFE 后端（`BMS_AFE_BACKEND`），没有运行时函数表。新增 MCU/AFE 时替换平台与后端实现，业务时序和协议单位保持不变。

## 2. AFE 通信、输出授权与故障确认

- 正常约 200 ms 完整读取；**不检测 ADC 活性、停更或"新样本次数"**；无转换完成事件也可更新测量。
- 持续普通通信异常 **1000 ms** 确认；关断未验证/配置未知时 DVC 静默 5 s、SH 静默 35 s，期间禁止访问 AFE。
- 已验证关断可在超过 200ms 后初始化；静默后的恢复也允许短重试，每轮最多三次且仅在 4s 窗口内安排，耗尽后重新静默。SH WDT 优先安全清除并回读，不能因一次清标志失败永久锁住采样。
- 恢复要求完整配置、完整测量和必要控制成功，**不再凑三次新转换**。
- 测量读失败：不发布、不积分、不作恢复证据，但不撤销已运行 MOS 资格；控制事务失败另行记录，禁止同轮新开。
- 配置不一致、明确芯片故障、真实保护和用户关闭始终生效；RAM 远程许可不能绕过保护。
- 用户已接受"通信成功但 ADC 停止更新"不可独立诊断。

权威：[AFE 通信确认与活性删除](AFE_COMMUNICATION_POLICY.md)、[AFE 硬件保护 v2](AFE_HARDWARE_PROTECTION_V2.md)。

## 3. 软件保护

- First/Second 只告警；Third 进入 CHG/DSG 阻断；Recover 是 Third 的回差。
- **触发与恢复是两种滤波**：触发按超限样本累积、正常样本递减；恢复要求连续满足。
- SC/OCD/OCC 不得用"关 MOS 后零电流"作为解除证据；必须负载/充电器移除或可靠反向电流等物理证据。
- 三级 `cell_delta_high` 同时阻断充电与放电；一二级仅告警。
- SOC Low 参与报告，不进入软件 CHG/DSG 阻断掩码。

权威：[软件保护](SOFTWARE_PROTECTION.md)。

## 4. 硬件保护与配置事务

- AFE 硬件保护为固定 **35-word** profile；历史持久值用 `validate`，新写入另用 `validate_write` 检查原厂保证范围，两者不可合并。
- 提交是"校验 → 持久化 → 应用 → 回读 requested/effective"事务；失败按原路径回滚，未验证前保持输出禁止。
- 请求、已写命令、芯片回读、诊断镜像分别保存，不能互相代替。
- 5-word/1-second 分片与完整 79 字节帧 CRC 属于写授权边界，不得简化成"收到即写"。

权威：[AFE 硬件保护 v2](AFE_HARDWARE_PROTECTION_V2.md)、[AFE 修复记录](AFE_AUDIT_REMEDIATION.md)。

## 5. 存储与 OTA

- Config/State/Event 三个独立 journal，**内部 schema 4**，payload 分别 414 / 24 / 202 bytes；不读取、不迁移旧参数格式。
- 旧板刷入必须走明确恢复出厂：有线擦除业务扇区，或 `factory_reset` 包；普通镜像遇非空白旧格式或损坏 Config 禁止输出。
- CPU 提交顺序 Config → State → Event；`0xFA57` pending 标记，三域提交完成前不取得输出资格。
- OTA 更新组与固定标识编入 BIN，一次性执行；保留模式不覆盖已有标识；不读取客户旧编号。
- 温度编码 `(°C+40)*10`，电流正放电负充电（mA），无效串位 `61001`。

权威：[STORAGE](STORAGE.md)、[OTA 参数更新](OTA_PARAMETERS.md)、[参数与电池类型切换](BATTERY_TYPE_SWITCH.md)。

## 6. SOC 与低功耗

- 200 ms 应用时间积分；只有合格样本推进，重复时间戳不重复积分；测量无效区间不补算。
- OCV 只允许向下校正，且有误差带与静置资格；Estimate 与 Display 分离。
- 压降保持、满/空端点锚定、SOH/循环计数是业务策略，不得为"减少代码"删除。
- 保护性深睡有独立计时（低压下档/上档、AFE 异常、固定过放），到期锁存后只收尾一次；普通 Suspend 与保护性深睡门禁不同。

权威：[SOC](SOC.md)、[低功耗策略](LOW_POWER_POLICY.md)、[休眠逻辑审查](SLEEP_LOGIC_REVIEW.md)。

## 7. 产品差异（不可合并项）

| 产品 | AFE | 关键差异 |
|---|---|---|
| D008 | DVC1124 | 16S LFP / 20S NMC / 24S LFP 三种 profile；PD4 加热保险丝、PB1 负载检测、ACC 休眠 |
| D011 | SH3673510 | 10S；PB5 fuse 安全低电平；片内 C+ 检测 |
| D013 | SH3673510 | 10S；**不支持 heater 与 balance**；原理图/BOM 仍缺 |
| D014 | SH3673510 | 8S；无 heater；TS3 NC、TS4 为 MOS NTC |

产品 reference：[D008](D008_PRODUCT_REFERENCE.md)、[D011](D011_PRODUCT_REFERENCE.md)、[D013](D013_PRODUCT_REFERENCE.md)、[D014](D014_PRODUCT_REFERENCE.md)。

## 8. 构建与验证入口

```powershell
python bms_tools/bms.py --all-products sources --check
python bms_tools/bms.py --all-products link --jobs 4
python bms_tools/bms.py --all-products resources
python tests/run_host_regression.py
```

`link` 不生成 BIN；`build`/`release`/`check-fw` 需要明确授权与产品签核。执行条件见 [AGENTS.md](../AGENTS.md#本机验证约定)，命令细节见 [构建与测试](BUILD_AND_TEST.md)。

## 9. 当前规则文档 vs 固定提交记录

**当前规则**（随代码维护，可以直接引用）：
[四产品 AFE 配置、保护/恢复、休眠与电流](AFE_PROTECTION_SLEEP_CURRENT.md)、
[ARCHITECTURE](ARCHITECTURE.md)、[STORAGE](STORAGE.md)、[SOFTWARE_PROTECTION](SOFTWARE_PROTECTION.md)、
[AFE_COMMUNICATION_POLICY](AFE_COMMUNICATION_POLICY.md)、[AFE_HARDWARE_PROTECTION_V2](AFE_HARDWARE_PROTECTION_V2.md)、
[SOC](SOC.md)、[OTA_PARAMETERS](OTA_PARAMETERS.md)、[BUILD_AND_TEST](BUILD_AND_TEST.md)、
[CONFIGURATION_AND_BUILD_GUIDE](CONFIGURATION_AND_BUILD_GUIDE.md)、[CODE_READING_GUIDE](CODE_READING_GUIDE.md)、
[RESOURCE_BUDGET_AND_VALIDATION](RESOURCE_BUDGET_AND_VALIDATION.md) 及四个产品 reference。

**固定提交记录**（开头带"固定提交记录"提示，提交、行数与测试数字只用于追溯）：
`SIMPLIFICATION_*`、`*_SIMPLIFICATION_*`、`REFACTOR_PHASE*`、`MAIN_CHAIN_REFACTOR_*`、
`OPTIMIZATION_*`、`ISSUE11_*`、`DEEP_SIMPLIFICATION_*`、`AFE_CONTROL_REFACTOR_*`、
`PROTECTION_RECOVERY_SIMPLIFICATION_*`、`OPENWIRE_*`、`docs/history/**`。

判断方法：文档开头有"固定提交记录"提示、或位于 `docs/history/`，就是快照；其余按当前规则阅读。
