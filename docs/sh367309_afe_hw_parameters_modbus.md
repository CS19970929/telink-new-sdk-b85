# SH367309 AFE 硬件保护参数 Modbus 对接文档

> 文档版本：1.0  
> 适用固件：`telink-new-sdk-b85` / `vendor/ble_sample`  
> 参考实现：`a002-c030` 的 `0x2400~0x2417` AFE 参数地址布局  
> 芯片依据：SH367309 数据手册  
> 用途：APP、PC 上位机、微信小程序等通过 Modbus RTU 读取/修改 SH367309 硬件保护参数。

## 1. 设计原则

AFE 硬件保护参数与 MCU 软件保护参数是两套独立参数：

- `0x2100~0x2140`：MCU 软件保护参数。
- `0x2400~0x2417`：SH367309 AFE 硬件保护参数。

写 `0x2100~0x2140` 不会触发 AFE EEPROM 更新；只有写 `0x2400~0x2417` 才会更新 AFE 参数。

SH367309 的很多参数不是连续可配值。例如过流阈值、短路阈值、保护延时均为芯片规定的离散档位。固件只接受能够 **精确编码到 SH367309 寄存器** 的值，不会自动向上/向下取近似档位。

因此 APP/上位机应优先使用下拉框、离散选项或受限步进输入，而不是让用户输入任意数值。

## 2. Modbus RTU 基本约定

| 项目 | 值 |
|---|---|
| Slave Address | `0x01` |
| Broadcast Address | `0x00`，设备执行但不回复 |
| 读保持寄存器 | Function `0x03` |
| 写单寄存器 | Function `0x06` |
| 写多个寄存器 | Function `0x10` |
| 寄存器字节序 | 16-bit 大端，高字节先发送 |
| CRC16 | Modbus CRC16，CRC 低字节先发送 |
| AFE 参数基地址 | `0x2400` |
| AFE 参数数量 | 24 个 16-bit 寄存器 |
| AFE 参数末地址 | `0x2417` |

## 3. 24 个 AFE 参数寄存器

### 3.1 总表

| 地址 | 参数名 | 线协议值 | 物理值/单位 | 合法值规则 | SH367309 EEPROM |
|---|---|---:|---|---|---|
| `0x2400` | Cell OVP | mV | 单节过充保护电压 | 3600~4500，5mV/step | OV：`02H[1:0]+03H` |
| `0x2401` | Cell OVP Recovery | mV | 单节过充恢复电压 | 3300~4500，5mV/step；必须 `< 0x2400` | OVR：`04H[1:0]+05H` |
| `0x2402` | Cell OVP Delay | 10ms | 过充保护延时 | 离散档位，见 4.1 | OVT：`02H[7:4]` |
| `0x2403` | Cell UVP | mV | 单节过放保护电压 | 2000~3100，20mV/step | UV：`06H` |
| `0x2404` | Cell UVP Recovery | mV | 单节过放恢复电压 | 2000~3600，20mV/step；必须 `> 0x2403` | UVR：`07H` |
| `0x2405` | Cell UVP Delay | 10ms | 过放保护延时 | 离散档位，见 4.1 | UVT：`04H[7:4]` |
| `0x2406` | Charge OCP Current A | 0.1A | 充电过流阈值 | 离散档位，见 4.2；与 `0x2408` 为同一 AFE 参数 | OCCV：`0FH[7:4]` |
| `0x2407` | Charge OCP Delay A | 10ms | 充电过流延时 | 离散档位，见 4.3；与 `0x2409` 为同一 AFE 参数 | OCCT：`0FH[3:0]` |
| `0x2408` | Charge OCP Current B | 0.1A | 兼容旧协议别名 | 必须与 `0x2406` 一致 | OCCV：`0FH[7:4]` |
| `0x2409` | Charge OCP Delay B | 10ms | 兼容旧协议别名 | 必须与 `0x2407` 一致 | OCCT：`0FH[3:0]` |
| `0x240A` | Discharge OCP1 Current | 0.1A | 放电过流1阈值 | 离散档位，见 4.2 | OCD1V：`0CH[7:4]` |
| `0x240B` | Discharge OCP1 Delay | 10ms | 放电过流1延时 | 离散档位，见 4.4 | OCD1T：`0CH[3:0]` |
| `0x240C` | Discharge OCP2 Current | 0.1A | 放电过流2阈值 | 离散档位，见 4.2 | OCD2V：`0DH[7:4]` |
| `0x240D` | Discharge OCP2 Delay | 10ms | 放电过流2延时 | 离散档位，见 4.3 | OCD2T：`0DH[3:0]` |
| `0x240E` | Charge OTP | `(℃+40)*10` | 充电高温保护 | 45~70℃，1℃/step | OTC：`11H` |
| `0x240F` | Charge OTP Recovery | `(℃+40)*10` | 充电高温恢复 | 40~70℃，1℃/step；必须 `< 0x240E` | OTCR：`12H` |
| `0x2410` | Charge UTP | `(℃+40)*10` | 充电低温保护 | -20~10℃，1℃/step | UTC：`13H` |
| `0x2411` | Charge UTP Recovery | `(℃+40)*10` | 充电低温恢复 | -20~15℃，1℃/step；必须 `> 0x2410` | UTCR：`14H` |
| `0x2412` | Discharge OTP | `(℃+40)*10` | 放电高温保护 | 45~80℃，1℃/step | OTD：`15H` |
| `0x2413` | Discharge OTP Recovery | `(℃+40)*10` | 放电高温恢复 | 40~80℃，1℃/step；必须 `< 0x2412` | OTDR：`16H` |
| `0x2414` | Discharge UTP | `(℃+40)*10` | 放电低温保护 | -40~10℃，1℃/step | UTD：`17H` |
| `0x2415` | Discharge UTP Recovery | `(℃+40)*10` | 放电低温恢复 | -40~15℃，1℃/step；必须 `> 0x2414` | UTDR：`18H` |
| `0x2416` | Short Circuit Current | A | 放电短路阈值 | 离散档位，见 4.5 | SCV：`0EH[7:4]` |
| `0x2417` | Short Circuit Delay | us | 短路延时 | 离散档位，见 4.6 | SCT：`0EH[3:0]` |

