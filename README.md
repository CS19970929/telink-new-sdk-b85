# D008 独立量产固件

本分支维护 HS-D008，使用 TLSR8251、DVC1124 和 Telink B85 SDK。四产品基线保留在 `codex-bms-deep-simplification-20261016` 的 `2e02f32e9af2`。

## 装配与构建

- 量产候选包括 16S LFP、20S NMC、23S LFP、24S LFP 和受控自定义装配。串数、电芯和 Rsense 以签核装配为准，示例默认仍需实物验收。
- 板级入口位于 [D008 产品目录](bms/products/d008/AGENTS.md)，唯一编译清单为 [sources.txt](bms/products/d008/sources.txt)。保留 B85 启动、链接 ABI、供应商库和生产 core 禁用跳转表的设置。
- 生产链接入口为 `python bms_tools/bms.py --production --d008-profile 16s-lfp link --jobs 4`。它产生 ELF/MAP/LST。用同样的配置参数执行 `resources` 查看资源。各量产装配分别核对。
- 配置用 `python bms_tools/build_options.py show` 查看，按需保存到源码。普通 OTA 默认保留客户数据，专用出厂操作清理业务区。生成固件与发布需符合本分支签核。
- 完整主机回归入口为 `python tests/run_host_regression.py`。本机验证按 [协作规则](AGENTS.md) 执行。工程产物写到源树外。

## 资料

- [D008 板级参考](docs/D008_PRODUCT_REFERENCE.md)、[24S 原理图](references/hardware/d008/HS-D008-24S100A-V1.pdf)、[AFE 原厂手册](docs/AFE_REFERENCE_GUIDE.md)
- [保护参数](docs/D008_PROTECTION_CONFIGURATION.md)、[电流校准](docs/CURRENT_CALIBRATION.md)、[低功耗](docs/LOW_POWER_POLICY.md)、[串口升级](docs/SERIAL_OTA.md)
- [历史四产品资料导航](docs/README.md)、[原硬件验收记录](docs/HARDWARE_VALIDATION.md)

历史资料按当时提交使用。当前编译输入、默认参数和通过的验证以本分支源码和对应收据为准。
