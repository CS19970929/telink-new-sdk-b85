# D008 加热 / 均衡安全闭环设计

> 分支：`refactor/d008-common-bms-features`
>
> 适用硬件：HS-D008 + TLSR8251 + DVC1124-2
>
> 本文记录 2026-09-19 已确认的产品需求、软件状态机、安全边界和实板验收项。涉及 DVC 寄存器事实以 DVC1124-2 Reference Manual V1.2 为准；未有硬件证据的项目保留 `TODO_VERIFY_HW`。

## 1. 设计目标

加热和均衡都是安全相关功能，必须同时满足：

- **Safety**：无可靠条件时不得误加热、误均衡。
- **Liveness**：条件满足时不能因为 CHG MOS 已关、充电电流消失、OVP 暂停充电等原因永久无法加热或均衡。
- **Fail-safe**：AFE/采样/NTC/通信失效时，加热和均衡默认退出。
- **方向解耦**：禁止充电不能破坏合法放电；禁止放电不能破坏合法充电。
- **Requested / Actual 分离**：软件请求不等于 AFE 实际输出，不得用请求值伪造硬件状态。

## 2. D008 充电会话

D008 的 DVC 产品路径没有外部电池 NTC 硬件低温自动关 CHG 的先行保护，因此允许使用**可靠充电电流**作为充电会话进入事件。

当前电流有效方向判定沿用项目约束：

- `abs(current) <= 200 mA`：不可靠区，不作为充/放方向证据；
- 可靠充电电流出现：置 `charge_session_active=1`；
- 可靠放电电流出现：立即清除 charge session，并关闭 Heater；
- 预热主动关闭 CHG 后 `Ichg=0` 属于预期行为，不能据此清除 session；
- PB1 是负载检测，不得作为 charger-present。

### 2.1 防止陈旧 session 误加热

仅有历史 `charge_session_active=1` 不足以从 `HEATER_IDLE` 发起新一轮加热。

`IDLE -> ARMING` 必须满足：

- 当前帧有可靠充电电流；或
- 将来加入了经过硬件确认的独立 charger-present 物理证据。

原因：用户可能正常充电后拔掉充电器并保持空载，电流为零时软件无法仅凭电流清除历史 session。如果后续环境降温，不允许因为陈旧 session 再次启动 Heater。

## 3. Heater 状态机

状态：

- `BMS_HEATER_IDLE`
- `BMS_HEATER_ARMING`
- `BMS_HEATER_ACTIVE`

### 3.1 IDLE -> ARMING

必须同时满足：

- Heater 功能使能；
- AFE / NTC / 参数有效；
- 无 heater hard fault；
- 当前存在新鲜 charger evidence；
- Tmin 满足加热需求。

进入 ARMING 后：

- Heater 保持 OFF；
- 充电方向被阻断；
- DVC common-port 映射为 `CHG=AUTO_DIODE, DSG=ON`；
- 不允许低温充电与加热同时存在。

### 3.2 ARMING -> ACTIVE

只有后续有效采样确认：

- `Ichg == 0`；
- 加热需求仍存在；
- 安全条件仍满足；

才允许 Heater ON。

### 3.3 ACTIVE

- 使用 GP2 / GP3 最低温作为电池温度；
- 达到 Heater stop 后退出；
- 可靠放电电流出现时立即退出 session 并 Heater OFF；
- OpenWire 诊断期间 Heater OFF；
- AFE、NTC、参数、严重短路/过流/高温、Heater 回路异常时 fail-safe OFF。

### 3.4 Heater hard fault 与低温保护解耦

充电低温保护本身不能成为 Heater hard fault，否则会形成：

`低温 -> Charge UTP -> 禁止 Heater -> 永远无法恢复充电`。

Heater hard fault 应只包含真正不允许加热的故障，如 AFE/NTC 失效、Heater 回路异常、严重短路、过流、高温、参数无效等。

