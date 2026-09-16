# HS-D008 / TLSR8251 / DVC1124 BMS

当前分支 `refactor/d008-common-bms-features` 的产品是 **HS-D008 + TLSR8251F512ET32 + DVC1124-2**。

默认 Product Profile 为 **24S LFP**，另支持编译为 **20S NMC**。产品串数/化学体系与容量、OV/UV、OC、温度等最终量产参数是不同层次；后者必须单独签核。

## 当前架构

- 软件保护：统一 `bms_sw_protection.*`，参数为 `g_tParam.protect` 的 First/Second/Third/Recover/Filter。
- AFE 硬件保护：独立 `bms_afe_hw_profile_t`，与软件三级参数分开持久化和修改。
- AFE backend：DVC1124；业务层通过 `bms_afe.h`。
- AFE 通信异常：output inhibit + 有界 reinit + 连续有效 snapshot 恢复资格。
- SOC：LFP/NMC profile 数据化；24S/20S product profile 决定物理通道。
- Windows 上位机唯一真源位于分支 `feature/windows-afe-hw-protection-editor-v2` 的 `bms-tool-windows/`；本固件分支不维护客户端副本。

## 文档入口

建议阅读顺序：

1. [D008 配置修改、拉代码、固件编译与上位机构建指南](docs/CONFIGURATION_AND_BUILD_GUIDE.md) — 想改什么、改哪儿、具体怎么改，以及完整 clone/build/package 命令。
2. [D008 产品硬件与固件配置基线](docs/D008_PRODUCT_REFERENCE.md) — IO、DVC GP/寄存器、24S/20S、已知不确定项。
3. [D008 实板验证与发布阻断项](docs/HARDWARE_VALIDATION.md) — 当前唯一待测清单。
4. [BMS 软件架构与配置所有权](docs/ARCHITECTURE.md)
5. [软件三级保护](docs/SOFTWARE_PROTECTION.md)
6. [AFE Hardware Protection V2](docs/AFE_HARDWARE_PROTECTION_V2.md)
7. [SOC](docs/SOC.md)
8. [Flash / Storage](docs/STORAGE.md)
9. [构建与测试](docs/BUILD_AND_TEST.md)
10. Windows 上位机：切换到 `feature/windows-afe-hw-protection-editor-v2`，使用 `bms-tool-windows/` 下的客户版与内部测试版。

历史日期型审计、旧 DVC 参数说明、旧任务清单和旧分支迁移说明已移除；需要追溯时使用 Git 历史。

## 快速拉取

```bash
git clone --single-branch --branch refactor/d008-common-bms-features https://github.com/CS19970929/telink-new-sdk-b85.git D008-BMS
cd D008-BMS
```

## 固件构建

```powershell
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static --no-report
```

最终烧录文件：

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin
```

## Windows 上位机

切换到 `feature/windows-afe-hw-protection-editor-v2`，以
`bms-tool-windows/BmsTool.Windows/` 和
`bms-tool-windows/BmsFactoryTest.Windows/` 为唯一构建、测试和发布依据。
历史 `tools/` 客户端不得恢复或用于协议判断。完整边界见
`docs/CONFIGURATION_AND_BUILD_GUIDE.md`。

## 发布原则

源码、Host contracts 和 TC32 CI 通过只证明软件/构建基线。SCD、dead-bus硬件安全路径、NTC/BOM、Open-Wire/Balance、24S/20S最终产品参数仍必须按 `HARDWARE_VALIDATION.md` 完成实板证据后才能宣称量产完成。
