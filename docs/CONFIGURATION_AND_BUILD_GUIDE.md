# D011 配置修改、拉代码、固件编译与上位机构建指南

> 适用分支：`feature/sh3673510-d011-bms`
>
> 产品：HS-D011-10S50A-V1 + TLSR8251F512ET32 + SH3673510 + 10S + 250 µΩ。

本文给出：从 GitHub 拉代码开始，D011 各类配置具体在哪改、怎么改、哪些是编译默认、哪些是持久参数，以及 Windows Qt 上位机如何运行和打包。

---

## 1. 拉取 D011

```bash
git clone --single-branch --branch feature/sh3673510-d011-bms https://github.com/CS19970929/telink-new-sdk-b85.git D011-BMS
git -C D011-BMS branch --show-current
git -C D011-BMS rev-parse HEAD
```

更新：

```bash
cd D011-BMS
git fetch origin
git switch feature/sh3673510-d011-bms
git pull --ff-only origin feature/sh3673510-d011-bms
```

完整固件目录前缀：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/
```

---

## 2. 配置修改总表

| 想修改 | 主要文件 | 说明 |
|---|---|---|
| 串数、Rsense、SPI pin、板级IO | `sh3673510_project_config.h` | D011 产品/板级 profile |
| SH3673510 静态 SCONF/ALARM 配置 | `sh3673510_project_config.h` | 每一bit由语义宏表示 |
| SH36735xx 寄存器定义 | `sh3673520_reg.h` | 芯片真值，不是普通调参位置 |
| SH3673510 protection encoding | `sh3673510_control.c` | 量化/寄存器apply/readback |
| 软件 First/Second/Third 默认 | `param.h` | 新板/空参数默认 |
| 软件保护运行参数 | Cold KV / Modbus | 已保存参数优先于 `param.h` 默认 |
| AFE HW Protection V2 | `bms_afe_hw_profile.*` + Cold KV | 与软件保护独立 |
| 产品容量、BMS类型、版本、BT默认名 | `conf.h` | system defaults |
| Modbus/RS485模式 | `conf.h` + `modbus_uart.*` / `modbus_rtu.c` | D011 当前 `MODBUS_RS485_ENABLE=1` |
| RS485方向GPIO | `D011_RS485_EN_PIN` | PA1 / `485-EN` |
| SOC OCV | `bms_soc_profile.h` | 数据profile |
| Flash | `flash_store_cfg.h` | 高风险兼容接口 |
| Sleep/Wake/BLE | `app.c` + SH control/backend | 必须结合硬件验证 |

---

## 3. D011 串数、Rsense、SPI 和 GPIO 怎么改

文件：

```text
sh3673510_project_config.h
```

当前：

```c
#define SH3673510_D011_CELL_COUNT              10u
#define SH3673510_D011_SHUNT_UOHM              250u
#define SH3673510_D011_NTC_NOMINAL_OHM         10000UL
#define SH3673510_D011_SPI_GROUP               SH3673520_SPI_GROUP_B6_B7_D2_D7
```

当前 SPI：

```text
PB6 = MISO
PB7 = MOSI
PD7 = SCLK
PD2 = CS
```

当前板级 GPIO：

```c
D011_CMNT_EN_PIN
D011_AFE_SCLK_PIN
D011_SWITCH_PIN
D011_RS485_EN_PIN
D011_SWS_PIN
D011_INT_WK_MCU_PIN
D011_HEATER_CHG_PIN
D011_HEATER_FUSE_TRIGGER_PIN
D011_AFE_MISO_PIN
D011_AFE_MOSI_PIN
D011_AFE_ALARM_PIN
D011_AFE_RESET_OUT_PIN
D011_SCI1_TX_PIN
D011_SCI1_RX_PIN
D011_DEBUG_LED_PIN
D011_CMNT_WK_PIN
D011_AFE_CS_PIN
```

### 修改原则

- 串数/Rsense只在硬件真的变化时改；
- SPI pin改动必须同时核对 Telink hardware SPI group 能否支持；
- GPIO改动必须同时改初始化、上下拉、wake source、active level；
- `PB5 / HT-RF-EN` 当前被定义为不可逆 heater-fuse trigger，普通业务不能因为换板就随意拉高。

---

## 4. SH3673510 静态寄存器怎么改

仍在：

```text
sh3673510_project_config.h
```

关键宏：

```text
SH3673510_D011_SCONF1_BOOT_VALUE
SH3673510_D011_PD_EN
SH3673510_D011_PD_CTL
SH3673510_D011_PUMP_EN
SH3673510_D011_PDSG_CTL
SH3673510_D011_PDSGMOS
SH3673510_D011_DSGMOS_BOOT
SH3673510_D011_CHGMOS_BOOT