### 3.2 充电过流两个地址为什么是别名

旧 `a002-c030` 协议保留了“一级充电过流”和“二级充电过流”两组地址，但 SH367309 实际只有一组 OCCV/OCCT 硬件充电过流参数。

因此当前固件保持地址兼容：

- `0x2406` 与 `0x2408` 始终表示同一个 OCCV。
- `0x2407` 与 `0x2409` 始终表示同一个 OCCT。
- 单独写任意一个地址时，固件会同步另一个别名。
- 使用 `0x10` 一次同时写到两组地址时，两组值必须相同，否则整包返回非法数据，不修改任何参数。

APP 可以只显示一组“AFE 充电过流”和“一组延时”，读取时忽略第二组别名；为了兼容协议，写入整块参数时应保持两组相同。

## 4. SH367309 离散档位

### 4.1 OVP / UVP 延时 (`0x2402`, `0x2405`)

线协议单位是 10ms。

合法线协议值：

```text
10, 20, 30, 40, 60, 80, 100, 200,
300, 400, 600, 800, 1000, 2000, 3000, 4000
```

对应实际时间：

```text
100, 200, 300, 400, 600, 800, 1000, 2000,
3000, 4000, 6000, 8000, 10000, 20000, 30000, 40000 ms
```

例如：

- 寄存器写 `100` = 1000ms = 1s。
- 寄存器写 `50` 非法，因为 SH367309 没有 500ms 档位。

### 4.2 OCP 电流档位 (`0x2406`, `0x2408`, `0x240A`, `0x240C`)

Modbus 线协议单位：0.1A。

SH367309 内部不是直接存 A，而是存采样电阻两端的保护电压档位，因此可用电流值取决于硬件采样电阻配置。

固件使用：

```text
K = g_u32CS_Res_AFE = CS_Res_Num * 1000 / CS_Res
```

OCP 线协议值的精确合法条件：

```text
wire_current_0p1A = threshold_mV * K / 100
```

且右侧必须为整数。

SH367309 OCC / OCD1 电压档位：

```text
20, 30, 40, 50, 60, 70, 80, 90,
100, 110, 120, 130, 140, 160, 180, 200 mV
```

SH367309 OCD2 电压档位：

```text
30, 40, 50, 60, 70, 80, 90, 100,
120, 140, 160, 180, 200, 300, 400, 500 mV
```

当前工程默认 `FD_BMS_TYPE=D3PRO`，`CS_Res_Num=2`、`CS_Res=2`，所以 `K=1000`。

当前 D3PRO 的 OCC / OCD1 合法线协议值：

```text
200, 300, 400, 500, 600, 700, 800, 900,
1000, 1100, 1200, 1300, 1400, 1600, 1800, 2000
```

对应物理电流：20A, 30A, ... 200A。

