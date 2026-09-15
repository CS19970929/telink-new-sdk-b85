# D008 配置修改、拉代码、固件编译与上位机构建指南

> 适用分支：`feature/sh3673510-d013-bmsdvc`
>
> 实际产品：HS-D008 + TLSR8251F512ET32 + DVC1124-2。分支名中的 `sh3673510-d013` 是历史命名，不代表硬件。

本文回答四件事：

1. 想改某个配置，**应该改哪个文件**；
2. **具体改什么宏/字段**；
3. 修改后哪些设备会生效，哪些已出货设备不会自动被默认值覆盖；
4. 从 `git clone` 开始，如何编译固件和 Windows Qt 上位机。

---

## 1. 从 GitHub 拉取 D008

推荐单独目录，不与 D011/D013 混用：

```bash
git clone --single-branch --branch feature/sh3673510-d013-bmsdvc https://github.com/CS19970929/telink-new-sdk-b85.git D008-BMS
git -C D008-BMS branch --show-current
git -C D008-BMS rev-parse HEAD
```

预期当前分支：

```text
feature/sh3673510-d013-bmsdvc
```

以后更新：

```bash
cd D008-BMS
git fetch origin
git switch feature/sh3673510-d013-bmsdvc
git pull --ff-only origin feature/sh3673510-d013-bmsdvc
```

不要直接 `git pull` 后不确认当前分支。

---

## 2. 先判断：你要改的是哪一类配置

| 想改的内容 | 应改位置 | 是否属于产品编译配置 | 已有设备是否自动变化 |
|---|---|---:|---:|
| 24S LFP / 20S NMC | `vendor/ble_sample/d008_product_profile.h` | 是 | 否，已持久化 chemistry/profile 不会被静默覆盖 |
| DVC cell count / Rsense / GP1..GP6 / ADC / WDT 等板级默认 | `vendor/ble_sample/dvc1124_project_config.h` | 是 | 视 DVC config store 是否已有持久值；不要假设默认会覆盖现场值 |
| MCU GPIO 网络 | `vendor/ble_sample/conf.h` + 实际调用代码 | 是 | 固件更新后变化 |
| 软件三级保护默认值 | `vendor/ble_sample/param.h` | 是 | **不会自动覆盖已保存参数** |
| AFE Hardware Protection V2 | `bms_afe_hw_profile_t` + `bms_afe_hw_profile.c` / 通信配置 | 持久参数 | 已保存 profile 优先 |
| SCD/WDT/Body-Diode DVC 专属安全默认 | `dvc1124_project_config.h` | 是 | 已保存 DVC config 可能覆盖 default |
| 容量/BMS类型/初始SOC/设备默认名 | `vendor/ble_sample/conf.h` | 是 | 已保存 system/BT name 可能覆盖 |
| SOC chemistry/profile | D008 product profile + Cold KV system keys | 两者都有 | 只在 AUTO/AUTO 的未配置设备自动迁移一次 |
| SOC OCV 曲线 | `vendor/ble_sample/bms_soc_profile.h` | 是 | 固件更新后算法使用新表；需提高 profile version 并验证 |
| Modbus 从站地址 | `vendor/ble_sample/modbus_rtu.c` 的 `MB_ADDR` | 是 | 固件更新后变化 |
| BLE 广播周期/RF功率/连接延迟 | `vendor/ble_sample/app.c` | 是 | 固件更新后变化 |
| Flash 区域 | `vendor/ble_sample/flash_store_cfg.h` | 是，兼容性高风险 | 不允许随意改 |
| 固件升级重置 epoch | `vendor/ble_sample/conf.h` 的 `FW_UPGRADE_RESET_*_EPOCH` | 是 | 只对对应 store 生效 |

本文路径中的 `vendor/ble_sample/` 完整前缀是：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/
```

---

## 3. D008 24S / 20S 怎么改

文件：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/d008_product_profile.h
```

当前定义：

```c
#define D008_PRODUCT_PROFILE_24S_LFP  1u
#define D008_PRODUCT_PROFILE_20S_NMC  2u

#ifndef D008_PRODUCT_PROFILE
#define D008_PRODUCT_PROFILE D008_PRODUCT_PROFILE_24S_LFP
#endif
```

### 默认 24S LFP

保持：

```c
#define D008_PRODUCT_PROFILE D008_PRODUCT_PROFILE_24S_LFP
```

实际得到：

```text
cell_count = 24
chemistry = LFP
SOC profile = GENERIC_LFP
```

### 改成 20S NMC

改为：

```c
#define D008_PRODUCT_PROFILE D008_PRODUCT_PROFILE_20S_NMC
```

实际得到：

```text
cell_count = 20
chemistry = NMC
SOC profile = GENERIC_NMC
```