SH3673510_D011_CGR_WK
SH3673510_D011_LD_WK_CODE
SH3673510_D011_CRLD_EN_CODE
SH3673510_D011_OWD_EN

SH3673510_D011_PDSGT_CODE
SH3673510_D011_MOS_EN
SH3673510_D011_OCC_EN
SH3673510_D011_CADC_EN
SH3673510_D011_WDT_EN
SH3673510_D011_WDT_CODE

SH3673510_D011_RLD
SH3673510_D011_CADCT_CODE
SH3673510_D011_CDV_CODE

SH3673510_D011_OWV_CODE
SH3673510_D011_*_INT
```

当前组合值：

```text
SCONF1 = 0x00
SCONF2 = 0x50
SCONF3 = 0x44
SCONF4 = 0x6A
SCONF5 initial = 0x3C
SCONF7 = 0x04
0x47 = 0x57
0x48 = 0xFF
```

不要直接把这些整字节 magic number 写进 transaction code。产品配置在 project config，寄存器地址/mask/shift在 `sh3673520_reg.h`。

### SCONF6 特别说明

SCONF6 最终保护使能由 AFE Hardware Protection V2 的 `enable_mask` 运行时生成，不是永远固定一个编译字节。

---

## 5. 软件三级保护怎么改

文件：

```text
param.h
```

默认宏：

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

默认最终通过：

```c
E2P_PROTECT_DEFAULT_PRT
```

装入 `struct PRT_E2ROM_PARAS`。

### 单位

- Filter：10 ms/单位；
- 软件保护公共采样：200 ms；
- 温度：`(°C+40)*10`；
- 电流：业务结构按0.1 A量级；
- 单体电压：mV；
- Pack voltage：修改前按当前 `u16VCellTotle` 与协议单位核对，不能从宏名猜。

### 对已有设备

`param.h` 只是 default。Cold KV 已保存参数时，保存值优先。

因此：

- 新板默认：改 `param.h`；
- 单台现场参数：通过通信改；
- 批量升级只改部分字段：写明确 migration/epoch，不要依赖旧 `PARAM_VER` 全量覆盖。

---

## 6. AFE Hardware Protection V2 怎么改

公共结构：

```text
bms_afe_hw_profile.h
bms_afe_hw_profile.c
bms_cold_kv_store.c
```

SH backend apply：

```text
sh3673510_control.c
```

当前 SH3673510 硬件profile包括：

```text
COV/CUV
OCD1/OCD2
OCC1
SC
hardware charge/discharge temperature
recover confirmation
enable_mask
```

### 修改第一次迁移默认

函数：

```c
bms_afe_hw_profile_build_migration_default()
```

空 profile 第一次才从软件参数构建，之后 AFE HW profile 独立持久化。

### 不要这样做

不要为了改 D011 过流阈值去改 `sh3673520_reg.h` 的步进定义；那是芯片事实。

### 通信接口

```text
0x2500..0x2522 requested
0x2523..0x252B metadata
0x2540..0x2562 effective
function 0x42 authorization session
```

必须完整35-word原子事务。

当前仓库的 `BMSAssistantQt` **没有专用 AFE HW V2 编辑器**，也没有 direct-serial transport；它主要是 BLE 状态/调试上位机。不要再参考旧文档中不存在于当前仓库的 `BmsTool.Windows` / `BmsFactoryTest.Windows` 名称。

---

## 7. 产品身份和通信怎么改

文件：

```text
conf.h
```

当前 D011：

```c
#define FD_BMS_TYPE                    D11
#define SeriesNum                      SH3673510_D011_CELL_COUNT
#define CapacityFactory                116
#define AFE_ODC1                       300
#define AFE_ODC2                       500
#define BMS_HARDWARE_VERDION_DEFAULT   "D011"
#define BMS_SOFTWARE_VERDION_DEFAULT   "V1.0"
#define BMS_SERIAL_NUMBER_DEFAULT      "D011-UNSET"
#define DEV_NAME_STR                   "BT_D011"
#define DEV_NAME_STR2                  "BT_D011_FACTORY"
#define MODBUS_RS485_ENABLE            1
```

改产品默认身份时从这里改；已保存 BT suffix/system参数可能继续覆盖编译默认。

### Modbus地址

`modbus_rtu.c`：

```c
#define MB_ADDR 0x01
```

### RS485方向

板级：

```c
#define D011_RS485_EN_PIN GPIO_PA1
```

改串口/方向时必须验证最后停止位发送完再翻转DE//RE，不能只改GPIO宏。

---

## 8. 温度怎么改

产品 NTC nominal：

```c
SH3673510_D011_NTC_NOMINAL_OHM 10000UL
```

温度换算表/硬件阈值编码在：

```text
sh3673510_control.c
```

当前 TS1/TS2 作为电池温度参与 AFE HW temp；TS3/TS4 是产品软件策略通道，不要只改 SCONF6 就认为温度策略完成。

D011 原图 RN3/RN4 标10M，但实际装配已按10K确认；如果硬件BOM再变化，先修订硬件资料，再改代码。

---

## 9. SOC / Flash / Sleep

SOC profile：

```text
bms_soc_profile.h
```

Flash：

```text
flash_store_cfg.h
```

低功耗/BLE主流程：

```text
app.c
```

AFE Sleep/Powerdown：

```text
sh3673510_control.c
sh3673510_bms.c
```

任何 sleep/wake 修改都必须一起核对 `ALARM`、`CMNT-WK`、C-3V3通信供电和WDT/Powerdown。

---

# 10. D011 固件编译

```bat
cd /d D:\work\D011-BMS
python bms_tools\bms.py env
python bms_tools\bms.py sources --check
python bms_tools\bms.py rebuild --jobs 4
python bms_tools\bms.py check-fw
python bms_tools\bms.py size
python bms_tools\bms.py map
python bms_tools\bms.py manifest
python bms_tools\bms.py verify
python bms_tools\bms.py static --no-report
```

最终BIN：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin
```

