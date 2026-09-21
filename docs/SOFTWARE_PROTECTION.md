# D008 / D011 / D013 统一软件保护

## 1. 参数所有权

三个产品的 MCU 软件保护参数都来自 `g_tParam.protect`，字段结构保持 First / Second / Third / Recover / Filter。共同算法位于 `bms_sw_protection.c/.h`；AFE backend 不再各自实现一套软件阈值状态机。

- First：一级告警/报告，不直接作为最终 MOS 关闭级。
- Second：二级告警/报告，不直接作为最终 MOS 关闭级。
- Third：软件保护级，进入 CHG/DSG 阻断。
- Recover：恢复阈值/回差。
- Filter：触发与恢复确认时间来源。

## 2. 当前公共算法覆盖

Cell OV/UV、Pack OV/UV、Charge/Discharge OC、Charge OT/UT、Discharge OT/UT、MOS OT、Cell delta。触发和恢复都经过连续样本确认。

## 3. SOC legacy 字段

参数表仍保留 `u16SocUp_First/Second/Third/Rcv/Filter` 兼容字段，但当前 `bms_sw_protection` 没有把它加入统一状态机。历史 `SocUp/SocLow` 命名冲突未解决前不能静默赋予新行为。因此参数存在/可读写不等于保护算法正在使用。

## 4. 温度有效性

backend 向公共层提供 Battery min/max、MOS温度和 valid 标志。必需温度无效时进入 `BMS_ERROR_TEMP_BREAK` / fail-safe，不能用旧样本恢复；重新有效后重新经过保护滤波。

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

具体 FET 寄存器、GPIO和故障恢复见各分支 `*_PRODUCT_REFERENCE.md` 与 backend 源码。

## 7. 修改规则

- 新软件保护项优先放公共层；
- 不在 AFE driver 复制三级状态机；
- 不把 SCD/WDT/Body-Diode 等硬件能力塞进 `g_tParam.protect`；
- 保护语义变化必须补 contract 和实板触发/恢复测试。
## 2026-09-21 公共核心一致性

First/Second 在离开本级阈值后按 Filter 恢复（等于阈值仍 active），Recover 仅用于 Third。新电池温度故障要求对应充/放电方向资格；已有故障按恢复门限清除。电池 NTC 与 MOS NTC 独立有效性。板级输入/IO/AFE 参数不随公共核心同步而修改，实板触发/恢复仍 TODO_VERIFY_HW。详见 [资源及验证约定](RESOURCE_BUDGET_AND_VALIDATION.md)。
