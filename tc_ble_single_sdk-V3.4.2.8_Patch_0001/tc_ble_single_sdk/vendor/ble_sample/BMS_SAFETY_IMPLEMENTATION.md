# BMS 多重保护整改实施记录

## 1. 结论

本次已在 `vendor/ble_sample` 中落地安全启动、统一测量、独立软件保护、诊断、唯一执行器仲裁、MOS 关断验证、保险丝状态机/持久化、安全日志、看门狗监督和通信上报。

两个硬件闸门保持安全默认：

- `BMS_POWER_PATH_HW_VERIFIED_ENABLE=0`：默认不开放 CTLC/CHG/DSG/PCHG。
- `BMS_FUSE_AUTO_TRIGGER_ENABLE=0`：默认不允许自动触发三端保险丝。

因此当前默认构建是“可验证但不放电/充电”的安全构建。完成第 20 节硬件验证和故障注入后，才能显式打开功率路径；保险丝还必须补齐反馈、授权和脉冲参数后才能启用。

## 2. 最终架构

```text
SH367309 CRC 帧 + MCU ADC
        │
        ▼
bms_measurement（统一单位、时间戳、有效位）
        │
        ├── bms_diagnostics（通道/交叉/冻结/突变/配置诊断）
        └── bms_sw_protection（不依赖 BSTATUS 的阈值状态机）
                         │
                         ▼
                  inhibit 原因位
                         │
                         ▼
              bms_actuator（唯一打开入口）
                         │
                         ▼
              CTLC + AFE CHG/DSG/PCHG
                         │
                         ▼
              bms_fet_monitor（断流闭环）
                         │ 关断失败 + 双证据 + 持续
                         ▼
                 bms_fuse（默认禁用）
```

`bms_supervisor` 管理启动和看门狗授权；`bms_safety_log` 先排队、后写 Flash，关断动作不等待日志；`bms_afe` 只管理 AFE 健康和配置，不决定业务开关。

## 3. 新增文件

- `bms_safety_config.h`
- `bms_measurement.c/.h`
- `bms_afe.c/.h`
- `bms_sw_protection.c/.h`
- `bms_diagnostics.c/.h`
- `bms_actuator.c/.h`
- `bms_fet_monitor.c/.h`
- `bms_fuse.c/.h`
- `bms_safety_log.c/.h`
- `bms_supervisor.c/.h`
- `bms_safety.c/.h`
- `tests/bms_safety_host_test.c`
- `tests/run_bms_safety_tests.sh`
- `project/.../vendor/ble_sample/bms_safety_sources.inc`

## 4. 修改文件

- `app.c/.h`
- `main.c`
- `param.c/.h`
- `modbus_rtu.c`
- `sif_send.c`
- `sh367309_datadeal.c/.h`
- `bms_cold_kv_store.c/.h`
- `flash_store_cfg.h`
- `project/.../825x_ble_sample/makefile`

## 5. 安全启动状态机

```text
RESET → IO_SAFE → AFE_WAIT_READY → AFE_CONFIG → AFE_VERIFY
      → SAMPLE_VALIDATE（连续 3 帧）
      → PROTECTION_CHECK → READY
任何关键失败 → LOCKED
```

上电先将 CTLC、MCC_C、RF_EN 拉到安全电平，CHG/DSG/PCHG 请求为关。AFE Reset、Ready、参数写入和参数镜像回读任一失败都不进入 READY。参数更新、配置漂移、OTA 结束、唤醒和旧采样失效都会重新进入连续采样验证；只有时间戳不同的新帧才计数，同一快照被主循环重复读取不会冒充连续帧。

B85 当前代码/头文件没有可确认的硬件复位原因 API，因此复位原因记为“未知并按可疑复位处理”，必须重新完成全部检查。另用掉电 KV 的 `BOOT_IN_PROGRESS` 和 `RESET_STREAK` 检测未稳定运行即再次复位；READY 稳定 60 秒后清除标记。

## 6. 软件保护参数与动作

单位：电压 mV，电流 mA，温度 0.1°C，时间 ms。下表由当前 `param.h` 默认值转换，不代表已确认的量产标定。

