# 7. DVC I2C / CRC / 地址审计

## 7.1 地址

DVC1124 数据手册说明典型固定地址：

```text
Write = 0x40
Read  = 0x41
```

这与 Telink B85 driver 使用 8-bit transfer address 的当前实现一致。不要再次把 `0x40` 右移成 `0x20` 再传给现有 Telink API。

DVC1124-22/-24 只有在 SRN/SRP 短接到指定电源并复用 GP 做硬线地址时才不同；D008 原理图把 SRP/SRN 用于分流器采样，所以当前 fixed mode 正确。

## 7.2 CRC

```text
Polynomial = x^8 + x^2 + x + 1 = 0x07
Initial    = 0x00
```

首个读取数据字节 CRC：

```text
CRC(AddrW, Reg, AddrR, Data0)
```

后续：

```text
CRC(DataN)
```

当前 `DVC1124_ReadRegisters/WriteRegisters` 实现正确。

## 7.3 超时

芯片自身一次 I2C 事务有约 64ms bus timeout。当前 MCU 侧又增加：

```text
DVC1124_I2C_CMD_TIMEOUT_US = 5000us
DVC1124_I2C_RETRY_COUNT    = 3
```

并在 NAK/超时后 reset I2C module。这是正确方向。

进一步需要：连续通信失败达到阈值后，不只“继续 retry”，而应切换 AFE health 状态并 inhibit 输出。

---

# 8. DVC 寄存器逐项审计

以下表是本报告的核心审计结果。

