# HS-D008 / TLSR8251 / DVC1124 BMS

当前分支产品是 **HS-D008 + TLSR8251F512ET32 + DVC1124-2**。分支历史名为 `feature/sh3673510-d013-bmsdvc`，名称含 D013/SH3673510 但实际硬件不是 SH3673510。

默认 Product Profile 为 **24S LFP**，另支持编译为 **20S NMC**。产品串数/化学体系与容量、OV/UV、OC、温度等最终量产参数是不同层次；后者必须单独签核。

## 当前架构

- 软件保护：统一 `bms_sw_protection.*`，参数仍为 `g_tParam.protect` 的 First/Second/Third/Recover/Filter。
- AFE 硬件保护：独立 `bms_afe_hw_profile_t`，与软件三级参数分开持久化和修改。
- AFE backend：DVC1124；业务层通过 `bms_afe.h`。
- AFE 通信异常：output inhibit + 有界 reinit + 连续有效 snapshot 恢复资格。
- SOC：LFP/NMC profile 数据化；24S/20S product profile 决定物理通道。
- Windows 工具通过统一 AFE Hardware Protection V2 读取 requested/effective，不直接暴露 raw DVC 寄存器作为普通产品参数。

## 文档入口

当前产品配置只认以下入口：

- [D008 产品硬件与固件配置基线](docs/D008_PRODUCT_REFERENCE.md) — IO、DVC GP/寄存器、24S/20S、已知不确定项。
- [D008 实板验证与发布阻断项](docs/HARDWARE_VALIDATION.md) — 当前唯一待测清单。
- [BMS 软件架构与配置所有权](docs/ARCHITECTURE.md)
- [软件三级保护](docs/SOFTWARE_PROTECTION.md)
- [AFE Hardware Protection V2](docs/AFE_HARDWARE_PROTECTION_V2.md)
- [SOC](docs/SOC.md)
- [Flash / Storage](docs/STORAGE.md)
- [构建与测试](docs/BUILD_AND_TEST.md)
- [分支策略](docs/BRANCH_STRATEGY.md)

历史日期型审计、旧 DVC 参数说明和旧任务清单已从当前文档入口移除；需要追溯时使用 Git 历史，不再把它们当设计真值。

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

Host contracts 至少覆盖 DVC config、D008 framework、20S profile、software protection、AFE HW profile、SOC 和 Flash。

## 发布原则

源码、Host contracts 和 TC32 CI 通过只证明软件/构建基线。SCD、dead-bus硬件安全路径、NTC/BOM、Open-Wire/Balance、24S/20S最终产品参数仍必须按 `HARDWARE_VALIDATION.md` 完成实板证据后才能宣称量产完成。