建议交付复制命名：

```text
HS-D011_TLSR8251_SH3673510_10S_<SWVER>_<shortsha>.bin
```

编译器原始输出仍保持 `825x_ble_sample.bin`。

---

# 11. Windows Qt 上位机怎么编译

工程：

```text
tools/BMSAssistantQt
```

依赖：

```text
Python 3.9+
PySide6 >=6.8,<7
PyInstaller >=6.10,<7
QtBluetooth
```

### 从拉代码直接运行

```bat
git clone --single-branch --branch feature/sh3673510-d011-bms https://github.com/CS19970929/telink-new-sdk-b85.git D011-BMS
cd /d D011-BMS\tools\BMSAssistantQt
scripts\run.bat
```

D011 当前工具默认：

```python
RegisterCatalog.currentProjectSeriesCount = 10
```

与 D011 10S 一致，正常无需改。

### 打包

```bat
cd /d D:\work\D011-BMS\tools\BMSAssistantQt
scripts\package-windows.bat
```

输出：

```text
.dist\BMSAssistantQt\BMSAssistantQt.exe
.dist\Launch-BMSAssistantQt.bat
.dist\docs\README.md
.dist\docs\WINDOWS-DELIVERY.md
```

客户推荐运行：

```text
Launch-BMSAssistantQt.bat
```

建议交付包名：

```text
BMSAssistantQt_HS-D011_10S_<date>_<shortsha>.zip
```

---

## 12. 修改后最小检查

```text
1. git diff
2. sources --check
3. SH family + D011 integration/protection/temp/comm contracts
4. software protection + AFE HW profile contracts
5. TC32 rebuild/check-fw/map/verify
6. cppcheck
7. BMSAssistantQt smoke/run/package
8. D011实板保护、RS485、wake、MOS、NTC验证
```
