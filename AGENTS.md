# AGENTS.md — BMS monorepo

这是 D008 / D011 / D013 / D014 的统一源码仓库。固件后续以 `codex-bms-monorepo` 为主要开发分支。先读 `README.md` 的完整文档导航及相关产品 reference。
涉及 AFE 寄存器、采样/校准、保护恢复、MOS/均衡、低功耗/通信，或默认/持久化/OTA AFE 配置时，先读 `docs/AFE_REFERENCE_GUIDE.md` 和其中的固定提交审查入口；按对应原 PDF 页码建立依据，明确型号/版本、硬件条件与未决项。原资料在 `references/afe/`，临时解析/渲染输出仍在源码树外。
首次接手读 `docs/ONBOARDING.md`；修改配置读 `docs/CONFIGURATION_AND_BUILD_GUIDE.md` 和 `docs/OTA_PARAMETERS.md`；
构建/测试读 `docs/BUILD_AND_TEST.md`；追调用与状态读 `docs/CODE_READING_GUIDE.md`、`docs/ARCHITECTURE.md`。
`docs/BMS_MONOREPO_VALIDATION.md` 仅为初次迁移的固定提交记录，不作为当前测试结果。
全局嵌入式规则继续适用；当前开发阶段按已确认产品约定拒绝旧 schema，不迁移旧开发板参数；同 schema 按产品更新编号处理。

- 唯一公共源码在 `bms/core` / `bms/app`。不复制到产品目录或 SDK 示例目录，不引入同步脚本。
- 产品目录只维护板级数据、feature 能力和 repository-relative `sources.txt`。
- AFE 寄存器、芯片恢复和芯片校准留在相应后端；Flash/UART/BLE/tick/GPIO 留在平台。
- SDK 和 TC32 ABI/启动/链接库保持原体系。TLSR8251 为 32 KiB SRAM，启动定义 `MCU_STARTUP_8251`。
- 公共 mA 正放电负充电；温度协议仍是 `(°C+40)*10`，容量字段单位必须核对。
- 软件保护与 AFE hardware profile 独立；SC/OCD/OCC 不能只用关 MOS 后零电流恢复。
- 通信失效必须保留 watchdog bus silence、三次有效样本资格和输出 inhibit。
- 无效串位发送 61001，不计入有效串数、min/max、SOC、保护、balance/open-wire。
- D011 PB5 fuse 安全电平不得改变；D013 heater/balance 都不支持；D014 无 heater、TS3 NC、TS4 为 MOS NTC。
- D014 原理图/BOM 与板级阻断详见产品目录 AGENTS 和 `docs/HARDWARE_VALIDATION.md`。
- 开发参数 CFG2 schema 2，旧记录拒绝；wire product ID、外部地址、缩放、Flash/OTA/APP 边界不能擅自改变。
- 公共改动至少执行四产品 `sources --check`、`link`、`resources` 和相关 host 回归。
- `link` 只产生 ELF/MAP/LST；没有明确镜像请求不得自动使用 `build`/`rebuild`/`objcopy`/`check-fw`。
- host/链接/MAP 不替代实板、Flash 掉电、低功耗、物理保护或 OTA 验收。
- 所有临时和构建输出放源码树外。重大变更附必要文档并提交本次文件。
- Windows 上位机继续以 `feature/windows-afe-hw-protection-editor-v2` 的 `bms-tool-windows/` 为维护来源。

完整本机回归入口：`python tests/run_host_regression.py`。
四目标入口：`python bms_tools/bms.py --all-products link --jobs 4`。
可移植核心库使用根目录 CMake，但 Telink 固件仍用统一 TC32 工具。

- 用户约定：今后所有 Git 提交标题、正文、合并及回退说明和 PR 变更记录均使用中文；函数名、路径、命令、型号及其它技术标识保留原文。提交前检查说明语言，不使用没有具体含义的占位标题。
- 新增、删除或改名项目文档时同步根 README 的直接链接；历史验证注明固定提交，不写成当前验收结论。

- 运行日志、串口/BLE 日志读取或调试开关改动，先读 `docs/RUNTIME_DEBUG_LOG.md`；保持量产关闭和原通信/低功耗门禁。
