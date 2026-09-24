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
- First / Second / Third / Recover / Filter。
- First/Second 用于分级状态；Third 进入 MOS 阻断策略。

### AFE 硬件保护
- 唯一 requested 参数源：`bms_afe_hw_profile_t`。
- 没有 First/Second/Third。
- 与软件保护独立持久化、独立通信修改。
- backend 负责具体芯片/Rsense 的 validation、量化、寄存器编码和 readback。
- requested 与 effective 分开，禁止把芯片量化后的 effective 伪装成用户 requested。

首次引入独立 AFE profile 时允许从历史软件参数初始化一次；完成迁移后两套参数不再联动。

D014 当前使用产品配置中的独立 AFE 默认值初始化，不从软件保护表迁移。

## 3. AFE Hardware Protection V2

- `0x2500..0x2522`：35-word requested profile；
- `0x2523..0x252B`：capability、profile-valid、Rsense、cell count、WDT、session、apply-state、last-error、interface version；
- `0x2540..0x2562`：35-word effective profile；
- 自定义功能 `0x42`：AFE 参数写授权会话，默认 60 s。

事务：`validate -> persist -> apply -> readback requested/effective -> verify`。失败恢复上一份 profile 并重新 apply；rollback 失败进入 `CONFIG_INCONSISTENT`。

## 4. AFE 通信失效边界

应用层只能通过 `bms_afe.h` 使用 AFE。backend/guard 必须保证无效采样不继续作为有效保护输入、通信失败进入 fail-safe、reinit 有界、恢复后重新满足有效样本资格。I2C/SPI 物理失联时软件 FET-off 是否真实可达仍须各产品用硬件 WDT/power-cycle 路径证明。

## 5. FET 与保护仲裁

最终输出逻辑上同时满足：

```text
product request
AND output enabled
AND AFE communication qualified
AND no software-third block
AND no active AFE hardware block/lockout
```

芯片特有的 latch clear、load removal、SCD recovery 保留在 backend/product 层，不能为了代码统一而强行统一寄存器行为。

## 6. Product Profile 边界

以下属于产品事实：cell count、chemistry、Rsense、MCU GPIO、外部 power topology、NTC 数量/阻值/位置、AFE 型号/capability、最终 OV/UV/OC/SC/temperature/SOC 参数。必须由该产品原理图/BOM/源码/实板证据决定，禁止跨产品复制。

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
- 文档与源码冲突时，以源码为当前软件事实，同时标出与原理图/手册的冲突，不静默修正。