| Reg | 功能 | V1.2 真值 | 当前 D008 | 结论 | 完善要求 |
|---|---|---|---|---|---|
| 0x00 | ALARM / IWTF,COV,CUV,OCD1,OCC1,OCD2,OCC2,SCD | W0C：写0清除对应位，写1无效 | 已使用专用 ClearAlarmFlags，普通 raw 配置不直接写 | ✅ | 保持专用 API；故障恢复资格由 BMS 状态机判断后再清 |
| 0x01 | STATUS / CST | VADF/CC1F/CC2F 为 RC；CST=0xD复位寄存器、0xE Sleep、0xF Shutdown | 采样路径读取并缓存；命令值已按 V1.2 修正 | ✅ | 禁止普通诊断直接读取；Sleep/Shutdown 后必须重新验证数据新鲜度/配置 |
| 0x02..0x06 | CC1 / CC2 | CC2 为 signed 20-bit，0.3125uV/LSB；正=放电，负=充电 | 20-bit 符号扩展与 200uΩ 换算正确 | ✅/⚠️ | 200uΩ 下约 1.5625mA/LSB；仍需零点、增益、温漂和 CAMZ 策略实测 |
| 0x07..0x10 | VTOP/VPACK/VLOAD/VCT/V1P8 | VTOP/VPACK/VLOAD 12.8mV/LSB；VCT公式；V1P8 100uV/LSB | 换算与 V1.2 一致 | ✅ | 保持；加入边界与精度实测 |
| 0x11..0x1C | GP1..GP6 ADC | 100uV/LSB；模式由 0x74/0x75 决定 | NTC 电阻公式使用 FRT 修调后的 Rpu | ✅/⚠️ | NTC R-T 表需按 SNC103B13435F0603E 规格书/温箱确认 |
| 0x1D..0x4C | VC1..VC24 | 默认 CVS=0 时 100uV/LSB；高共模需二次校准 | 已实现 V1.2 p32 K 查表校准 | ✅ | 保持查表边界；20S/24S profile 由 cell mask 决定有效通道 |
| 0x51 | FET_CTRL | DSGC/CHGC: 00/01关、10自动体二极管、11开 | 普通 API 仅使用 00/11，实际状态读取 CC2 flags | ✅/⚠️ | 普通开关正确；若启用 Body Diode recovery，需扩展枚举而不是 bool API |
| 0x52 | PDWM / DPC | PDWM 控制 I2C WDT 对 PDSG；DPC 0..30 | DPC=16；PDWM 基本依赖复位值 | ⚠️ | D008 不用高边预放；应把“不使用高边预输出”的策略显式化，不依赖 reset |
| 0x53 | DSG_MASK | DWM/DDM/DPDM/DBDM 等，1=屏蔽对应关断/动作 | 仅 DWM 被运行配置显式管理，其它位主要沿用复位 | ⚠️ | 把 WDT/Body-Diode/预输出策略全部做成显式语义字段；不要靠复位值隐式决定 |
| 0x54 | CHG_MASK | CWM/CO1M/CO2M/CSM/CDM/CCM/CPCM/CBDM | 仅 CWM 显式管理；默认 CO1M/CO2M/CSM=1 保持反向充电能力 | ⚠️ | 显式写入“放电故障不关 CHG”等产品策略；Body Diode 未启用时 CBDM 明确保持1 |
| 0x55 | CADC_CTRL | HSFM=1屏蔽高边驱动；CAEW工作态 CADC；CAES休眠电流唤醒 | 当前默认 HSFM=0、CAEW=1、CAES=1 | ❌/⚠️ | HS-D008 主 CHG/DSG 为 NC，实际用 GP5/GP6 低边，建议 HSFM=1；CWT=0 时 CAES 也应默认0 |
| 0x56 | CC1_TIMING | C1OW/C1OS：工作0.5/1/2/4ms，Sleep 4/8/16/32ms | 语义配置默认 4ms/32ms，并通过 ConfigStoreApply 下发 | ✅ | 保持；若改变需验证功耗/唤醒/测量质量 |
| 0x59/0x5A | OCD1/OCC1 | code0=off，其余 code*0.25mV | 编码正确；200uΩ 时 1 code=1.25A | ✅/⚠️ | 当前默认 10A→code8 可精确表示，但不是已签核的 D008-100A 产品值 |
| 0x5B/0x5C | OCD1/OCC1 Delay | (code+1)*8ms | 采用“不晚于 requested”的 floor 量化 | ✅ | 100ms→96ms；协议必须显示 requested/effective |
| 0x5E | OCD2 | bit6 enable；bits5:0 threshold；bit7非 owned；(code+1)*4mV | enable=0x40，RMW 保留 bit7 | ✅ | 禁止退回 0x80 旧错误 |
| 0x5F | OCC2 | bit7 R/W 默认1但未公开业务语义；bit6 enable | RMW 只改 bit6+threshold，保留 bit7 | ✅ | 保持；禁止整字节 0x40\|code 覆盖 bit7 |
| 0x60/0x61 | OCD2/OCC2 Delay | (code+1)*4ms | 编码正确 | ✅ | 100ms→code24→100ms |
| 0x62/0x63 | SCD | SCDE=bit6；SCDT*10mV；delay=SCDD*7.81us | 编码正确，但默认 threshold=0 即关闭 | ⛔ | D008 发布前必须按短路实测确定；200uΩ 下每个 SCD code=50A |
| 0x65 | CWT Current Wake | 0=off，其余 code*10uV | 默认0 | ✅/⚠️ | 200uΩ 下 1 code=50mA；若保持0，CAES 应同步关闭以避免语义不一致 |
| 0x66 | BDPT Body Diode | 0=off，其余 code*40uV | 默认0 | ✅/⚠️ | 200uΩ 下 1 code=0.2A；若启用必须同时配置 DBDM/CBDM 和 FET mode=10 |
| 0x67..0x69 | Balance | CB24..1，约60s自动清零 | 写后读回，报告实际 AFE mask | ✅/⚠️ | 上层需要 <60s 有条件续期；必须考虑温升、测量间隙和数据有效性 |
| 0x6A..0x6C | Cell / measurement masks | CMn=1 关闭该节保护并默认关闭测量；另含 PKM/LDM/CTM/V1P8M | 24S不屏蔽；20S屏蔽21..24；misc mask 尚未完全建模 | ✅/⚠️ | 20S mask 正确方向；补齐 PKM/LDM/CTM/V1P8M 语义字段 |
| 0x6D | CPVS/COW/CMM/CVS | bits5:3 CPVS；bit2 COW；bit1 CMM；bit0 CVS；CPVS=101=10V | mask=0x38/COW=0x04/CMM=0x02/CVS=0x01，10V=0x28 | ✅ | 这是已修正关键项；COW 约1s自清，不能当持久状态 |
| 0x6E | VADC | VAE/VASM/VAMP/VAO；周期1/2/4/8 CC2；0.79/1.54/3.03/6.02ms | 默认 VAE=1、同步CC2、每1周期、1.54ms | ✅/⚠️ | 编码正确；结合均衡间隙、功耗和 200ms BMS 调度实测 |
| 0x70/0x71 | COV | code0=off；Vtrip=code*1mV+500mV；delay 200ms..8s | 编码正确；当前默认 3750mV/1s | ✅/⚠️ | 24S LFP 值仍需产品签核；20S NMC 绝不能复用 3750mV |
| 0x72/0x73 | CUV | code0=off；Vtrip=code*1mV；delay 200ms..8s | 编码正确；当前请求 3000mV/10s 会量化为 8s | ✅/⚠️ | requested/effective 必须分离；20S NMC 与24S LFP分 profile |
| 0x74 | GP1/2/3 Mode | GP1: OFF/NTC/analog/CON；GP2/3 支持 OFF/NTC/analog/INT/low-side pre-FET | 当前 GP1/GP2/GP3 均 NTC | ⚠️ | GP1 可作为板载 NTC；GP2/GP3 在 CN4 外部接口，需按 BOM/profile 决定，未装时建议 OFF |
| 0x75 | GP4/5/6 Mode | GP4 NTC；GP5=111 low-side CHG；GP6=111 low-side DSG | GP4 NTC、GP5 CHG、GP6 DSG | ✅ | 与 HS-D008 原理图一致 |
| 0x76 | Core OT | COTF=RC；COTT=0关闭，否则按公式设置 | 专用 RC-safe API；默认0关闭 | ✅/⚠️ | 访问方式正确；产品是否启用须热测试，不能拿外部 NTC 阈值替代 |
| 0x77 | V3P3 + I2C WDT | V3P3ES/EW/M；IWT=0/4/8/16/32s | V3P3工作/休眠开；WDT=0；timeout不关CHG/DSG | ⚠️ | WDT 为安全架构决策；如启用必须联动 0x53/0x54、软件 output inhibit 与通信恢复测试 |
| 0x78 | Timed Wake | 0关闭，10s..10min 离散值 | 默认关闭 | ✅ | 当前无明确需求，保持关闭 |
| 0x79 | Interrupt Mask | 1=屏蔽输出；0=对应事件产生1ms低脉冲 | 默认写0x00 | ⚠️ | 0x00不是“关闭中断”；当前 GP 未配置 INT 时无物理输出。若产品不用INT，建议显式0xFF |
| 0x7E | FRT | Rpu=6800+FRT*25Ω，只读 | 读取后用于 NTC 电阻换算 | ✅ | 保持 |
| 0x8F | Chip Version | 只读 CV | 用于 ready probe | ✅ | 保持 |
| 0x90 | V1.2 合法 R register | V1.2 新增，地址上限应到0x90 | REG_MAX=0x90 | ✅ | 禁止把上限退回0x8F；generic write 仍应由 access mask 阻止写RO |


