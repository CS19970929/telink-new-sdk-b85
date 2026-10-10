# D008 / D011 / D013 / D014 统一软件保护

## 1. 参数所有权

四个产品的 MCU 软件保护参数都来自 `g_bms_protection_params`，字段结构保持 First / Second / Third / Recover / Filter。共同算法位于 `bms_sw_protection.c/.h`；AFE backend 不再各自实现一套软件阈值状态机。

- First：一级告警/报告，不直接作为最终 MOS 关闭级。
- Second：二级告警/报告，不直接作为最终 MOS 关闭级。
- Third：软件保护级，进入 CHG/DSG 阻断。
- Recover：Third 的恢复阈值/回差；First/Second 离开各自触发阈值后按 Filter 清除。
- Filter：触发与恢复确认时间来源。

## 2. 当前公共算法覆盖

Cell OV/UV、Pack OV/UV、Charge/Discharge OC、Charge OT/UT、Discharge OT/UT、MOS OT、Cell delta。触发采用超限样本累积，正常样本将触发计数减一；恢复要求连续满足恢复条件。两者不是同一种滤波。该行为保持原 D011/D013 策略，不能把触发计数递减改成清零而称为等价清理。

## 3. SOC Low 与最终阻断

`soc_low_first_percent/Second/Third/Rcv/Filter` 由 `bms/core/bms_soc.c::soc_update_low_faults()` 执行，写入三级 `soc_low`；不是未使用的 legacy 字段。SOC Low 与压差故障用于报告，不直接列入软件 CHG/DSG 阻断掩码；具体掩码见 `bms_sw_protection_charge_blocked()` / `bms_sw_protection_discharge_blocked()`。

`Filter` 单位 10 ms，以 `ceil(Filter * 10 / 200)` 转为样本数，最少一个样本。高值触发 `>=trip`，低值触发 `<=trip`；Third 分别在 `<=Recover` / `>=Recover` 后确认恢复。采样阻塞会影响墙钟时延，不能把配置 100 ms 写成保证 100 ms 响应。

2026-10-10：名义周期统一引用 `bms_timing.h` 的 200 ms。DVC 与 SH 均在每次有效
应用观察评估软件保护，正常等待 ADC 不跳过整轮软件滤波；无效数据仍由 guard 处理。
过流恢复的新鲜标志继续独立限制资格，缓存不能代替物理释放证据。
DVC COV/CUV 改用首次正常新电压后的实际稳定时间，最后仍需新电压确认和清除/回读；
电流恢复每轮有效观察维护连续性，真正失效、观察超时或负载重接才撤销相应窗口。
未修改阈值、原软件累积/衰减算法、硬件保护配置或 watchdog 静默。以上仅源码实现，
本轮未执行 host、编译或实板；相关验收范围见 [SOC 与调度](SOC.md)。

SH 后端的软件 Third OC 恢复额外要求负载/充电器移除或既有可靠反向状态，并仅在完整新 ADC 样本上推进计数；正常 CADC 等待暂停恢复计数，证据丢失/通信失败清零。D008 使用新电流及已确认的 PB1 负载移除/可靠反向电流，未签核充电器移除输入。AFE 重初始化保留已触发 OC，MCU 冷复位不持久化该 RAM 状态。完整状态所有权、场景与未关闭边界见 [D014 软件闭环](D014_SAFETY_LOOP.md)。

## 4. 温度有效性

backend 向公共层提供 Battery min/max、MOS温度、valid 标志及产品固定的 `mos_temp_required`。
四产品均要求电池 NTC 和 MOS NTC 有效。D013 于 2026-10-10 按用户确认的 TS4 MOS 10K-3435 启用；
默认保护、持久参数和重初始化边界见 [D013 影响审查](D013_PRODUCT_REFERENCE.md#11-ts4-mos-温度接入与影响审查2026-10-10)。
`required && !valid` 才是 MOS 断线；不支持时不参与 MOS OTP，不能把无效样本伪装为有效。必需温度无效时进入 `BMS_ERROR_TEMP_BREAK` / fail-safe，不能用旧样本恢复；重新有效后重新经过保护滤波。

D008 的 NTC 开短路范围筛选和三次新 VADF 恢复资格由 DVC 测量层统一产生，
电阻零值表示无效或尚未恢复合格；公共软件保护是 DVC 路径 `TEMP_BREAK` 的唯一
运行时写入者。必需 NTC 一次无效立即禁止两路 MOS，缓存不增加恢复次数。具体判据、原理图
依据及本次未执行测试的边界见 [D008 NTC 说明](D008_PRODUCT_REFERENCE.md#ntc-开短路与恢复资格2026-10-10)。

## 5. 与 AFE 硬件保护的关系

AFE Hardware Protection V2 使用独立 `bms_afe_hw_profile_t`，不属于 First/Second/Third。软件参数写入不得重写 AFE profile，反之亦然。硬件 flag/latch 由 backend 映射为 BMS fault/lockout；SC/SCD、WDT、load detect、body diode、Open-Wire、Balance、寄存器量化和恢复条件保留 AFE/Product 差异。

## 6. 最终输出语义

```text
product CHG/DSG request
AND output-enable
AND communication-qualified
AND no software Third block
AND no AFE hardware block/lockout
```

具体 FET 寄存器、GPIO和故障恢复见本仓库各产品 `*_PRODUCT_REFERENCE.md` 与 backend 源码。

## 7. 修改规则

- 新软件保护项优先放公共层；
- 不在 AFE driver 复制三级状态机；
- 不把 SCD/WDT/Body-Diode 等硬件能力塞进 `g_bms_protection_params`；
- 保护语义变化必须补 contract 和实板触发/恢复测试。

## 温度传感器暂时失效

已建立的电池/MOS 温度保护在传感器暂时无效时保持锁存，触发和连续恢复计数归零。TEMP_BREAK 保持输出禁止；传感器恢复到动作/恢复阈值之间时不能解除旧锁存。明确禁用温度保护或产品不装配 MOS NTC 时仍清理对应状态。host 场景不替代 NTC 与 Gate 实板验证。