| 保护 | 进入值 | 恢复值 | 进入延时 | 恢复延时 | 动作 |
|---|---:|---:|---:|---:|---|
| 单体 OV | 3750 | 3500 | 100 | 1000 | 禁 CHG |
| 单体 UV | 3000 | 3100 | 1000 | 1000 | 禁 DSG |
| 总压 OV | 36500 | 35000 | 100 | 1000 | 禁 CHG |
| 总压 UV | 29000 | 30000 | 100 | 1000 | 禁 DSG |
| 充电 OC | 15000 | 10000 | 10 | 1000 | 禁 CHG |
| 放电 OC1 | 15000 | 10000 | 10 | 1000 | 禁 DSG |
| 放电 OC2 | 20000 | 10000 | 10 | 2000 | 禁 DSG、关 CTLC |
| 软件短路 | 500000 | 10000 | 0 | 锁定 | 禁 DSG、关 CTLC、锁定 |
| 充电高温 | 55.0°C | 50.0°C | 100 | 1000 | 禁 CHG |
| 充电低温 | 0.0°C | 3.0°C | 100 | 1000 | 禁 CHG |
| 放电高温 | 60.0°C | 50.0°C | 100 | 1000 | 禁 DSG |
| 放电低温 | -20.0°C | -10.0°C | 100 | 1000 | 禁 DSG |
| MOS 高温 | 95.0°C | 80.0°C | 100 | 2000 | 禁 CHG/DSG、关 CTLC |

软件短路只用于冗余、锁定和升级判断，不能替代 SH367309 的微秒级保护。

当前默认参数中部分软件层与 AFE 层没有足够裕量，且 15 A/20 A/500 A 的项目含义需要结合实际采样电阻和量产规格复核。代码没有自行编造新阈值，而是保留显式配置和 CRC 校验。

## 7. 参数管理

`PRT_E2ROM_PARAS` 新增结构版本、长度和 CRC32，并与参数本体在同一个 `flash_kv32_write_pairs` 事务中提交。软件保护表、诊断策略和保险丝策略也分别带版本、长度、CRC 和交叉校验；诊断/保险丝策略目前是编译期策略，不开放普通通信修改。校验包括：

- OV 恢复值低于触发值，UV 恢复值高于触发值；
- OC1 < OC2 < OC3；
- 温度恢复值在安全侧；
- 电压、延时范围；
- 软件短路阈值高于 OC2；
- 串数、板型和采样电阻配置诊断。

Modbus 0x06/0x10 先复制候选参数、整体校验、原子提交，失败返回异常码 `0x03`，不会污染当前参数。成功后立即全局禁止、重配/回读 AFE、丢弃旧采样并重新等待连续 3 帧。

普通 Modbus/BLE 路径没有保险丝触发接口。

## 8. 诊断

已实现并保留独立故障位：

- 单体范围、突变、冻结、数量/有效掩码；
- 单体和与 AFE 总压、独立 ADC 总压比较；
- AFE/ADC 总压范围和相互偏差；
- 电流原始值饱和、突变、冻结、静置零漂、充电器方向；
- NTC ADC 开路、短路、温度突变；
- 采样时间戳超期、AFE 帧失败；
- AFE 配置镜像漂移；
- 板型、串数、参数 CRC、采样电阻配置；
- 唤醒后旧采样强制失效。

MCU NTC 换算和 AFE NTC 换算均增加分母下限保护。

当前没有第二个真正独立电流通道、栅压、VDS 或 PACK 输出反馈，因此只能诊断明显零点、方向、饱和、冻结和关断响应异常，不能完整诊断采样电阻比例漂移，也不会在线修改采样电阻系数。

## 9. 执行器仲裁原因位

原因位包括：

`INIT, AFE_COMM, AFE_CONFIG, SAMPLE_INVALID, CELL_OV, CELL_UV, PACK_OV, PACK_UV, CHG_OC, DSG_OC1, DSG_OC2, SHORT, CHG_OT, CHG_UT, DSG_OT, DSG_UT, MOS_OT, FET_OFF_FAILED, WDT_RESET, FUSE_STATE, FACTORY, PARAM_INVALID, DIAGNOSTIC, OTA, HW_UNVERIFIED`。

方向禁止互不覆盖；OC1/OC2 使用独立原因位，恢复其中一个不会擦除另一个；全局禁止强制 CTLC/CHG/DSG/PCHG 全关；关闭优先；只有安全启动完成、所有相关原因清零且恢复保护时间完成后才可能打开。

## 10. 直接控制点迁移

- `open_ctlc/close_ctlc`：改为兼容请求，不再写 GPIO。
- `open_chg_close_dsg/open_dsg_close_chg/close_chg/open_dsg/close_dsg`：改为业务请求。
- 工厂模式、充电器检测、按键：只更新业务请求，仍受安全原因位约束。
- MOS 温度分支：旧的直接关/开 CTLC 状态机删除，改由软件保护原因位处理。
- 旧 RF_EN 保险丝原型：全部删除。
- `main.c`：不再无条件喂狗。
- AFE 初始化：只允许写安全关闭状态；运行期打开只存在于 `bms_actuator_hw_apply`。
- `init_bms_io` 中 RF_EN/MCC_C 直接写 0 是上电安全初始化，不存在拉高。
- `AFE_Sleep` 只在执行器已经全局关闭后设置 AFE SLEEP 位。

