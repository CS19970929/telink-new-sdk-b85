# BMS 软件架构与配置所有权

本文只描述 D008 / D011 / D013 当前共同的软件边界。产品专属 IO、AFE 静态配置和未决硬件项分别见各分支 `*_PRODUCT_REFERENCE.md`。

## 1. 依赖方向

```text
app / BLE / Modbus / product logic
              |
              v
      bms_sw_protection
              |
              v
        BMS state/faults
              |
              v
          bms_afe.h
              |
      +-------+-------+
      |               |
      v               v
 DVC1124 backend   SH35xx backend
      |               |
      v               v
 I2C/registers     SPI/registers
```

一个产品固件只编译一个 AFE backend；不使用运行时 AFE factory/ops table。

## 2. 保护参数所有权

### 软件保护

- 唯一 requested 参数源：`g_tParam.protect`。
- 统一实现：`bms_sw_protection.c/.h`。
- 语义：First / Second / Third / Recover / Filter。
- First/Second 用于分级告警/状态；Third 进入 MOS 阻断策略。

### AFE 硬件保护

- 唯一 requested 参数源：`bms_afe_hw_profile_t`。
- 没有 First/Second/Third。
- 与软件保护独立持久化、独立通信修改。
- backend 负责根据具体芯片/Rsense 做 validation、量化、寄存器编码和 readback。
- requested 与 effective 必须分开，禁止把芯片量化后的 effective 反写成用户 requested。

首次引入独立 AFE profile 时允许从历史软件参数初始化一次；完成迁移后两套参数不再联动。

## 3. AFE Hardware Protection V2

统一 Modbus 语义接口：

- `0x2500..0x2522`：35-word requested profile；
- `0x2523..0x252B`：capability、profile-valid、Rsense、cell count、WDT、session、apply-state、last-error、interface version；
- `0x2540..0x2562`：35-word effective profile；
- 自定义功能 `0x42`：AFE 参数写授权会话，默认 60 s。

硬件参数写入必须是完整事务：

```text
validate candidate
 -> persist
 -> apply backend
 -> readback requested/effective
 -> verify
 -> success
```

失败时恢复上一份 profile 并重新 apply；rollback 失败进入 `CONFIG_INCONSISTENT`，不能返回成功。

## 4. AFE 通信失效边界

应用层只能通过 `bms_afe.h` 请求采样、FET、输出许可、保护应用和辅助测量。AFE backend/guard 必须保证：

- 无效完整采样不能被当作有效数据继续保护决策；
- 通信失败进入 output inhibit / fail-safe；
- reinit 有次数/时间边界，不能无界循环；
- 重新通信后必须重新满足有效样本资格，不能因为 init 成功立即开 MOS；
- I2C/SPI 物理失联时，软件关 FET 是否真正可达必须由各产品硬件 WDT/power-cycle 路径验证。

## 5. FET 与保护仲裁

最终输出不能只由 charger/key 请求决定。逻辑上必须同时满足：

```text
product request
AND output enabled
AND AFE communication qualified
AND no software-third block
AND no active AFE hardware block/lockout
```

具体芯片的 latch clear、load removal、short-circuit recovery 仍属于 backend/product 策略，不能为了“统一”而强行写成相同寄存器行为。

## 6. Product Profile 边界

以下内容属于产品，而不是通用 AFE 算法：

- cell count / chemistry；
- Rsense；
- MCU GPIO / 外部 power topology；
- NTC 数量、阻值、物理位置；
- AFE backend 型号；
- 可启用的 hardware capability；
- 产品最终 OV/UV/OC/SC/temperature/SOC 参数。

这些值必须由该产品原理图/BOM/源码/实板证据决定，禁止跨 D008/D011/D013 复制。

## 7. 当前产品映射

| 产品 | AFE | 物理 profile 来源 |
|---|---|---|
| D008 | DVC1124-2 | `d008_product_profile.h` + `dvc1124_project_config.h` |
| D011 | SH3673510 | `sh3673510_project_config.h`，10S/250µΩ |
| D013 | SH3673510（当前源码） | `sh3673510_project_config.h`，4S/100µΩ；D013 原理图尚缺，IO 未硬件签核 |

## 8. 文档规则

- 产品 IO/AFE 配置只在该分支的 `Dxxx_PRODUCT_REFERENCE.md` 维护。
- 通用保护语义只在 `SOFTWARE_PROTECTION.md` 与 `AFE_HARDWARE_PROTECTION_V2.md` 维护。
- 实板未完成项只在 `HARDWARE_VALIDATION.md` 维护。
- 历史审计、旧任务列表、跨产品硬件说明不得继续作为当前设计入口。
- 文档与源码冲突时，以源码为“当前软件事实”，同时标出与原理图/手册的冲突，不静默修正。