当前 D3PRO 的 OCD2 合法线协议值：

```text
300, 400, 500, 600, 700, 800, 900, 1000,
1200, 1400, 1600, 1800, 2000, 3000, 4000, 5000
```

对应物理电流：30A, 40A, ... 500A。

注意：如果以后硬件采样电阻配置改变，APP 不应继续把上述 D3PRO 电流表当成 SH367309 通用值，应按产品硬件配置重新生成可选电流表。

### 4.3 OCC / OCD2 延时 (`0x2407`, `0x2409`, `0x240D`)

线协议单位是 10ms。

合法线协议值：

```text
1, 2, 4, 6, 8, 10, 20, 40,
60, 80, 100, 200, 400, 800, 1000, 2000
```

对应实际延时：

```text
10, 20, 40, 60, 80, 100, 200, 400,
600, 800, 1000, 2000, 4000, 8000, 10000, 20000 ms
```

### 4.4 OCD1 延时 (`0x240B`)

线协议单位是 10ms。

合法线协议值：

```text
5, 10, 20, 40, 60, 80, 100, 200,
400, 600, 800, 1000, 1500, 2000, 3000, 4000
```

对应实际延时：

```text
50, 100, 200, 400, 600, 800, 1000, 2000,
4000, 6000, 8000, 10000, 15000, 20000, 30000, 40000 ms
```

### 4.5 短路电流 (`0x2416`)

线协议单位：A。

SH367309 SCV 电压档位：

```text
50, 80, 110, 140, 170, 200, 230, 260,
290, 320, 350, 400, 500, 600, 800, 1000 mV
```

精确合法条件：

```text
wire_current_A = threshold_mV * K / 1000
```

当前 D3PRO `K=1000`，所以合法值为：

```text
50, 80, 110, 140, 170, 200, 230, 260,
290, 320, 350, 400, 500, 600, 800, 1000 A
```

### 4.6 短路延时 (`0x2417`)

线协议单位：us。

合法值：

```text
0, 64, 128, 192, 256, 320, 384, 448,
512, 576, 640, 704, 768, 832, 896, 960 us
```

## 5. 温度编码

为了兼容旧协议，8 个温度寄存器不是直接传有符号摄氏度，而是：

```text
wire = (temperature_C + 40) * 10
```

解码：

```text
temperature_C = wire / 10 - 40
```

例子：

| 温度 | 线协议值 |
|---:|---:|
| -40℃ | 0 |
| -20℃ | 200 |
| -7℃ | 330 |
| 0℃ | 400 |
| 45℃ | 850 |
| 55℃ | 950 |
| 80℃ | 1200 |

由于 SH367309 温度保护是 1℃/step，因此合法线协议温度值必须是 10 的整数倍。

## 6. 跨参数合法性约束

固件对单个参数范围以外还检查参数组合：

```text
Cell OVP Recovery       < Cell OVP
Cell UVP Recovery       > Cell UVP
Charge OTP Recovery     < Charge OTP
Charge UTP Recovery     > Charge UTP
Discharge OTP Recovery  < Discharge OTP
Discharge UTP Recovery  > Discharge UTP
Charge OCP Current A     == Charge OCP Current B
Charge OCP Delay A       == Charge OCP Delay B
```

这意味着修改一对有关系的阈值时，推荐使用 Function `0x10` 一次原子写入，而不是分两次 `0x06`。

例如当前 OVP=4250mV、OVR=4150mV，如果目标改成 OVP=4100mV、OVR=4000mV：

- 先用 `0x06` 写 OVP=4100 会失败，因为此时旧 OVR=4150 >= 4100。
- 应使用 `0x10` 从 `0x2400` 一次写入 `[4100, 4000]`，固件在 candidate 参数集上整体校验并一次提交。

## 7. Function 0x03：读取

### 7.1 推荐一次读取全部 AFE 参数

请求：

```text
01 03 24 00 00 18 4F 30
```

含义：

```text
Slave = 0x01
Func  = 0x03
Start = 0x2400
Qty   = 0x0018 = 24 registers
CRC   = 4F 30 (低字节在前)
```

成功响应格式：

```text
01 03 30 [48 bytes parameter data] CRC_LO CRC_HI
```

24 个寄存器按 `0x2400` 到 `0x2417` 顺序返回，每个寄存器高字节在前。

## 8. Function 0x06：写单个 AFE 参数

例如写 OVP=4250mV (`0x109A`)：

```text
01 06 24 00 10 9A 0E 91
```

合法时，设备按 Modbus 规范原样回显请求。

