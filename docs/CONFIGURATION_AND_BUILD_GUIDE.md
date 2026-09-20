# D013 配置修改、拉代码、固件编译与上位机构建指南

> 上位机章节中的历史 Qt 路径已废弃。当前唯一真源是
> `feature/windows-afe-hw-protection-editor-v2:bms-tool-windows/`；诊断与命令以
> `docs/D013_DIAGNOSTICS.md` 及该分支最新文档为准。

> 适用分支：`refactor/d013-common-bms-features`
>
> 当前源码 profile：TLSR8251F512ET32 + SH3673510 + 4S + 100 µΩ。
>
> **当前可访问资料中仍缺 D013 专属原理图/BOM。** 因此本文把“当前代码怎么改”讲清楚，但凡涉及 GPIO、NTC物理位置、加热/fuse、通信供电等硬件事实，都必须在拿到 D013 原理图后再签核。

---

## 1. 拉取 D013

```bash
git clone --single-branch --branch feature/sh3673510-d013-bms https://github.com/CS19970929/telink-new-sdk-b85.git D013-BMS
git -C D013-BMS branch --show-current
git -C D013-BMS rev-parse HEAD
```

更新：

```bash
cd D013-BMS
git fetch origin
git switch feature/sh3673510-d013-bms
git pull --ff-only origin feature/sh3673510-d013-bms
```

固件业务目录完整前缀：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/
```

---

## 2. 配置修改总表

| 想修改 | 应改文件 | 当前状态/注意事项 |
|---|---|---|
| 4S / Rsense / SPI / GPIO / SH静态配置 | `sh3673510_project_config.h` | 目前大量 `D011_*` 宏名沿用；没有D013原理图前不能随意重命名/改pin |
| 软件三级保护默认 | `param.h` | 只影响默认，不自动覆盖现场Cold KV |
| AFE HW Protection V2 | `bms_afe_hw_profile.*` + Cold KV | 与软件保护独立 |
| SH量化/寄存器apply | `sh3673510_control.c` | 不应拿它当普通产品参数表 |
| 芯片寄存器真值 | `sh3673520_reg.h` | 只因手册事实错误才改 |
| 产品身份/容量/BT名/通信模式 | `conf.h` | 当前明显保留 D011 identity 技术债 |
| SOC OCV/profile | `bms_soc_profile.h` | 数据化 profile |
| Modbus地址 | `modbus_rtu.c` | 当前 `MB_ADDR=0x01` |
| Flash | `flash_store_cfg.h` | 高风险兼容接口 |
| BLE/低功耗 | `app.c` | 必须配合真实D013硬件验证 |

---

## 3. 4S / 100 µΩ 怎么改

文件：

```text
sh3673510_project_config.h
```

当前源码：

```c
#define SH3673510_D011_CELL_COUNT               4u
#define SH3673510_D011_SHUNT_UOHM              100u
#define SH3673510_D011_NTC_NOMINAL_OHM         10000UL
#define SH3673510_D011_SPI_GROUP               SH3673520_SPI_GROUP_B6_B7_D2_D7
```

虽然宏仍叫 `D011_*`，在 D013 分支里它们就是当前编译输入。

### 如果未来 D013 硬件不是4S

拿到原理图后再改：

```c
SH3673510_D011_CELL_COUNT
```

同时必须核对：

```text
AFE CN字段
未使用cell通道接法
min/max/delta
软件保护
SOC
上位机 cell display count
协议/容量
```

### 如果 Rsense 不是100 µΩ

改：

```c
SH3673510_D011_SHUNT_UOHM
```

然后重新验证：

```text
CADC电流换算
OCD1/OCD2/OCC effective
SC effective
上位机 requested/effective 显示
SOC库仑积分
```

不能只改 `CS_Res/CS_Res_Num`。

---

## 4. D013 当前 GPIO 怎么改

当前源码仍继承：

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

这些定义在：

```text
sh3673510_project_config.h
```

### 当前禁止做的事

没有 D013 原理图时，不要直接：

```text
D011_* -> D013_*
```

然后认为完成硬件适配。重命名只有在每个网络都被原理图确认后才有意义。

尤其：

```text
PB5 fuse trigger
PB4 heater
PD4 CMNT-EN
PD3 CMNT-WK
PA1 RS485-EN
TS3/TS4物理位置
```

当前都不能当 D013 PCB 真值。

---

## 5. SH3673510 静态配置怎么改

文件：

```text
sh3673510_project_config.h
```

当前组合：

```text
SCONF1 0x40 = 0x00
SCONF2 0x41 = 0x50
SCONF3 0x42 = 0x44
SCONF4 0x43 = 0x64   # CN=4
SCONF5 0x44 initial = 0x3C
SCONF7 0x46 = 0x04
0x47 = 0x57
0x48 = 0xFF
```

要改具体bit，改语义宏，例如：

```text
SH3673510_D011_PD_EN
SH3673510_D011_PUMP_EN
SH3673510_D011_CGR_WK
SH3673510_D011_LD_WK_CODE
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

