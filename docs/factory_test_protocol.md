# BMS Factory Test（RAM-only）

## 目的与边界

`vendor/ble_sample/factory_test.c` 提供独立功能码 `0x41`，用于出厂测试时向软件保护算法提供临时测量值。注入只在 RAM 中保存，并由 `soft_protect_update_all()` 读取；AFE 原始快照、MCU ADC 采样值、正式保护参数和 Flash 均不被修改。

Factory Session 在 BLE 建链和断链时清除，心跳超时为 8 秒，复位后静态 RAM 状态重新初始化。`OPEN` 需要 magic `0x46414354`（大端），服务端返回随机 token；除 `OPEN` 外的命令都必须携带 token。`CLEAR` 清除所有注入，`CLOSE` 清除并结束会话。

## 帧格式

帧使用现有 Modbus RTU CRC16（多项式 `0xA001`、初值 `0xFFFF`、CRC 低字节在前）：

```text
请求：地址(1) 功能码(0x41) 命令(1) 参数(N) CRC(2)
应答：地址(1) 功能码(0x41) 命令(1) 状态(1) 负载(N) CRC(2)
```

地址沿用 `modbus_rtu.c` 的 `MB_ADDR=0x01`。命令如下：

| 命令 | 参数 | 成功负载 |
| --- | --- | --- |
| `0x01 OPEN` | magic `u32` 大端 | token `u16`、超时秒数 `u16`、协议版本 `u8`、串数 `u8` |
| `0x02 HEARTBEAT` | token `u16` | token、剩余秒数 |
| `0x03 INJECT` | token、类型 `u8`、索引 `u8`、值 `u16` | token、注入 mask |
| `0x04 CLEAR` | token | token |
| `0x05 CLOSE` | token | 无 |
| `0x06 STATUS` | token | 有效测量值、L1/L2/L3 故障字、MOS 状态 |

`INJECT` 请求固定为 11 字节（地址、功能码、命令、token、类型、索引、值和 CRC）。带有效 token 的 `HEARTBEAT`/`INJECT`/`CLEAR` 即使返回错误状态仍保留其定义的 token 负载；鉴权失败等未进入命令处理的响应才使用无负载短帧。

注入类型：`1 cell mV`、`2 pack cV`、`3 current 0.1A`（有符号 `int16` 编码，正值充电、负值放电）、`4 温度原始值`、`5 MOS 温度原始值`、`6 SOC 百分比`。单体注入会重新计算有效单体最大值、最小值、压差和总压；显式 pack 注入优先于计算总压。

## 软件保护映射

算法实际读取 `g_stCellInfoReport` 的派生测量字段，并在测试会话有效时由 `factory_test_get_effective_measurement()` 提供替代输入：单体最大/最小、总压、压差、充/放电流、温度最大/最小、MOS 温度和 SOC。正式保护参数仍从 `g_tParam.protect` 读取，测试程序只读 `0x2100..0x2140` 的 65 个 word。

故障位沿用 `MDLCHGFAULT_BITS`：单体 OVP/UVP、总压 OVP/UVP、充/放电 OCP、充/放电高低温、压差、SOC 低和 MOS 高温。软件三级保护继续参与 `mos_update()` 的 MOS 关断判定；Factory Session 仅使能 RAM-only 测试请求，不进入会写 Flash 的持久化 factory mode。

## 构建与真机测试

在仓库根目录执行：

```text
python bms_tools/bms.py sources --check
python bms_tools/bms.py static
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py verify
```

可烧录文件为 `project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin`。当前自动构建仅证明 TC32 编译、链接和协议静态检查通过；仍需在目标板上确认 BLE 分包、AFE 正常状态、实际 MOS 驱动反馈以及断连/复位后的清理行为。
