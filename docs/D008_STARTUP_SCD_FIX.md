# D008 无放电启动 SCD 与三级放电过流定位

2026-10-13，基线 `65ae7125`，独立分支 `codex-d008-false-overcurrent`。
本记录只覆盖启动配置顺序，不改变保护阈值、持久参数、协议、Flash 或运行恢复条件。

## 现场证据

用户确认 20S、50Ah 设备实际没有放电；多板出现上电三级放电过流及 CBC 历史事件。
经 Android 无线 ADB 读取既有 BLE Modbus 应答：

- 充放电报告均为 0，诊断电流约 -92 mA，处于既有 ±200 mA 不可信区间。
- 当前三级故障字为 `0x0020`，后端放电阻断为 `DIAG_BLOCK_HW`，不是软件过流滤波。
- DVC 原始 `0x00=0x01`，`0x62=0x44`，`0x63=0x20`。
- 软件为 V1.8，Build ID 为 `f9147f86`、dirty；设备序列号读回为 D008-20261019。

只增加启动只读探针的开发包经 BIN/CRC/SHA-256/输入溯源校验后，通过手机 OTA。
手机收到 `OTA_SUCCESS`，重连后读到以下四阶段快照；探针已经从最终实现删除。

| 阶段 | ALARM | SCD 配置 | SCD 延时 |
|---|---|---|---|
| 复位并等待 300 ms 后 | 0x01 | 0x40 | 0x00 |
| 基础配置后 | 0x01 | 0x40 | 0x00 |
| 正式保护配置后 | 0x01 | 0x44 | 0x20 |
| 启动校准后 | 0x01 | 0x44 | 0x20 |

这证明正式参数下发前已存在 SCD，而正确参数下发没有解除它。
本次未测量比较器输入或确定 SCD 首次置位的模拟瞬态；不能从数字寄存器读回声称已测得该瞬态。

## 软件链与原厂依据

`dvc_recover_current_faults()` 的放电硬件掩码包含 SCD：任一放电硬件标志都会建立后端
`s_current_recovery.discharge`，并发布 `fault_third.discharge_ocp`。
`dvc_merge_hw_faults()` 又将 SCD 映射到 `BMS_ERROR_CBC_DSG`，每秒事件任务记录 CBC。
因此同一个 SCD 可以同时产生 CBC 和“三级放电过流”，不要求软件测量电流超过三级阈值。
该锁存按已有可靠反向电流条件恢复，零电流不会自动解除。

- [DVC1124-2 RM V1.2](../references/afe/AFE_完整开发参考库_20261005/sources/DVC1124-2参考手册_V1.2.pdf)，原 PDF 第 6 页：ALARM bit0 为 SCD，W0C。
- 同手册第 20 页：复位 `SCDE=1`、`SCDT=0`、`SCDD=0`；非零阈值为 `SCDT×10 mV`。
- [DVC1124-2 DS V1.1](../references/afe/AFE_完整开发参考库_20261005/sources/DVC1124-2数据手册_V1.1.pdf)，原 PDF 第 19 页：SCD 锁存可由 SCD 写零或 SCDE=0 解除。

原初始化没有先撤销复位默认 SCD 使能，直接将正式值写为 `0x44/0x20`，把启动标志带入正常保护。
当前证据支持这个配置/锁存路径；未发现能够解释现场的 20S 数组越界。
本轮没有用 Flash 余量或静态 RAM 余量推断运行时栈安全。

## 最小修复

`dvc_apply_basic_config()` 开始配置前：

1. 保留 FET 控制的其它位，关闭预充、预放、主充、主放四路命令，并使用原安全写入/回读。
2. 读取 R6 四路输出标志；任何一路尚未关闭或通信失败，返回失败，不撤销 SCD。
3. 仅撤销 SCDE，按原厂语义解除复位默认 SCD，并读取 ALARM 确认 SCD 已清除。
4. 基础配置成功后才允许执行原独立硬件 profile；其中仍写入并开启正式 SCD。

该步骤不在正常运行的保护参数应用入口执行；不清其它告警，不修改后端物理恢复状态。
同一次 MCU 运行中 AFE 重初始化仍保留既有软件锁存；新故障和恢复条件保持。
失败时 `s_need_config` 保持，快照无效，guard 继续禁止输出；没有增加阻塞等待或 ISR 操作。

## 软件验证与现场验收口径

新增 `tests/dvc_startup_protection_host_check.py` 使用真实基础配置、初始化调度、安全写入及 SCD
编码函数。修复前得到 `expected ALARM=0x24 actual=0x25`；修复后通过，覆盖其它告警保留、
正式启用后新 SCD 保留、四路反馈拒绝、SCD 未解除、逐总线访问失败及失败时禁止应用 profile。

本轮相关检查通过：新启动回归、`dvc_adc_events_host_check.py`、`d008_backend_contract_check.py`、
`dvc1124_fixed_policy_host_check.py`、D008 `sources --check`、Git 差异检查。
既有 `dvc_register_scenarios_host_check.py` 的 OCD1 固定期望 8 与当前默认编码 64 不一致；
其调用的保护配置和 OC1 编码函数与基线完全相同，本次不修改产品参数或掩盖该失败。

修复包采用 20S NMC 开发配置、OTA 更新组为 0，保留设备全部持久参数。
现场验收需同时记录 OTA_SUCCESS、重连设备身份/Build ID、当前 ALARM、三级故障及正式
SCD 阈值/延时读回。已有 Flash 历史事件会保留，不能以旧 CBC 记录仍存在判定当前故障未解除。
本机原始日志、探针源码/BIN/manifest 和修复包验证证据保存在用户级 CodexTemp；具体结果以本次交付说明为准。
一次设备 OTA 验证不替代其它板复电、真实短路动作/恢复、Gate/Vgs 或完整量产验收。