不要在 `sh3673510_control.c` 手写另一个整字节常量，也不要为产品调参修改 `sh3673520_reg.h` 的芯片步进定义。

SCONF6最终硬件保护enable由 persisted AFE HW profile 的 `enable_mask` 动态产生。

---

## 6. 软件 First/Second/Third 保护怎么改

文件：

```text
param.h
```

常用默认宏：

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

最终通过：

```c
E2P_PROTECT_DEFAULT_PRT
```

形成 `g_tParam.protect`。

单位规则：

```text
Filter = 10 ms/单位
温度 = (°C+40)*10
电流 = 0.1 A量级
单体电压 = mV
```

Pack voltage必须按当前 `u16VCellTotle` 和协议实际单位核对，不能从宏名猜。

### 对已出货设备

改 `param.h` 只改 default；Cold KV 已有参数不会自动被覆盖。

所以：

- 新板默认 -> 改 `param.h`；
- 现场设备 -> 通信写入；
- 升级批量改指定字段 -> 写明确 migration/epoch；
- 不要用 `PARAM_VER` 全量重置客户参数。

---

## 7. AFE Hardware Protection V2 怎么改

公共：

```text
bms_afe_hw_profile.h
bms_afe_hw_profile.c
bms_cold_kv_store.c
```

SH3673510编码：

```text
sh3673510_control.c
```

D013使用100 µΩ计算 effective current。

### 首次 migration default

函数：

```c
bms_afe_hw_profile_build_migration_default()
```

只对空profile做一次初始化；已保存profile不会因为你改这个函数自动变化。

### Modbus接口

```text
0x2500..0x2522 requested 35 words
0x2523..0x252B metadata
0x2540..0x2562 effective 35 words
function 0x42 privileged session
```

必须完整35-word原子写。

当前仓库 `BMSAssistantQt` 还没有专用 AFE HW V2 编辑器/direct-serial transport。旧 `AFE_HARDWARE_PROTECTION_V2.md` 中曾出现的 `BmsTool.Windows/BmsFactoryTest.Windows` 不属于当前仓库实际工程，不应再作为构建入口。

---

## 8. D013 产品身份怎么改

文件：

```text
conf.h
```

当前源码仍是：

```c
#define FD_BMS_TYPE                    D11
#define BMS_HARDWARE_VERDION_DEFAULT   "D011"
#define BMS_SERIAL_NUMBER_DEFAULT      "D011-UNSET"
#define DEV_NAME_STR                   "BT_D011"
#define DEV_NAME_STR2                  "BT_D011_FACTORY"
#define CapacityFactory                116
#define AFE_ODC1                       300
#define AFE_ODC2                       500
#define MODBUS_RS485_ENABLE            0
```

### 拿到D013产品资料之后怎么处理

可以修改字符串类：

