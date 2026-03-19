# 代码修改记录

**日期**: 2026-03-19
**文件**: `vendor/ble_sample/app.c`

---

## 修复项

### 1. ADC除零风险修复 (app.c:373-374)
**严重度**: 高
**问题**: `bat_temp_mv` 和 `mos_temp_mv` 采样值 >= 3300mV 时，`(3300 - xxx_mv)` 为0或负数，导致除零崩溃或计算错误。
**修改**:
```c
// 新增保护
if(bat_temp_mv >= 3299) bat_temp_mv = 3299;
if(mos_temp_mv >= 3299) mos_temp_mv = 3299;
```

### 2. timer0_irq_cnt 竞争条件修复 (app.c:97)
**严重度**: 高
**问题**: `timer0_irq_cnt` 在ISR中修改、主循环中可能读取，但未声明为 `volatile`，编译器可能优化导致读到过期值。
**修改**:
```c
// 原: int timer0_irq_cnt = 0;
volatile int timer0_irq_cnt = 0;
```

### 3. BLE广播UUID注释澄清 (app.c:199-205)
**严重度**: 低
**问题**: UUID list 的 `0x05` 长度字段实际正确(1type+4data=5)，但缺少注释容易误解。
**修改**: 添加清晰注释说明长度字段含义。

### 4. blt_pm_proc 冗余代码清理 (app.c:958-1064)
**严重度**: 中
**问题**: 大量 `#if 0` 废弃代码块嵌套，旧逻辑与新逻辑混杂，维护困难。
**修改**:
- 删除整个 `#if 0` 废弃块（约65行旧逻辑）
- 保留并整理当前生效的电源管理逻辑
- 统一代码缩进格式

### 5. 低功耗进出状态机变量重置 (app.c:353-372)
**严重度**: 中
**问题**: `app_adc_multi_sample()` 进入低功耗直接return，但 `mos_state`、`state_fuse`、`rong_fuse_afe_err_cnt` 状态机变量未重置，唤醒后可能残留错误状态。
**修改**:
- 将 `mos_state`、`rong_fuse`、`state_fuse`、`rong_fuse_afe_err_cnt` 声明移至函数顶部
- 在低功耗return前重置所有状态机变量
- 删除函数中部的重复声明

---

## 待确认项

### BIT2_3_CTLC 宏定义 (sh367309_datadeal.h:423)
```c
#define BIT2_3_CTLC (1<<4) | (1<<3)  // 设置了 bit3 和 bit4
```
宏名 `BIT2_3` 暗示操作 bit2/bit3，但实际设置 bit3/bit4。需核对 SH367309 规格书确认 CTL_C 寄存器位定义。

### 安全配置建议
- `BLE_APP_SECURITY_ENABLE=0` 当前关闭，量产建议开启
- `APP_BATT_CHECK_ENABLE=0` 关闭，低电压写Flash有风险