静态检索结果：业务模块没有 `AFE_CTL_PIN/RF_EN_PIN/MCC_C_PIN` 拉高，也没有 CHGMOS/DSGMOS/PCHMOS 置 1；唯一打开点是 `bms_actuator_hw_apply`。

## 11. AFE 增强

- Reset/Ready/配置写入/参数镜像回读全部返回明确结果；
- 非零错误均按异常处理；
- CRC 帧失败与采样超时独立上报；
- 每 10 秒巡检完整 `0x00..0x19` 参数镜像，以及 WDT/CADC/CHG/DSG/PCH 控制位；
- 漂移后先全局关断，再受控重写、回读、连续采样恢复；
- ALARM ISR 框架只置事件标志，默认仍周期轮询；
- AFE WDT、PF/二级保护、预充均有编译闸门且默认关闭。

ALARM GPIO、AFE WDT 的真实失效行为和 PF 引脚连接无法从当前代码确认，未擅自启用。

## 12. MOS 关断闭环

每个 CHG、DSG、CTLC 路径分别运行：

```text
IDLE → OFF_REQUESTED → GRACE(500 ms) → VERIFY
     → OFF_CONFIRMED
     → OFF_FAILED（危险电流连续 3 帧）
```

充电方向以 `current_ma > +2000` 判断，放电方向以 `< -2000` 判断。关断失败锁定 CTLC 并进入保险丝观察。反馈接口默认“不支持”，当前只能结合 AFE FET 逻辑位和电流判断，不能区分 MOS 击穿、驱动故障或外部反灌。

## 13. 保险丝状态机

```text
DISABLED → MONITORING → ARMED → TRIGGERING → FIRED
                                      └────→ FAILED
```

生产默认直接处于 `DISABLED`。即使将自动开关改为 1，还必须同时满足：

- 非工厂/调试/升级；
- 配置和采样全部有效；
- 严重状态持续 15 秒；
- 至少两个独立证据；
- FET_OFF_FAILED；
- 硬件支持、驱动正常、物理授权；
- 最大脉冲时间大于 0；
- ARMED 快照成功持久化。

当前硬件适配的 supported/authorized/driver_ok/feedback 全部返回 false，RF_EN 驱动无条件保持 0。`BMS_FUSE_MAX_PULSE_MS=0`，没有猜测脉冲参数。

## 14. 持久化

保险丝记录包含版本、长度、状态、原因、时间、最高/最低单体、总压、电流、温度、AFE 状态、复位原因、参数版本和 CRC32，使用 16 个 KV word 原子提交。FIRED/FAILED/TRIGGERING 复位后恢复为永久禁止，禁止重复触发。普通 cold-KV factory reset 会先保存再恢复保险丝记录。

安全事件日志使用原事件日志区域最后 2 个扇区（原事件日志改用前 6 个），记录测量快照、诊断位、活动保护位、三个 inhibit mask、AFE/复位状态、序号、CRC 和 commit；包含 FET 关断请求/确认/失败及 CTLC 状态变化。事件先入 4 项 RAM 队列，执行器刷新后每次写一条；队列满会计数丢弃，不阻止安全关断。

## 15. 看门狗监督

监督 AFE 采样、MCU ADC、保护、诊断、执行器、FET monitor、保险丝和安全日志心跳。安全采样任务 1.8 秒超时，其余关键任务 0.5～1.5 秒超时。只有关键任务全部出现且未超期才喂狗；初始化同步阶段允许 AFE 启动函数完成。日志失败不会阻止关断。

时间基准使用系统 tick 的无符号差值累计成毫秒，正确处理原始 32 位 tick 回卷，不依赖主循环次数。

## 16. 通信故障优先级

SIF 的两个故障字段改为单一优先级函数，不再被多个 `if` 覆盖：

`保险丝 > FET关断失败 > 短路 > OC2 > OC1 > OV/UV > 充电OC > MOS温度 > 充放电温度 > AFE > 采样/参数诊断`。

Modbus 实时区版本升级为 `0x0002`、扩展到 25 个寄存器，增加启动状态、主故障、全部活动保护位、诊断位、CHG/DSG/全局 inhibit mask、保险丝状态和复位原因。

## 17. 测试与静态检查

执行命令：

```sh
cd tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample
sh tests/run_bms_safety_tests.sh
python3 tests_flash_quick_check.py
```

结果：

- 生产默认 host 构建：PASS；
- 专用自动保险丝测试构建：PASS；
- C 测试源包含 91 个断言，两个构建分别覆盖默认禁用和完整触发分支；
- 既有 Flash/源契约测试：37 PASS，1 SKIP（缺少固件 bin）；
- `git diff --check`：PASS；
- 新安全模块无动态内存、无浮点；
- macOS Clang 以 `i386-none-elf` 目标对 17 个新增/修改接入文件做兼容语法检查：PASS。