**只改这个宏不代表 20S NMC 产品参数完成。** 容量、OV/UV、OC、温度、SOC OCV/端点仍需要单独签核。

对已经保存过 chemistry/profile 的设备，源码 `param_apply_d008_product_identity_if_unset()` 只在 Cold KV 为 `AUTO/AUTO` 时应用编译 profile，不会强制把已配置设备从 24S/LFP 改成 20S/NMC。

---

## 4. DVC1124 板级配置怎么改

文件：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124_project_config.h
```

常用项目：

```c
DVC1124_DEFAULT_CELL_COUNT
DVC1124_DEFAULT_SHUNT_UOHM
DVC1124_DEFAULT_BATTERY_NTC_GP
DVC1124_DEFAULT_MOS_NTC_GP

DVC1124_GP1_DEFAULT_MODE ... DVC1124_GP6_DEFAULT_MODE

DVC1124_DEFAULT_HIGH_SIDE_FET_MASK
DVC1124_DEFAULT_CADC_WORK_ENABLE
DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE
DVC1124_DEFAULT_CC1_WORK_TIME
DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME

DVC1124_CHARGE_PUMP_VOLTAGE_CODE
DVC1124_DEFAULT_VADC_ENABLE
DVC1124_DEFAULT_VADC_SYNC_WITH_CC2
DVC1124_DEFAULT_VADC_PERIOD
DVC1124_DEFAULT_VADC_TIME

DVC1124_DEFAULT_V3P3_SLEEP_ENABLE
DVC1124_DEFAULT_V3P3_WORK_ENABLE
DVC1124_DEFAULT_V3P3_TIMEOUT_RESTART
DVC1124_DEFAULT_TIMED_WAKE
DVC1124_DEFAULT_INTERRUPT_MASK

DVC1124_DEFAULT_DSG_MASK_POLICY
DVC1124_DEFAULT_CHG_MASK_POLICY

DVC1124_HW_SCD_THRESHOLD_MV
DVC1124_HW_SCD_DELAY_US
DVC1124_CURRENT_WAKE_THRESHOLD_UV
DVC1124_BODY_DIODE_THRESHOLD_UV
DVC1124_I2C_WATCHDOG_SECONDS
DVC1124_I2C_TIMEOUT_CLOSE_CHG
DVC1124_I2C_TIMEOUT_CLOSE_DSG
```

### 不应该改的文件

普通产品调参时不要去改：

```text
dvc1124_reg.h
```

这个文件是 DVC1124-2 V1.2 的寄存器真值。只有发现手册事实/驱动定义错误时才改。

### 当前 D008 默认必须保持谨慎的项目

在没有实板签核前，不要为了“功能完整”把以下项目从 0 改成非0：

```text
DVC1124_HW_SCD_THRESHOLD_MV
DVC1124_HW_SCD_DELAY_US
DVC1124_CURRENT_WAKE_THRESHOLD_UV
DVC1124_BODY_DIODE_THRESHOLD_UV
DVC1124_I2C_WATCHDOG_SECONDS
```

---

## 5. MCU IO 怎么改

当前 D008 GPIO 别名在：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/conf.h
```

例如：

```c
#define RF_EN_PIN              (GPIO_PD4)
#define AFE1_PRO_EN_PIN        (GPIO_PD7)
#define SW_PIN                 (GPIO_PA0)
#define HEATER_EN_PIN          (GPIO_PA1)
#define CHG_IN_PIN             (GPIO_PB1)
#define OWC_TX_PIN             (GPIO_PC2)
#define OWC_RX_PIN             (GPIO_PC3)
#define MCU_LDO_PIN            (GPIO_PC4)
#define SOC25_PIN              (GPIO_PB4)
#define SOC50_PIN              (GPIO_PB5)
#define SOC75_PIN              (GPIO_PB7)
#define SOC100_PIN             (GPIO_PD3)
#define LED_BLUE_PIN           (GPIO_PB6)
```

### 修改规则

1. 先改原理图/BOM或确认目标PCB确实变更；
2. 再改 `conf.h` 的网络别名；
3. 全仓搜索该宏，检查初始化方向、上下拉、active level、wake source；
4. 检查低功耗 GPIO wake；
5. 实板测量上电/休眠/唤醒默认态。

不要只改 `#define` 就认为 IO 移植完成。

---

## 6. 软件三级保护怎么改

