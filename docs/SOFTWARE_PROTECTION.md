# D008 / D011 / D013 统一软件保护

## 1. 参数所有权

三个产品的 MCU 软件保护参数都来自 `g_tParam.protect`，字段结构保持 First / Second / Third / Recover / Filter。共同算法位于 `bms_sw_protection.c/.h`；AFE backend 不再各自实现一套软件阈值状态机。

- First：一级告警/报告，不直接作为最终 MOS 关闭级。
- Second：二级告警/报告，不直接作为最终 MOS 关闭级。
- Third：软件保护级，进入 CHG/DSG 阻断。
- Recover：恢复阈值/回差。
- Filter：触发与恢复确认时间的来源；当前公共算法按实际调度周期换算计数。

## 2. 当前公共算法覆盖

1. Cell OV / UV；
2. Pack OV / UV；
3. Charge OC / Discharge OC；
4. Charge OT / UT；
5. Discharge OT / UT；
6. MOS OT；
7. Cell delta large。

触发和恢复都要经过连续样本确认；不再使用旧 D008 “达到恢复值一次即清除”的行为。

## 3. 当前参数表中的 SOC legacy 字段

`g_tParam.protect` / PC 工具参数表仍保留 `u16SocUp_First/Second/Third/Rcv/Filter` 这一组兼容字段，但当前 `bms_sw_protection` 没有把它加入统一保护状态机。历史命名同时存在 `SocUp`、`SocLow` 等冲突，产品语义未得到足够证据前不能静默赋予新行为。

因此：**参数存在/可存储 != 当前保护算法正在使用。**

## 4. 温度输入有效性

backend 向公共保护层提供 Battery min/max、MOS 温度和 valid 标志。必需温度无效时触发 `BMS_ERROR_TEMP_BREAK` / fail-safe 路径，不能继续用旧温度样本判断恢复；重新有效后重新经过保护滤波。

## 5. 与 AFE Hardware Protection 的关系

AFE Hardware Protection V2 使用独立 `bms_afe_hw_profile_t`，不属于软件 First/Second/Third。软件参数写入不得重写 AFE hardware profile，反之亦然。

硬件保护可以因为 AFE 芯片速度/量化先于软件 Third 关 MOS；其硬件 flag/latch 由 backend 映射到 BMS fault/lockout。不同 AFE 的 SC/SCD、WDT、load detect、body diode、Open-Wire、Balance、寄存器量化和恢复条件继续保留产品差异。

## 6. 最终输出语义

三个产品虽然 backend 代码组织不同，最终安全语义必须同时受以下门控：

```text
product CHG/DSG request
AND output-enable
AND communication-qualified
AND no software Third block
AND no AFE hardware block/lockout
```

实际 GPIO/FET寄存器、硬件 latch clear 和故障移除判据见各分支 `*_PRODUCT_REFERENCE.md` 与 backend 源码。

## 7. 修改规则

- 新增保护项优先进入公共软件层，除非它明确是某个 AFE 的硬件特性。
- 不在 AFE driver 中复制 First/Second/Third 状态机。
- 不为了“统一参数”把 SCD/WDT/Body-Diode 等硬件能力塞进 `g_tParam.protect`。
- 每次改变保护语义都必须补 contract test 和实板触发/恢复测试。