例如尝试写 UVP=2750mV：

- 2750 不是 20mV 可整除档位。
- 固件不会把它静默变成 2740/2760。
- 返回 Modbus Exception `0x03`。

异常响应：

```text
01 86 03 02 61
```

## 9. Function 0x10：原子写多个 AFE 参数

推荐 APP 在“保存/应用”时使用 `0x10`，尤其是阈值与恢复值一起修改时。

例如一次写：

```text
OVP       = 4250mV
OVP RCV   = 4150mV
OVP Delay = 100  (100 * 10ms = 1s)
```

请求：

```text
01 10 24 00 00 03 06 10 9A 10 36 00 64 E7 D9
```

成功响应：

```text
01 10 24 00 00 03 CRC_LO CRC_HI
```

### 9.1 事务语义

AFE 参数 `0x10` 是整包事务：

```text
当前参数 -> RAM candidate -> 应用本次所有寄存器 -> 全量校验 -> 持久化 -> 提交
```

只要本次写入导致任意一个 AFE 参数非法：

- 整包拒绝；
- RAM 当前参数不变；
- MCU Flash 参数不变；
- 不触发 AFE EEPROM 更新。

不会出现“前几个寄存器已经保存，后一个失败”的半更新状态。

如果一个 `0x10` 范围从 AFE 区外跨入或从 AFE 区跨出去，例如同时覆盖 `0x23FF` 和 `0x2400`，返回 Exception `0x02`，必须拆包。

## 10. Modbus 异常码

| Exception | 含义 | AFE 参数场景 |
|---:|---|---|
| `0x02` | Illegal Data Address | `0x10` 写入范围跨越 AFE 参数区边界 |
| `0x03` | Illegal Data Value | 数值不在芯片合法档位、范围非法、恢复关系非法、两个充电 OCP 别名不一致 |
| `0x04` | Server Device Failure | AFE 参数持久化到 MCU Flash 失败 |

广播地址 `0x00` 不返回响应，包括异常响应。

## 11. 参数保存和实际写入 SH367309 的时序

一次合法的 Modbus AFE 参数写入分为两层：

```text
Modbus 写入
  -> 全量合法性校验
  -> 持久化到 MCU Cold KV Flash
  -> 更新 RAM 参数
  -> AFE_PARAM_WRITE_Flag = 1
  -> 主循环异步执行 SH367309_UpdataAfeConfig()
  -> 构造 00H~18H EEPROM image
  -> 读取 SH367309 当前 image
  -> 只写发生变化的 EEPROM byte
  -> 每 byte 写后延时并回读验证
  -> 全 image 最终校验
  -> AFE Reset，使新参数生效
```

### 11.1 Modbus ACK 的含义

当前协议中，`0x06/0x10` 成功响应表示：

- 参数组合合法；
- 已成功持久化到 MCU Flash；
- 已排队等待/开始同步到 SH367309。

AFE EEPROM 的实际烧写在主循环中异步完成。因此成功 ACK **不是一个独立的“AFE EEPROM 已烧写完成”状态通知**。

固件内部会对 SH367309 写入进行逐字节回读和最终 image 校验；失败会进入 AFE 错误诊断。当前 `0x2400~0x2417` 协议没有单独的 apply-complete 状态寄存器。

APP 一般流程可以在写成功后稍等并重新读取 24 个参数用于确认应用层参数一致；如果后续需要产线级“AFE EEPROM 写完成/失败”闭环，建议另加只读 apply-status 诊断寄存器，不要改变现有 24 个参数地址。

## 12. EEPROM 寿命与 APP 操作要求

SH367309 数据手册给出的内部 EEPROM 编程/擦除次数有限（<=100 次）。固件已经采用：

- 先比较 SH367309 当前 EEPROM image；
- 只写发生变化的 byte；
- 值没有变化时不烧写；
- 每个变化 byte 写后回读验证。

但 APP/上位机仍必须避免高频修改真实参数。

推荐 UI 行为：

1. 进入页面时一次 `0x03` 读取 24 个参数。
2. 用户编辑只修改 APP 本地数据，不立即下发。
3. 对离散参数使用选择框，不使用连续 slider 实时写设备。
4. 用户点击“保存/应用”后，优先使用一个或少量 `0x10` 原子写入。
5. 写成功后再次 `0x03` 读取并刷新 UI。
6. 不要轮询写相同参数，不要把参数写操作当成周期同步机制。

## 13. APP/上位机推荐数据模型

建议将 UI 数据和线协议数据分开：