默认值位置：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/param.h
```

结构：

```c
struct PRT_E2ROM_PARAS
```

常用宏：

```text
COV_* / CUV_*
BOV_* / BUV_*
OCC_* / ODC_*
OTC_* / UTC_*
OTD_* / UTD_*
mos_*
VDELTER_*
socLow_*
```

最终默认结构由：

```c
#define E2P_PROTECT_DEFAULT_PRT {...}
```

生成。

### 重要单位

- `Filter` 字段：源码按 **10 ms/单位**，公共软件保护采样周期为 200 ms；
- 温度字段：`(°C + 40) * 10`；
- 电流保护字段：当前业务结构使用 0.1 A 量级；
- 单体电压字段按 mV 处理；
- Pack voltage 要沿用现有 `u16VCellTotle`/协议单位，修改前先核对当前产品的协议/采样单位，不要只看宏名猜。

### 为什么改 `param.h` 后旧设备可能没变化

启动时：

```text
param.h default
 -> bms_cold_kv_store_get_default_protect()
 -> 只在没有有效持久参数时成为默认
```

已出货设备 Cold KV 已有值时，旧值优先。

因此：

- **新板/空 Flash 默认值**：改 `param.h`；
- **现场单台设备调参**：走通信写持久参数；
- **升级固件统一迁移某些参数**：不要靠 `PARAM_VER` 猜版本，应使用明确升级迁移/epoch逻辑，只改目标字段，不能覆盖其他客户参数。

---

## 7. AFE Hardware Protection V2 怎么改

它与软件三级保护是两套独立参数。

源码结构：

```text
bms_afe_hw_profile.h
bms_afe_hw_profile.c
bms_cold_kv_store.c
```

35-word requested profile 包含：

```text
COV/CUV
OCD1/OCD2
OCC1/OCC2
SC
hardware temperature
recover/recover confirmation
enable_mask
```

D008 backend 会按 200 µΩ 和 DVC 量化规则产生 effective 值。

### 修改新设备第一次迁移默认

入口：

```c
bms_afe_hw_profile_build_migration_default()
```

但要注意：这只对 `schema_version==0 && afe_model==0` 的空 profile 做一次 migration。已保存 profile 的设备不会因为你修改这个函数自动改变。

### 修改现场设备

固件公开：

```text
0x2500..0x2522  requested 35 words
0x2523..0x252B  metadata
0x2540..0x2562  effective 35 words
Custom function 0x42  privileged session
```

必须整块原子写，不允许分段伪造成功。

当前仓库的 `BMSAssistantQt` **还没有专用 AFE Hardware Protection V2 编辑页，也没有 direct-serial transport**。不要把旧文档中的 `BmsTool.Windows/BmsFactoryTest.Windows` 当成当前仓库存在的工程。当前 Qt 工具可用于 BLE 状态、软件参数预览、手动寄存器和原始帧调试；AFE HW profile 正式写入应使用实现了 `0x42 + 35-word atomic write + requested/effective readback` 的工程工具，或先补齐 Qt 端该功能。

---

## 8. SOC 怎么改

### D008 选择 LFP/NMC

优先改：

```text
d008_product_profile.h
```

### 修改 OCV 数据

文件：

```text
bms_soc_profile.h
```

修改时必须同时考虑：

```text
profile ID
profile version
OCV table
valid voltage range
full/empty anchor
low-end knee
```

不要在 `SocEnhance.c` 里直接塞某个电芯的硬编码 OCV 表。

---

## 9. 产品身份、容量、蓝牙名怎么改

文件：

```text
conf.h
```

当前 D008 仍有历史 `FD_BMS_TYPE D3PRO` 兼容残留；不要把它误当成 D008 产品签核参数。

常见项：

```c
BMS_SOFTWARE_VERDION_DEFAULT
BMS_HARDWARE_VERDION_DEFAULT
BMS_SERIAL_NUMBER_DEFAULT
CapacityFactory
FAC_INIT_soc
DEV_NAME_STR
DEV_NAME_STR2
```

蓝牙名称还支持 Cold KV 后缀运行时修改，因此改编译默认名不一定覆盖已经保存的 suffix。

---

## 10. 通信和低功耗配置怎么改

### Modbus 地址

文件：

```text
modbus_rtu.c
```

当前：

```c
#define MB_ADDR 0x01
```

### BLE 广播/射频/连接参数

文件：

```text
app.c
```

常见项：

```text
MY_ADV_INTERVAL_MIN / MAX
MY_RF_POWER_INDEX
APP_CONN_LATENCY_NORMAL
APP_CONN_LATENCY_OTA
ADV_IDLE_ENTER_DEEP_TIME
CONN_IDLE_ENTER_DEEP_TIME
```

### CHG-IN 深睡唤醒

当前 PB1 `CHG_IN_PIN` 是低有效 wake source。修改休眠策略时必须同时核对：

```text
IsChargerWakeupActive()
app_deepsleep_pad_wakeup_active()
cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 1)
```

---

## 11. Flash / OTA 怎么改

业务 Flash 布局：

```text
flash_store_cfg.h
```

这是兼容性接口。普通参数需求不要通过“换地址”解决。

修改任何地址前必须同时检查：

```text
Firmware A/B
OTA metadata
event log
runtime
SOC KV
cold KV
DVC work-config KV
pairing/MAC/calibration
```

并执行 `flash_quick_check.py + MAP + manifest + verify + OTA掉电测试`。

---

# 12. D008 固件怎么编译

## Windows

进入仓库：

```bat
cd /d D:\work\D008-BMS
```

先检查环境：

```bat
python bms_tools\bms.py env
python bms_tools\bms.py sources --check
```

完整 clean build：

```bat
python bms_tools\bms.py rebuild --jobs 4
python bms_tools\bms.py check-fw
python bms_tools\bms.py size
python bms_tools\bms.py map
python bms_tools\bms.py manifest
python bms_tools\bms.py verify
python bms_tools\bms.py static --no-report
```

最终烧录 BIN：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin
```