## 4. Common-port FET 仲裁

D008 使用 DVC GP5/GP6 低边 CHG/DSG 驱动。

方向控制语义：

| 业务状态 | CHG | DSG |
|---|---|---|
| 正常 | ON | ON |
| 仅禁止充电 | AUTO_DIODE | ON |
| 仅禁止放电 | ON | AUTO_DIODE |
| Hard isolate | OFF | OFF |

DVC 固定 `BDPT=80 uV`，Rsense=200 uOhm，名义反向续流门槛约 0.4 A。该参数属于固定板级 Fail-safe，不是客户参数。

要求：

- 单侧保护直接一次写入最终 AUTO_DIODE + opposite ON；
- 禁止每 200 ms 先 OFF 再切 AUTO_DIODE；
- 禁止因为 AFE driver flag 与 Requested 不一致而反复写同一模式；
- OpenWire、通信 inhibit、shutdown、显式输出关闭等 hard block 必须双 FET hard-off，不得被 AUTO_DIODE 绕过。

## 5. 加热过程中拔插与弱充电器

### 5.1 加热中转为放电

若加热中出现可靠放电电流：

1. 清除 charge session；
2. Heater 立即 OFF；
3. 保持合法放电方向；
4. 通过 CHG AUTO_DIODE / DVC 续流机制避免长期走 body diode。

### 5.2 拔充电器且完全空载

仅靠 `Ichg=0 && Idsg=0` 无法区分：

- CHG 已主动关闭但充电器仍连接；
- 充电器已拔出且系统空载。

因此该场景必须：

- 实板证明 Heater 供电路径不会由电池反供；或
- 增加第二 charger-present 物理证据。

在此之前保持 `TODO_VERIFY_HW`，不得宣称仅靠电流已经完全闭环。

### 5.3 充电器功率不足

弱充电器/限流/打嗝电源不能降级为“允许低温充电”。

应验证：

- Heater ON 后是否出现电池持续反向放电；
- 输入是否塌陷；
- MCU/AFE 是否反复复位；
- 温度是否按合理速率上升；
- GP1 是否异常升温。

必要时后续再引入加热功率分档/PWM；第一版优先保证失败时 Heater OFF、Charge 仍禁止。

## 6. 均衡参数

Balance 与软件压差保护参数彻底解耦，不再使用 `u16VdeltaOvp_*`。

独立参数：

- `balance_enable`
- `balance_start_mv`：可配置，当前默认 3400 mV
- `balance_start_delta_mv`：默认 **50 mV**
- `balance_stop_delta_mv`：默认 30 mV，必须小于 start delta

当前寄存器：

- `0x2E70` Balance enable
- `0x2E71` Balance start voltage
- `0x2E72` Balance start delta
- `0x2E73` Balance stop delta

当前 Config schema 4，参数协议 v2。

默认值只是当前开发基线，量产值必须结合电芯规格、AFE精度、均衡电流和热测试签核。

## 7. 均衡前必须证明电压可信

禁止简单执行：

`DeltaV >= 50mV -> Balance`

因为断线、接触不良、采样瞬态、通信异常都可能制造假压差。

建立 `balance_voltage_trusted` 门禁，至少要求：

- AFE snapshot 有效且新鲜；
- cell count 有效；
- 每节电压在物理合理范围；
- 软件重算 min/max/delta 与发布值一致；
- 单节相邻帧无超过 sanity limit 的异常跳变；
- 总压差未进入明显异常区；
- 连续稳定样本达到确认时间；
- OpenWire 非 active / suspected / confirmed；
- Heater 非 ARMING / ACTIVE。

任何可信度失败：

1. 立即请求 Balance OFF；
2. 清除 voltage trusted；
3. 置 `openwire_suspected`；
4. 条件允许时优先运行 OpenWire；
5. 诊断健康后重新累计稳定样本，不能立刻恢复均衡。

## 8. DVC OpenWire 与均衡