---

# 9. 关键寄存器问题详解

## 9.1 0x55 HSFM：当前 D008 默认应修改

V1.2：

```text
HSFM=0 -> 允许高边 FET 驱动输出
HSFM=1 -> 屏蔽高边 FET 驱动输出
```

D008 原理图中主 `CHG` / `DSG` 为 NC，产品控制使用 GP5 / GP6 低边输出。因此建议：

```c
#define DVC1124_DEFAULT_HIGH_SIDE_FET_MASK 1u
```

这不是通用 DVC 驱动规则，而是 **HS-D008 board policy**。

## 9.2 0x53/0x54：不要再依赖 reset 隐式决定 MOS 行为

DVC reset 的 0x54 中 `CO1M/CO2M/CSM=1`，意味着放电过流/短路默认不会关闭 CHG；这恰好有利于“放电故障后仍允许充电恢复”。但量产代码不应因为“reset 刚好是这个值”就不配置。

建议在 `dvc1124_project_config.h` 或独立 FET policy 中明确：

```text
I2C timeout -> CHG 是否关闭
I2C timeout -> DSG 是否关闭
OCD1/OCD2/SCD -> CHG 是否保持可用
Body diode auto recovery -> enable/mask
PCHG/PDSG -> disabled/masked
DON/CON hardwire -> D008 是否使用
```

每次 AFE reset 后完整下发并 readback。

## 9.3 Current Wake：当前“发动机开、阈值关”不一致

当前：

```text
CAES = 1
CWT  = 0
```

而 CWT=0 已表示 current wake threshold 关闭。建议默认：

```text
CAES = 0
CWT  = 0
```

真正需要“接负载/产生电流唤醒”时，再同时设置：

```text
CAES = 1
CWT = n * 10uV
```

200uΩ 下每 code = 50mA，应根据实际静态噪声、漏电、车辆待机电流确定阈值。

## 9.4 0x79：0x00 不是“关闭中断”

V1.2 明确：mask bit 为 0 时，对应事件会在配置为 INT 的 GP 输出约 1ms 低脉冲；bit=1 才是屏蔽。

D008 当前 GP2/3/5/6 没有作为 INT，所以 0x00 目前不会形成实际中断输出，但配置语义容易误导。产品明确不用 INT 时建议：

```c
#define DVC1124_DEFAULT_INTERRUPT_MASK 0xFFu
```

若后续需要 AFE INT，再按事件逐位打开。

---

# 10. 保护量化：D008 必须区分 requested 与 effective

## 10.1 OC1

200uΩ：

```text
1 code = 0.25mV = 1.25A
10A -> 2mV -> code 8 -> 10A exact
```

## 10.2 OC2

```text
Vtrip = (code + 1) * 4mV
```

200uΩ：

```text
code0 -> 4mV  -> 20A
code1 -> 8mV  -> 40A
code2 -> 12mV -> 60A
...
```

