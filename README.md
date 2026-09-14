# HS-D011 / TLSR8251 / SH3673510 BMS

当前分支产品是 **HS-D011-10S50A-V1 + TLSR8251F512ET32 + SH3673510 + 10S**。板级连接以用户提供的 `hs-d011-10s50a-v1.pdf` 为依据；AFE 寄存器/协议以 `SH36735XX CV1.0A` 为依据。

## 当前架构

- AFE backend：SH3673510，系列共用驱动文件仍名为 `sh3673520_*`。
- 软件保护：`bms_sw_protection.*`，使用 `g_tParam.protect` First/Second/Third/Recover/Filter。
- AFE 硬件保护：独立 `bms_afe_hw_profile_t`；与软件保护分开修改/持久化。
- 产品 profile：10S、250µΩ；SPI PB6/PB7/PD7/PD2。
- 主通信：Modbus RTU over RS485，`MODBUS_RS485_ENABLE=1`。
- AFE requested/effective 通过统一 Hardware Protection V2 暴露；芯片量化由 SH backend 完成。

## 文档入口

当前产品配置只认以下入口：

- [D011 产品硬件与固件配置基线](docs/D011_PRODUCT_REFERENCE.md) — 原理图 IO、SH3673510 静态/动态 AFE 配置、温度/BOM冲突和源码事实。
- [D011 实板验证与发布阻断项](docs/HARDWARE_VALIDATION.md) — 当前唯一待测清单。
- [BMS 软件架构与配置所有权](docs/ARCHITECTURE.md)
- [软件三级保护](docs/SOFTWARE_PROTECTION.md)
- [AFE Hardware Protection V2](docs/AFE_HARDWARE_PROTECTION_V2.md)
- [SOC](docs/SOC.md)
- [Flash / Storage](docs/STORAGE.md)
- [构建与测试](docs/BUILD_AND_TEST.md)

DVC1124/D008 文档不属于 D011；历史 D011 状态/硬件重复说明已合并到 `D011_PRODUCT_REFERENCE.md`，后续追溯使用 Git 历史。

## 构建/检查

```powershell
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py map
python bms_tools/bms.py verify
python bms_tools/bms.py static --no-report
```

Host contracts 覆盖 SH driver、D011 integration、protection mode、temperature、unified software protection、independent AFE HW profile、SOC 和 Flash。

## 发布原则

编译/CI 成功不等于硬件验收。C-3V3/RS485供电与唤醒、PB5不可逆保险丝路径、TS3/TS4 BOM、SC/OC实际恢复、低功耗、Balance/Open-Wire 等必须按 `HARDWARE_VALIDATION.md` 取得实板证据。