覆盖正常启动、AFE 启动失败、连续帧、参数重验证、OV/UV/OC/温度、多故障、原因位隔离、采样范围/突变/总压不一致、NTC 开短路、方向、超时、FET 关断失败、保险丝默认禁用、单证据拦截、双证据武装、脉冲超时、反馈成功/失败、复位保持和防重复触发。

## 18. TC32 编译结果与资源

当前 macOS 环境没有 `tc32-elf-gcc/tc32-elf-ld/tc32-elf-size`，工程生成的 `subdir.mk` 还含 Windows `D:/...` 路径，GNU make 在本机解析原文件时即报错；仓库也没有基线 ELF/bin。因此不能诚实给出 TC32 最终链接、Flash/RAM 增量或 OTA bin 尺寸。

已把新增源加入 `bms_safety_sources.inc`。在原 Telink Windows 工具链中应执行：

```sh
cd project/tlsr_tc32/B85/825x_ble_sample
make clean
make all
tc32-elf-size -t tc_ble_single_sdk_B85.elf
```

必须在合入/烧录前补做该步骤，并保存修改前后 ELF 的 `tc32-elf-size` 和 map 对比。当前交付没有伪造资源数字。

## 19. 已知未解决项

- 未取得 TC32 目标编译器，尚无最终 ELF/资源增量；
- B85 可靠复位原因寄存器未确认，当前按未知可疑复位处理；
- AFE ALARM GPIO 未确认；
- 无独立电流通道、栅压/VDS/PACK 输出反馈；
- 多 NTC 一致性只能在新增第二独立 NTC 后启用；
- AFE WDT、PF、二级 OV/采样线断线的寄存器行为和硬件动作未确认；
- 当前软件/AFE 阈值层级裕量不足，需要安全工程师批准量产表；
- 关断电流阈值 2 A、宽限 500 ms、连续 3 帧是显式软件配置，必须结合采样周期/负载实测确认；
- 默认功率路径被硬件未验证闸门锁住，这是有意的安全状态。

## 20. 硬件待验证清单

1. SH367309 MODE；
2. PB6 与 AFE CTL 连接；
3. CTL 极性；
4. CTL 外部上下拉；
5. MCU 复位时 CTL 状态；
6. AFE CHG/DSG 与实际 MOS 连接；
7. MOS 拓扑和方向；
8. AFE 掉电默认输出；
9. PF 引脚连接；
10. PD4/RF_EN 与保险丝驱动连接；
11. RF_EN 极性；
12. RF_EN 外部下拉；
13. 保险丝触发电流；
14. 最大触发时间；
15. 保险丝反馈；
16. MCC_C 硬件作用；
17. PCHG 硬件和预充电阻；
18. 实际采样电阻值及并联数量；
19. Kelvin 走线；
20. MCU 总压 ADC 是否真正独立；
21. MCU NTC 是否真正独立；
22. 板型和串数硬件编码；
23. AFE 睡眠时保留的保护；
24. MOS 栅压/VDS/PACK 反馈；
25. MCU 异常时 AFE 是否继续供电。

## 21. 实机故障注入步骤

1. 保持两个硬件闸门为 0，先确认上电 CTLC/RF_EN/CHG/DSG 全关。
2. 断开 AFE、制造 Ready 超时、CRC 错误和参数回读不一致，确认不进入 READY。
3. 用校准源逐项越过/恢复单体、总压、温度和电流阈值，测量进入/恢复时间。
4. 同时注入两个故障，确认恢复一个不会清除另一个 inhibit。
5. 在故障活动时操作充电器、按键、工厂和通信命令，确认不能旁路。
6. 关 CTLC/CHG/DSG 后用受限电源模拟残余危险电流，确认 500 ms + 3 帧后 FET_OFF_FAILED。
7. 先用测试桩验证栅压/VDS/PACK 反馈，再接真实反馈。
8. 使用保险丝等效负载而非真实电池，验证授权、驱动电流、极性和最大脉冲。
9. 分别在 ARMED/TRIGGERING/FIRED/FAILED 期间复位，确认持久状态和 RF_EN 关断。
10. 最后才设置实际脉冲参数、实现硬件反馈/授权并评审是否启用自动保险丝。
11. 功率路径验证通过后设置 `BMS_POWER_PATH_HW_VERIFIED_ENABLE=1`，重复全部测试。
12. 自动保险丝仍保持 0，除非完成独立安全评审和破坏性试验。

## 22. 回滚

本次没有修改 SDK 库或删除原 AFE 快速保护。回滚时只回退本记录第 3、4 节列出的文件；不要擦除 Flash 中的保险丝 FIRED/FAILED 记录。若回滚固件不理解新记录，必须先由专用维修流程读取并确认，而不能通过普通恢复出厂设置清除。