```text
BMS_HARDWARE_VERDION_DEFAULT
BMS_SERIAL_NUMBER_DEFAULT
DEV_NAME_STR
DEV_NAME_STR2
BMS_SOFTWARE_VERDION_DEFAULT
CapacityFactory
```

但 **`FD_BMS_TYPE D11` 不能简单改成 `D13`**，因为当前 enum 里没有 D13，并且数值可能参与协议/存储兼容。要新增 D013 product ID，必须一起评审：

```text
numeric ID
Cold KV
上位机
协议
历史设备兼容
factory reset/migration
```

在这之前，文档只记录它是历史兼容残留，不伪造一个新值。

---

## 9. 当前通信模式怎么改

`conf.h` 当前：

```c
#define MODBUS_RS485_ENABLE 0
```

即当前分支按 direct UART product mode 编译。

如果 D013 原理图最终确认是 RS485，不是只把 `0` 改 `1` 就结束，还必须核对：

```text
PA1是否真的是DE//RE
隔离器/收发器
TX/RX pin
方向时序
wake pin
供电使能
```

### Modbus地址

```text
modbus_rtu.c
```

```c
#define MB_ADDR 0x01
```

---

## 10. SOC / Flash / BLE / 低功耗

SOC OCV/profile：

```text
bms_soc_profile.h
```

Flash：

```text
flash_store_cfg.h
```

BLE广播/RF/connection/主低功耗：

```text
app.c
```

AFE sleep/powerdown：

```text
sh3673510_control.c
sh3673510_bms.c
```

没有 D013 原理图时，低功耗 wake pin 和外部电源时序只能当代码假设，必须补硬件证据。

---

# 11. D013 固件编译

```bat
cd /d D:\work\D013-BMS
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

在D013原理图未确认前，建议内部交付命名不要把未验证硬件事实包装成已确认事实：

```text
D013_CODEPROFILE_SH3673510_4S_<SWVER>_<shortsha>.bin
```

硬件确认后再使用：

```text
D013_TLSR8251_SH3673510_4S_<SWVER>_<shortsha>.bin
```

---

# 12. Windows Qt 上位机怎么编译

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

## 12.1 从拉代码直接运行

```bat
git clone --single-branch --branch feature/sh3673510-d013-bms https://github.com/CS19970929/telink-new-sdk-b85.git D013-BMS
cd /d D013-BMS\tools\BMSAssistantQt
scripts\run.bat
```

## 12.2 D013上位机串数必须先改

文件：

```text
tools/BMSAssistantQt/bmsassistantqt/protocol.py
```

当前共享历史值：

```python
class RegisterCatalog:
    currentProjectSeriesCount = 10
```

D013当前源码profile是4S，因此打包前改为：

```python
currentProjectSeriesCount = 4
```

否则单体显示数量错误。

以后更合理的方案是让上位机从设备metadata自动读取串数，而不是手工编译常量；当前代码尚未实现。

## 12.3 打包

```bat
cd /d D:\work\D013-BMS\tools\BMSAssistantQt
scripts\package-windows.bat
```

输出：

```text
.dist\BMSAssistantQt\BMSAssistantQt.exe
.dist\Launch-BMSAssistantQt.bat
.dist\docs\README.md
.dist\docs\WINDOWS-DELIVERY.md
```

客户/测试人员推荐启动：

```text
Launch-BMSAssistantQt.bat
```

D013原理图确认前建议包名：

```text
BMSAssistantQt_D013_CODEPROFILE_4S_<date>_<shortsha>.zip
```

---

## 13. 修改后的最小验收

```text
1. git diff
2. sources --check
3. SH family + D013 integration contracts
4. software protection + AFE HW profile + SOC/Flash contracts
5. TC32 rebuild/check-fw/map/verify
6. cppcheck
7. BMSAssistantQt smoke/run/package
8. 拿D013原理图逐网复核
9. D013实板测量与保护验证
```