```text
AFEParamViewModel
  ovp_mV
  ovp_recovery_mV
  ovp_delay_ms
  uvp_mV
  uvp_recovery_mV
  uvp_delay_ms
  charge_ocp_A
  charge_ocp_delay_ms
  discharge_ocp1_A
  discharge_ocp1_delay_ms
  discharge_ocp2_A
  discharge_ocp2_delay_ms
  charge_otp_C
  charge_otp_recovery_C
  charge_utp_C
  charge_utp_recovery_C
  discharge_otp_C
  discharge_otp_recovery_C
  discharge_utp_C
  discharge_utp_recovery_C
  short_circuit_A
  short_circuit_delay_us
```

编码到 Modbus 时：

```text
voltage wire     = mV
delay(OV/UV/OCP) = ms / 10
temperature wire = (C + 40) * 10
OCP current wire = A * 10
SC current wire  = A
SC delay wire    = us
```

解码时执行反向转换。

## 14. APP 本地校验建议

APP 可以提前做相同校验以改善用户体验，但 **设备端校验始终是最终权威**。

特别注意：

- 电压不仅要在范围内，还要满足 5mV/20mV 步进。
- 延时必须从离散表中选择，不能只做 min/max。
- 电流必须从当前硬件采样电阻对应的离散表中选择。
- 温度 wire 必须是 10 的整数倍。
- 所有 recovery 和 protection 的大小关系必须同时满足。
- `0x2406==0x2408`、`0x2407==0x2409`。

## 15. 当前默认值（D3PRO）

当前工程 `FD_BMS_TYPE=D3PRO`。主要默认值为：

| 参数 | 默认线协议值 | 物理值 |
|---|---:|---:|
| OVP | 4250 | 4250mV |
| OVP Recovery | 4150 | 4150mV |
| OVP Delay | 100 | 1s |
| UVP | 2740 | 2740mV |
| UVP Recovery | 3000 | 3000mV |
| UVP Delay | 100 | 1s |
| Charge OCP | 200 | 20A |
| Charge OCP Delay | 10 | 100ms |
| Discharge OCP1 | 300 | 30A |
| Discharge OCP1 Delay | 100 | 1s |
| Discharge OCP2 | 500 | 50A |
| Discharge OCP2 Delay | 60 | 600ms |
| Charge OTP | 950 | 55℃ |
| Charge OTP Recovery | 850 | 45℃ |
| Charge UTP | 330 | -7℃ |
| Charge UTP Recovery | 400 | 0℃ |
| Discharge OTP | 1150 | 75℃ |
| Discharge OTP Recovery | 1000 | 60℃ |
| Discharge UTP | 200 | -20℃ |
| Discharge UTP Recovery | 300 | -10℃ |
| Short Circuit Current | 200 | 200A |
| Short Circuit Delay | 256 | 256us |

## 16. 固件实现位置

主要代码：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/
  sh367309_datadeal.h
  sh367309_datadeal.c
  modbus_rtu.c
  bms_cold_kv_store.h
  bms_cold_kv_store.c
  app.c
```

关键接口：

```c
u8 SH367309_AfeParamLoad(void);
u8 SH367309_AfeParamReadReg(u16 reg, u16 *value);
sh309_afe_param_result_t SH367309_AfeParamWriteRegs(u16 start_reg,
                                                    const u16 *values,
                                                    u16 count);
u8 SH367309_AfeParamValidate(const AFE_Parameters_RS485_Typedef *params);
```

## 17. 对接验收建议

至少覆盖以下用例：

1. `0x03` 从 `0x2400` 一次读取 24 个寄存器。
2. `0x06` 写合法 OVP，回显成功，重启后值仍存在。
3. `0x06` 写 OVP=4251，收到 `0x03` 异常且原值不变。
4. `0x06` 写 UVP=2750，收到 `0x03` 异常且原值不变。
5. `0x10` 同时修改 OVP/OVR，满足恢复关系时成功。
6. `0x10` 其中一个值非法时，全部参数保持原值。
7. 同时写 `0x2406` 和 `0x2408` 为不同值，收到 `0x03`。
8. 写合法参数后断电重启，MCU Flash 参数仍保持。
9. 写合法参数后检查 SH367309 EEPROM 对应字节与目标编码一致。
10. 连续写相同值，确认固件不重复烧写 SH367309 EEPROM。

---

本协议的核心约束是：**APP 写的是物理工程值，但这些值必须精确落在 SH367309 的硬件可表示档位上；设备端绝不自动近似。**
