# D014 配置、构建与联调指南

> 分支：`refactor/d014-common-bms-features`  
> 产品：HS-D014-8S15A + TLSR8251F512ET32 + SH3673510 + 8S。

> 上位机章节中的历史 Qt 路径已废弃。当前唯一真源是
> `feature/windows-afe-hw-protection-editor-v2:bms-tool-windows/`；诊断与命令以
> `docs/D014_DIAGNOSTICS.md` 及该分支最新文档为准。

硬件事实和未签核项先看 `D014_PRODUCT_REFERENCE.md`，不要从文件名“15A”反推保护阈值或额定容量。

## 1. 拉取分支

```bash
git clone --single-branch --branch refactor/d014-common-bms-features https://github.com/CS19970929/telink-new-sdk-b85.git D014-BMS
cd D014-BMS
git branch --show-current
git rev-parse HEAD
```

更新：

```bash
git fetch origin
git switch refactor/d014-common-bms-features
git pull --ff-only origin refactor/d014-common-bms-features
```

固件目录：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/
```

## 2. 主要配置入口

| 修改内容 | 文件 | D014 当前值/规则 |
|---|---|---|
| 串数/Rsense/板级 GPIO/能力开关 | `sh3673510_project_config.h` | 8S、667µΩ、heater off、balance on |
| SH3673510 静态寄存器 | `sh3673510_project_config.h` | 复用 D011 已验证语义宏；CN=8 |
| AFE 寄存器定义 | `sh3673520_reg.h` | 芯片真值，不作为产品调参文件 |
| AFE protection encoding/apply/readback | `sh3673510_control.c` | 当前电流量化使用 667µΩ |
| 软件三级保护默认 | `param.h` | 只影响没有持久参数的新设备/迁移 |
| AFE HW Protection V2 | `bms_afe_hw_profile.*` | 与软件保护独立持久化 |
| 产品身份/容量/RS485 | `conf.h` | D014/8S/RS485；容量仍待产品签核 |
| SOC | `bms_soc_profile.h`, `SocEnhance.c` | 公共框架 |
| Flash/Storage | `flash_store_cfg.h`, `bms_config_store.*` | 不因 D014 移植改变布局 |
| Sleep/Wake/BLE | `app.c` + AFE guard/backend | 实板验证必需 |

## 3. D014 板级配置

`sh3673510_project_config.h`：

```c
#define SH3673510_D011_CELL_COUNT               8u
#define SH3673510_D011_SHUNT_UOHM              667u
#define SH3673510_D011_NTC_NOMINAL_OHM         10000UL

#define SH3673510_PRODUCT_HEATER_SUPPORTED       0u
#define SH3673510_PRODUCT_BALANCE_SUPPORTED      1u
#define SH3673510_PRODUCT_HEATER_NTC_SUPPORTED   0u
#define SH3673510_PRODUCT_MOS_NTC_SUPPORTED      0u
```

`SH3673510_D011_*` 是复用 D011 公共实现时保留的兼容命名，不代表 D014 使用 D011 硬件。

正式 D014 GPIO：

```text
PD4  CMNT-EN
PD7  SCLK
PA0  DI1/SW1
PA1  485-EN
PA7  SWS-A7
PB1  INT-WK-MCU
PB6  MISO
PB7  MOSI
PC0  ALARM
PC1  RESET
PC2  SCI1-TX
PC3  SCI1-RX
PC4  DB-LED1
PD3  CMNT-WK
PD2  CS-M
```

D014 不得新增对 PB4/PB5 D011 heater/fuse 网络的驱动。

## 4. 8S / Rsense 修改规则

当前原理图为 8S，三只 2mΩ 并联：

```text
2mΩ / 3 = 0.666666...mΩ
software model = 667µΩ
```

如果后续硬件改分流：

1. 先改 `SH3673510_D011_SHUNT_UOHM`；
2. 重新检查 current raw -> mA；
3. 重新检查 AFE OCD1/OCD2/OCC/SC requested -> code -> effective；
4. 重新做零偏/增益/温漂实测；
5. 不要只改 legacy `CS_Res/CS_Res_Num`。

## 5. 软件保护和 AFE 硬件保护

软件保护：`param.h` / `g_tParam.protect`。

AFE hardware profile：`bms_afe_hw_profile.*`，独立持久化并执行：

```text
validate -> persist -> apply -> readback/effective -> verify/rollback
```

两者不能绑在一个 enable 或同一套 recovery 上。SC/OCD/OCC 必须继续使用物理恢复证据，不能因关 MOS 后电流变为 0 就自动恢复。

## 6. 温度

- TS1/TS2：图纸为 10K-3435，是当前可信 battery temperature。
- TS3：图纸标 NC，heater 完全禁用。
- TS4：图纸标 MOS，但 RN4=10M；未确认 BOM 前 `SH3673510_PRODUCT_MOS_NTC_SUPPORTED=0`。

若确认 TS4 实装为 10K-3435，需要同时修改 capability、contract，并做至少低/中/高三个温度点以及开短路验证。

## 7. 产品身份与未签核默认

`conf.h` 当前：

```c
#define FD_BMS_TYPE                    D14
#define SeriesNum                      SH3673510_D011_CELL_COUNT
#define CapacityFactory                116
#define BMS_HARDWARE_VERDION_DEFAULT   "D014"
#define BMS_SERIAL_NUMBER_DEFAULT      "D014-UNSET"
#define DEV_NAME_STR                   "BT_D014"
#define MODBUS_RS485_ENABLE            1
```

注意：

- `CapacityFactory=116` 是继承迁移默认，不是原理图证明的 D014 容量；
- `D14` 当前暂时别名到历史 D11 numeric wire/storage ID，避免未同步 host 时破坏兼容；
- 如果要分配独立 D014 numeric ID，必须同步 Windows 上位机、协议和迁移策略。

## 8. 编译与静态 contract

```bash
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python tests/sh3673520_contract_check.py
python tests/sh3673510_d014_integration_check.py
python tests/sh3673510_protection_mode_check.py
python tests/sh3673510_temperature_encoding_check.py
python tests/sw_protection_contract_check.py
python tests/common_feature_policy_contract_check.py
python tests/afe_hw_profile_contract_check.py
python tests/afe_hw_access_contract_check.py
python tests/soc_contract_check.py
python tests/flash_quick_check.py
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static --no-report
```

输出 BIN：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin
```

建议交付名：

```text
HS-D014_TLSR8251_SH3673510_8S_<SWVER>_<shortsha>.bin
```

## 9. 上位机

Windows 工具的当前真源继续使用：

```text
feature/windows-afe-hw-protection-editor-v2
```

不要在 D014 固件分支复制第二套 Windows 工具。D014 独立 numeric ID、默认串数或新硬件保护参数如果需要在 UI 展示，应在该上位机分支做兼容更新。

## 10. 实板最小联调顺序

1. 只上电：3V3、AFE VCC、SPI、ALARM/RESET。
2. 8 节 cell 电压与 pack voltage。
3. 0A 零偏、已知充电电流、已知放电电流。
4. CHG/DSG MOS 手动和保护仲裁。
5. 软件 OV/UV/OC 与 AFE HW protection。
6. SC 与 recovery。
7. RS485 收发、方向切换、通信异常。
8. Balance B1..B8、open-wire。
9. TS1/TS2；确认 TS3 NC、核对 TS4 BOM。
10. Sleep/wake/BLE connected/idle 功耗。

完整清单见 `HARDWARE_VALIDATION.md`。
