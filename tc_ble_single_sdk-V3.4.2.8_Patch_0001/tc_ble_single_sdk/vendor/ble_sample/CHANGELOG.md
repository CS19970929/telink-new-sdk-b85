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

---

## SOC精度改进 (SocEnhance.c)

**日期**: 2026-03-19

### 新增：OCV-SOC查表 (P0)
- 添加三元锂电池25C OCV-SOC参考表（21个数据点，3000-4200mV）
- 实现 `soc_ocv_lookup()` 基于 `GetEndValue` 线性插值
- 替换旧的 `Get_OpenCircuit_Value_new()` 空实现

### 新增：静置检测状态机 (P0)
- `soc_rest_detect()` 三状态机：IDLE -> PREPARE -> READY
- 小电流阈值 0.5A，准备时间 30秒，稳定时间 5分钟
- 大电流(>5A)后需等待 30分钟才可进入静置

### 新增：OCV校准 (P0)
- `soc_ocv_calibration()` 静置稳定后用OCV查表修正SOC
- 向下修正：偏差>=5%时校正（防止积分漂移偏高）
- 向上修正：仅SOC<50%时且偏差>=3%时校正（保守策略）

### 新增：温度-容量补偿 (P1)
- 添加三元锂温度-容量系数表（-20C~60C，千分比表示）
- `set_calsoc()` 和 `soc_param_lib_init()` 中 CapFull 乘以温度系数
- 低温折减示例：-10C时容量为标称的72%

### 新增：温度-电压阈值修正 (P1)
- 添加低温满充电压修正表（-20C时4000mV，0C时4150mV）
- 添加低温满放电压修正表（-20C时3300mV，0C时3150mV）
- `soc_cali()` 满充满放判断使用温度修正后的阈值

### 修改：APP_SOC_IntEnhance_Ctrl()
- 每200ms周期调用 `soc_rest_detect()` + `soc_ocv_calibration()`
- 在原有 `soc_cali()` 之前执行OCV校准

### 清理：旧OCV代码
- 删除约210行 `#if 0` 废弃代码（PRE_OCV, SOC_OCV_Fix2, get_ocv_cali）

### 待标定
- OCV-SOC表为参考值，建议基于实际电池型号标定
- 温度容量系数表为经验值，建议做不同温度点充放电验证
