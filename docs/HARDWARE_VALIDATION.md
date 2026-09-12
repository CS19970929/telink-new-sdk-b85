# 硬件验证与未决项

本文件是唯一待测清单。每次实测要记录板号、DVC1124 型号/修订、固件 commit、参数、仪器、环境和结论；不要再创建新的 `TODO_*.md` 或日期型测试记录。

## 1. 发布阻断项

- [ ] 在固定 `tc32-elf-gcc 4.5.1-tc32-1.3` 环境 clean rebuild，检查 MAP/BIN、`check-fw` 和 `verify`。
- [ ] DVC1124 I2C 地址、CRC、超时、重试上限和通信故障 fail-safe。
- [ ] cell/pack/current/GP/NTC 在温度、电压和电流边界的精度与符号。
- [ ] COV/CUV/OCD1/OCC1/OCD2/OCC2 requested、量化、readback、延时、恢复和日志。
- [ ] SCD 阈值/延时和 MOS 实际动作；未验证前保持 `TODO_VERIFY_HW`/默认禁用。
- [ ] CHG/DSG FET、Body Diode、硬件 latch 与软件恢复无反复抢占。
- [ ] AFE reset、配置丢失、CRC/I2C 连续失败后的恢复和安全输出状态。
- [ ] Open Wire、Balance 60 s refresh、CADC calibration、WDT、Sleep/Wake。
- [ ] BLE 与 UART 单字段配置、非法值、Factory raw 门禁、失败异常和掉电恢复。
- [ ] Flash 写入中掉电、journal 切换、损坏记录、未知 schema、factory reset 和 upgrade epoch。
- [ ] OTA A/B、124 KB 上限、升级中断恢复，并确认 `0x74000..0x7FFFF` 未被擦除。

## 2. 低功耗和时间

- [ ] 测量正常运行、连接、广播、浅睡和 deep sleep 电流；目标 `<100 uA` 必须注明具体模式和唤醒源。
- [ ] 验证开关断开进入休眠，开关闭合或充电可可靠唤醒。
- [ ] 验证低压休眠计时、tick 回绕、复位和 deep sleep 期间的计时口径。
- [ ] 确认 runtime 只累计 awake 时间且 deep sleep 不补偿是否符合产品老化口径。
- [ ] 验证看门狗、BLE/UART 和 AFE 在唤醒后的恢复顺序。

历史需求中同时出现过“单体低于 2750 mV 且无充电 12 h”“低于 3000 mV 48 h”和“低于 2550 mV 48 h”三条规则。它们的阈值覆盖关系和优先级尚不明确，不能直接全部实现；产品确认后再形成单一状态机和测试向量。

## 3. SOC

- [ ] 校准电流零点、增益和方向，确认不同板型的 shunt/前端参数。
- [ ] 200 ms 积分周期与真实调度周期一致，无按 1 s 重复累计。
- [ ] 满电、静置、5/10/20 A 动态压降、3050 mV 和 3000 mV 端点。
- [ ] SOC/SOH/cycle 的上报单位、KV 写入频率、掉电恢复和 Flash 寿命。
- [ ] 低功耗前后 SOC 不因无效样本或时间补偿发生跳变。

## 4. 历史产品参数，仅作追溯

以下数据来自旧项目便笺，不是 HS-D008/DVC1124 默认值，不得自动写入当前板：

| 型号 | 串数/电流 | OCD1 | OCD2 | OCC | SCD |
|---|---|---:|---:|---:|---:|
| D002 / 32002276 / C11 | 13S 20A | 40 A / 1 s | 60 A / 600 ms | 20 A / 100 ms | 200 A / 256 us |
| D004 / 32002278 / D11 | 10S 15A | 30 A / 1 s | 50 A / 600 ms | 20 A / 100 ms | 200 A / 256 us |
| D004 / 32002279 / C700 | 10S 15A | 30 A / 1 s | 50 A / 600 ms | 20 A / 100 ms | 200 A / 256 us |

三者旧便笺还记录：COV 4250/4150 mV/1 s，CUV 2750/3000 mV/1 s，充电温度 55/45 °C 与 -7/0 °C，放电温度 75/60 °C 与 -20/-10 °C。应用前必须核对产品、传感器单位、DVC 可量化值和认证要求。