因此：

```text
requested 15A
```

不可能由硬件准确表示；最低只能 20A。

现有 requested/effective 分离是正确的，必须继续保留：

```text
Requested OCD2 : 15.0 A
Effective OCD2 : 20.0 A
Quantized      : yes
```

更重要的是：HS-D008 标注 24S100A，现有 10/15/20A 保护默认是历史通用值，不能作为 D008 产品阈值。需要产品工程给出真实额定、峰值、MOS SOA、线束/保险丝能力后重新签核。

## 10.3 SCD

```text
Vtrip = SCDT * 10mV
```

200uΩ：

```text
code1 = 50A
code2 = 100A
code4 = 200A
...
```

这只说明“硬件可表示的网格”，**不代表建议量产值**。最终 SCD 必须根据：

- MOS 数量和 SOA；
- shunt/铜排/线束；
- 短路环路寄生；
- 保险丝；
- 实际短路峰值；
- 关断延迟；
- 车辆负载瞬态；

通过实板短路测试确定。

## 10.4 延时

```text
COV/CUV: 200..900ms, 1..8s
OC1:     (code+1)*8ms
OC2:     (code+1)*4ms
SCD:     code*7.81us
```

当前 floor 策略“尽量不晚于 requested”合理，但上位机必须显示实际值。例如：

```text
CUV requested 10s -> effective 8s
OC1 requested 100ms -> effective 96ms
OC2 requested 100ms -> effective 100ms
```

---

# 11. DVC 采样与换算审计

## 11.1 Cell Voltage

默认 `CVS=0`：

```text
LSB = 100uV = 0.1mV
```

高共模二次校准：

```text
K[n] = 1 + p1*VCM[n] + p2*VCM[n]^2
p1   = 0.35e-6
p2   = -0.12e-6
```

当前使用 V1.2 提供的 K 查表并以整数计算，方向正确。不要删除或用旧 SH AFE 系数替换。

## 11.2 CC2 Current

```text
signed 20-bit two's complement
positive = discharge
negative = charge
LSB      = 0.3125uV
```

当前为适应 TC32 32-bit 工具链使用等价定点公式，不依赖 `__muldi3/__divdi3`，应保留。

实板校准至少覆盖：

```text
0A, ±1A, ±5A, ±10A, ±20A, 大电流点
冷态/热态
充电/放电两个方向
```

## 11.3 NTC

DVC pull-up：

```text
Rpu = 6800 + FRT*25 ohm
Rntc = VGP_code * Rpu / (V1P8_code - VGP_code)
```

驱动公式正确。但 `SNC103B13435F0603E` 的 R-T 曲线不能仅凭历史表默认正确。量产前必须用器件规格书或温箱核对。

---

# 12. MOS / Fault Recovery：按 D013 框架重做 DVC 版本

建议 D008 引入以下状态变量：

```text
requested_charge_on
requested_discharge_on
output_enabled
output_inhibit
snapshot_valid
valid_snapshot_streak
comm_failures
reinit_cooldown
short_latched
charge_fault_latched
discharge_fault_latched
```

最终 FET 决策：

```text
actual_request
 = user/app request
 × output_enabled
 × !output_inhibit
 × snapshot_valid
 × AFE health
 × direction protection permit
```

### 通信失败

```text
任何完整采样失败
 -> output_inhibit=1
 -> 禁止新开 FET
 -> 记录 AFE_COMM fault
 -> 尝试安全关断
 -> 连续失败达到阈值后 AFE reinit/power cycle
```

### 恢复

```text
AFE reset/reconfigure OK
 -> 不立即恢复 MOS
 -> 连续 3 帧有效完整 snapshot
 -> 确认 fault/recovery 条件
 -> 才 release inhibit
```

### 短路

禁止：

```text
SCD -> MOS关 -> 电流变0 -> 立刻清SCD -> MOS重开
```

应使用：

```text
SCD latch
 -> 负载/开关侧条件稳定解除
 -> debounce
 -> clear W0C
 -> readback 确认 SCD 消失
 -> controlled retry
 -> 实际 FET readback
```

---

# 13. Body Diode 与方向恢复

DVC 原生提供：

```text
DSGC/CHGC = 10
BDPT != 0
DBDM/CBDM = 0
```

可以实现方向相反电流出现时自动打开对应 FET。但当前 D008：

- `BDPT=0`；
- bool `SetMosState()` 只表达 OFF/ON；
- body-diode mask 默认仍屏蔽。

所以当前实际采用软件仲裁更清晰。建议先把 D013 的 FET FSM 移植完成，再单独评估是否引入 DVC 原生 Body Diode。不能同时在多个业务文件中散落“强制开 MOS”特判。

---