DVC COW 诊断：

- COW 拉低刺激窗口约 1 s；
- 当前实现在约 200 ms 后、COW 仍有效时取诊断样本；
- 按手册流程，断开的采样输入在刺激期间诊断值为 0 mV；
- 不自行发明非零断线阈值。

OpenWire active / suspected / confirmed 全部禁止 Balance。

若实板结果与手册流程不一致，必须重新签核判据，不能为了“容易通过”放宽成经验阈值。

## 9. 均衡业务策略

第一版使用**充电会话中的顶部被动均衡**。

允许条件：

- Balance enable；
- voltage trusted；
- charge session active；
- Heater IDLE；
- OpenWire 健康；
- 温度安全；
- 无 balance hard fault；
- `Vmax >= balance_start_mv`；
- `DeltaV >= start/stop delta`。

选中电芯：

- `Vcell >= balance_start_mv`；
- `Vcell - Vmin >= 当前 delta threshold`。

回差：

- 未均衡：使用 start delta（默认 50 mV）；
- 已均衡：使用 stop delta（默认 30 mV）。

## 10. COV 与均衡恢复

Cell OVP 不应默认作为 Balance hard block。

在满足采样可信、温度安全、无 OpenWire 等条件时，应允许：

`高单体 -> COV 停止充电 -> 继续被动泄放高单体 -> COV recover -> 恢复充电`

因此不能再用一个 `major_fault()` 把所有保护一刀切禁止均衡。

必须禁止均衡的典型故障：

- AFE/通信异常；
- OpenWire；
- 电压不可信；
- Cell/Pack UV；
- 短路；
- 严重过流；
- NTC / 温度异常；
- Heater active；
- 参数无效。

## 11. Requested / Actual Balance

必须区分：

- `balance_requested_mask`
- AFE actual balance mask

若请求 OFF 但 AFE 写失败：

- 不能把软件期望的 0 发布成“硬件已经关闭”；
- 能读回则发布 actual；
- 读不到则保持 UNKNOWN/错误语义；
- DVC 约 60 s auto-clear 仍是独立硬件 fallback；
- 软件约 45 s 续租仅在公共策略持续授权时执行。

## 12. 关键系统不变量

1. 无可靠 charger evidence 不得从 IDLE 发起新加热。
2. ARMING 时 Heater 必须 OFF，直到确认充电电流消失。
3. Heater failure 永远不能转换成低温充电放行。
4. 加热中可靠放电出现必须立即关闭 Heater。
5. 单侧保护不能破坏反方向合法功率路径。
6. Hard isolate 不能被 AUTO_DIODE 绕过。
7. 电压不可信时 Balance 必须 OFF。
8. OpenWire active / suspected / confirmed 时 Balance 必须 OFF。
9. 均衡阈值不得重新复用压差保护参数。
10. COV 可在满足安全门禁时继续作为被动均衡恢复路径。
11. Balance requested 与 actual 必须分离。
12. AFE 无效时 Heater / Balance 都 fail-safe。

## 13. TODO_VERIFY_HW

量产前至少完成：

- DVC AUTO_DIODE / BDPT 双向续流与 Gate/Vgs；
- 低温插充电器时，从可靠 Ichg 到 CHG=AUTO_DIODE 的最大响应；
- Heater ARMING -> ACTIVE 时确认不存在持续低温充电；
- 加热中拔充电器再放电；
- 拔充电器且完全空载时 Heater 是否可能由电池反供；
- 弱充电器、限流、打嗝电源；
- GP1 Heater MOS 过温与不可逆 Fuse；
- OpenWire 首/中/末通道和接触不良；
- 49/50/51 mV 启动边界、stop delta 回差；
- 可调 Balance start voltage；
- COV 停充后继续均衡；
- Balance 写失败和 actual mask；
- 均衡温升、测量扰动和 DVC 45 s 续租/约 60 s auto-clear。