不要烧：

```text
825x_ble_sample.raw.bin
```

### 建议交付重命名

源码实际产物名保持 `825x_ble_sample.bin`，交付时建议复制成：

```text
HS-D008_TLSR8251_DVC1124_24S-LFP_<SWVER>_<shortsha>.bin
```

20S版本：

```text
HS-D008_TLSR8251_DVC1124_20S-NMC_<SWVER>_<shortsha>.bin
```

这里是交付命名规范建议，不是编译器自动输出名。

---

# 13. Windows Qt 上位机：从拉代码到打包

当前桌面上位机完整路径：

```text
tools/BMSAssistantQt
```

技术栈：

```text
Python 3.9+
PySide6 >=6.8,<7
PyInstaller >=6.10,<7
QtBluetooth
QtWidgets
```

## 13.1 单独拉 D008 并直接运行上位机

```bat
git clone --single-branch --branch feature/sh3673510-d013-bmsdvc https://github.com/CS19970929/telink-new-sdk-b85.git D008-BMS
cd /d D008-BMS\tools\BMSAssistantQt
scripts\run.bat
```

`run.bat` 会自动：

```text
使用 python 或 py -3
创建 %LOCALAPPDATA%\BMSAssistantQt\venv
pip install requirements.txt
启动 main.py
```

## 13.2 D008 上位机串数

当前 Qt 客户端在：

```text
tools/BMSAssistantQt/bmsassistantqt/protocol.py
```

有：

```python
class RegisterCatalog:
    currentProjectSeriesCount = 10
```

这个值目前仍是共享工具历史默认，**D008 打包前必须改**：

24S：

```python
currentProjectSeriesCount = 24
```

20S：

```python
currentProjectSeriesCount = 20
```

否则电池状态页单体显示数量会错误。

这也是当前上位机的已知产品化技术债；后续更合理的做法是由设备 metadata 自动读取串数，而不是每个包手改常量。

## 13.3 Windows 打包

```bat
cd /d D:\work\D008-BMS\tools\BMSAssistantQt
scripts\package-windows.bat
```

脚本先执行：

```text
python main.py --smoke-test
```

然后用 PyInstaller：

```text
--windowed
--name BMSAssistantQt
--collect-all PySide6
--hidden-import PySide6.QtBluetooth
```

最终输出：

```text
tools/BMSAssistantQt/.dist/BMSAssistantQt/BMSAssistantQt.exe
tools/BMSAssistantQt/.dist/Launch-BMSAssistantQt.bat
tools/BMSAssistantQt/.dist/docs/README.md
tools/BMSAssistantQt/.dist/docs/WINDOWS-DELIVERY.md
```

客户侧推荐启动：

```text
Launch-BMSAssistantQt.bat
```

### 建议交付包名

```text
BMSAssistantQt_HS-D008_24S-LFP_<date>_<shortsha>.zip
BMSAssistantQt_HS-D008_20S-NMC_<date>_<shortsha>.zip
```

---

## 14. macOS / Linux 上位机

macOS：

```bash
cd D008-BMS/tools/BMSAssistantQt
./scripts/package-macos.sh
./scripts/run-macos-app.sh
```

输出：

```text
.dist/BMSAssistantQt.app
```

Linux：

```bash
cd D008-BMS/tools/BMSAssistantQt
./scripts/run.sh
./scripts/package-linux.sh
```

---

## 15. 修改配置后的最小检查顺序

```text
1. git diff -- 只确认预期文件
2. python bms_tools/bms.py sources --check
3. 对应 Host contract tests
4. TC32 rebuild
5. check-fw / size / map / manifest / verify
6. cppcheck
7. 上位机运行/打包 smoke test
8. 实板验证
```

保护、AFE、GPIO、Flash、低功耗任何一项改动，不能只以“编译通过”作为完成标准。
