# D008 IO、断电与 suspend SOC 实现说明

实现基于 `c789a3270fdf0df5da2d3f4a367eed38411d0d2d`（此前原理图/文档提交），目标分支 `refactor/d008-common-bms-features`。本文件随代码提交，具体构建 SHA 与结果以同提交 GitHub Actions 为准，未运行/排队/失败不能视为通过。

## 行为与边界

| 范围 | 当前实现 | 验收限制 |
|---|---|---|
| PA0 | `ACC_MCU_PIN`，输入保留；移除旧 key 读取及关钥匙 3 s 深睡眠 | 不新增 ACC 策略 |
| PB1 | 保留 `CHG_IN_PIN` 符号对应负载检测；取消 charger、MOS、suspend/PAD wake 业务读取 | 不新增负载策略；自动加热失去旧的错误充电源资格，暂不启动 |
| MOS | 每次提交产品 CHG+DSG ON 请求，由既有 guard、软件/AFE 保护和 output-enable 仲裁；不以反馈比较决定是否更新请求 | AFE driver flag 不是物理 Gate/Vgs 反馈 |
| 深度关机 | State 保存成功 → sleep 事件保存成功 → guard 禁止输出/加热 → balance OFF/FET OFF 命令成功 → AFE shutdown 命令成功 → 停止 AFE 访问 → PC4 LOW | shutdown 写返回成功不等于物理断电已测；sleep 事件记尝试，后续失败可能仍有记录 |
| 关机失败 | 任一步返回失败不切 MCU 电源；重试至少间隔 5 s，走既有 guard 恢复 | Flash/AFE 电气失败注入待板测 |
| 启动 | `gpio_init` 后尽早 PC4 HIGH 保持电源；复用 PD7/I2C 唤醒、reset、配置/读回及三帧 guard 资格 | 不改变现有厂商唤醒脉冲时序；电源保持时点待测 |
| suspend | SDK application wakeup 请求 200 ms 采样周期；callback 只置标志，I2C/SOC/Flash 留在主循环 | 固定 AFE current-wake/interrupt 策略不变；实际最坏延迟待测 |
| 退出 suspend | 精确有符号 mA，`>=500` 或 `<=-500`；无效/陈旧样本、OTA/Flash stack session、OWC 总线忙、BLE 连接、采样待处理也保持 active | 没有新增 GPIO 唤醒/阈值迟滞；电流噪声待实测 |
| SOC | guard 合格快照传递 mA/32k timestamp；实际时间积分；无效、重复、长间隔不能充当静置证据 | 最大年龄/间隔 400 ms 是两个名义采样周期的保守软件界限 |

既有低压关机电压及持续时间保留：<2550 mV 1 h；其余按 `__SLEEP_VLOW__/__SLEEP_TIMEVLOW__`、`__SLEEP_VNORMAL__/__SLEEP_TIMENORMAL__`。不再保留无 key/charger 或 AFE 失联后直接深睡眠路径。OTA、连接、总线事务、未合格测量阻断关机计时。正常电压下不会因 ACC/PB1 电平自动关机。

PC4 LOW 后若调试器等仍反向供电，主循环只进入有界 SDK suspend，不再跑 AFE/Flash/业务；真正复电依赖硬件并从冷启动恢复。未知断电时长不补积分，不继承 RAM 静置资格。

## SOC 数值与兼容性

- MCU 工程原有 SDK 32k 时间源按 32000 ticks/s；无符号相减处理回绕。电量单位 As×10，`abs(mA) * elapsed_ticks / (32000 * 100)`，64 位乘积保留余数。
- 默认 <200 mA 为 SOC 积分死区，200..499 mA 仍积分，不具备静置资格；500 mA suspend 门槛不改变 SOC 参数。
- 新样本首帧只建立时间基准；相同时标不重复积分；>400 ms 或无效帧清除计时/校准资格；恢复第一帧不补未知时间。
- 既有 200 ms 校准计数由实际 elapsed 换算，一帧最多处理两个片；方向切换清除旧连续资格。静置 600 s、压差 100 mV、8 mV 稳定条件、OCV ±5% 带及 30 min/1% 向下纠偏保留；只有合格充电满电锚点可向上。
- 两种 profile、协议地址/编码、Flash schema/分区、固定保护参数、source order、SDK/TC32 ABI 均不变。内部 C API/快照增加有效时间和 mA 参数，所有固件调用者同步更新；非 D008 后端不在本轮验收范围。

## 自动验证

执行 `python tests/d008_power_soc_host_check.py`：用 host C 编译器运行完整 `SocEnhance.c`，并对实际 PM 函数及完整 guard 做硬件/存储 mock。覆盖 LFP/NMC、200/250/400 ms 等电量积分、正负电流/死区、重复/缺失/无效帧、32 位回绕、静置资格中断、OCV 向下及有效满电向上；PM 覆盖 ±499/500/501 mA、事务阻断、持久化/关断失败、操作顺序与重试。

现有源码契约测试继续保留；不能把字符串检查描述成芯片运行测试。GitHub-hosted Ubuntu 运行 C 主机测试及 contracts；Windows self-hosted `telink-tc32` runner 用锁定 TC32 编译器执行 24S/20S × 1/1、1/0、0/1、0/0 共八种 clean build，各自 check-fw/size/MAP/manifest/verify，最终 24S 默认 1/1 再做 cppcheck。各变体 BIN/ELF/MAP/build.log/manifest 分目录留存，Actions artifact 保留 14 天，非正式发布归档。非 1/1 只准台架，不准量产。

## 未决与既有审核

`TODO_VERIFY_HW`：实际整机掉电/外部唤醒、I2C 唤醒波形、MOS Gate/Vgs、32k 精度、±500 mA 校准、调度最坏延迟、Flash 掉电失败、20S 装配/NTC。统一见 [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md)。

[原审核](D008_FULL_MODULE_AUDIT_2026-09-17.md)固定证据保持不改。此次移除了 F01 的请求与反馈比较路径，阻断 F04 的 OTA 关机路径，并对 F05 无效样本误校准增加运行测试。其他审核缺陷不在本次范围，尤其 F02 配置回滚与 F08 写失败计数问题不能据此宣称已解决。当前提交不构成量产放行，不执行烧录、合并或发布。
