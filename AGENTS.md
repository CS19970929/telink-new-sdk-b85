# AGENTS.md — BMS monorepo

这是 D008 / D011 / D013 / D014 的统一源码仓库。直接在本仓库的 `codex-bms-deep-simplification-20261016` 分支开发，不另建 monorepo worktree。先读 `README.md` 的完整文档导航及相关产品 reference。
涉及 AFE 寄存器、采样/校准、保护恢复、MOS/均衡、低功耗/通信，或默认/持久化/OTA AFE 配置时，先读 `docs/AFE_REFERENCE_GUIDE.md` 和其中的固定提交审查入口；按对应原 PDF 页码建立依据，明确型号/版本、硬件条件与未决项。原资料在 `references/afe/`，临时解析/渲染输出仍在源码树外。
首次接手读 `docs/ONBOARDING.md`；修改配置读 `docs/CONFIGURATION_AND_BUILD_GUIDE.md` 和 `docs/OTA_PARAMETERS.md`；
构建/测试读 `docs/BUILD_AND_TEST.md`；追调用与状态读 `docs/CODE_READING_GUIDE.md`、`docs/ARCHITECTURE.md`。
`docs/history/BMS_MONOREPO_VALIDATION.md` 仅为初次迁移的固定提交记录，不作为当前测试结果。
全局嵌入式规则继续适用；当前 Config/State/Event 使用 schema 4；不解析旧格式；旧板刷出厂包须明确选择 `factory_reset` 或有线烧录擦除全部业务扇区；普通镜像遇非空白旧存储/损坏数据禁止输出。详见 `docs/STORAGE.md`；OTA 按 BIN 内参数组选择和一次性标记处理，详见 `docs/OTA_PARAMETERS.md`，不读取客户旧编号。

## 本机验证约定

- 默认完成源码审查、调用链/状态/异常路径核对和 Git 差异检查。只有用户明确要求测试、编译或运行验证时，才执行 `sources --check`、host 回归、ASan/UBSan、静态分析工具、目标编译/链接和资源检查。
- 用户指定测试范围时按指定范围执行；仅要求“测试”时选择受影响模块的相关检查。只有明确要求“完整测试”“全量回归”等完整范围时才执行完整耗时套件。
- 项目文档中的构建和测试命令是按需入口，不构成自动执行授权。交付中明确注明未执行的编译/测试，不把源码审查或历史结果写成当前测试通过。

## 项目边界

- 唯一公共源码在 `bms/core` / `bms/app`。不复制到产品目录或 SDK 示例目录，不引入同步脚本。
- 产品目录只维护板级数据、feature 能力和 repository-relative `sources.txt`。
- AFE 寄存器、芯片恢复和芯片校准留在相应后端；Flash/UART/BLE/tick/GPIO 留在平台。
- SDK 和 TC32 ABI/启动/链接库保持原体系。TLSR8251 为 32 KiB SRAM，启动定义 `MCU_STARTUP_8251`。
- 公共 mA 正放电负充电；温度协议仍是 `(°C+40)*10`，容量字段单位必须核对。
- 软件保护与 AFE hardware profile 独立；SC/OCD/OCC 不能只用关 MOS 后零电流恢复。
- AFE 通信异常持续 1 秒才确认故障；保留 watchdog bus silence 和输出 inhibit。删除 ADC 活性、停更故障及三次新样本门禁；完整通信和配置验证后恢复，真实保护仍按物理条件恢复。详见 `docs/AFE_COMMUNICATION_POLICY.md`。
- 无效串位发送 61001，不计入有效串数、min/max、SOC、保护、balance/open-wire。
- D011 PB5 fuse 安全电平不得改变；D013 heater/balance 都不支持；D014 无 heater、TS3 NC、TS4 为 MOS NTC。
- D014 原理图/BOM 与板级阻断详见产品目录 AGENTS 和 `docs/HARDWARE_VALIDATION.md`。
- 外部参数协议 v2 保持；Config/State/Event 内部 schema 4 和三域布局按 `docs/STORAGE.md` 维护，出厂重置只有 Config/State/Event 完成持久提交后才取得输出资格；wire product ID、外部地址、缩放、Flash/OTA/APP 边界不能擅自改变。
- 用户要求测试且未限定更小范围时，公共改动至少执行四产品 `sources --check`、`link`、`resources` 和相关 host 回归。
- `link` 只产生 ELF/MAP/LST；没有明确镜像请求不得自动使用 `build`/`rebuild`/`objcopy`/`check-fw`。
- host/链接/MAP 不替代实板、Flash 掉电、低功耗、物理保护或 OTA 验收。
- 对象、ELF/MAP、raw BIN、日志和临时文件放源码树外；最终校验通过的 BIN 和 manifest 放 `firmware/<mode-profile>/<product>/`，不提交生成产物。批量镜像命令和 D008 profile 选择见 `docs/BUILD_AND_TEST.md`。重大变更附必要文档并提交本次文件。
- Windows 上位机继续以 `feature/windows-afe-hw-protection-editor-v2` 的 `bms-tool-windows/` 为维护来源。
- 生成 Windows 上位机时，正式发布包统一交付到当前固件项目根目录的 `releases/bms-tool-windows/<发布标签>/`，客户版、内部版、CLI 分目录保存；即使源码位于其他 worktree，也以用户当前固件项目为交付根目录。发布标签使用时间戳或明确版本，保留旧包；记录源码提交、未提交修改状态及 EXE SHA-256。编译缓存、obj/bin 和临时日志仍放 `%LOCALAPPDATA%/CodexTemp/`，正式发布包不提交 Git。

完整本机回归入口：`python tests/run_host_regression.py`。
四目标入口：`python bms_tools/bms.py --all-products link --jobs 4`。
可移植核心库使用根目录 CMake，但 Telink 固件仍用统一 TC32 工具。

- 用户约定：今后所有 Git 提交标题、正文、合并及回退说明和 PR 变更记录均使用中文；函数名、路径、命令、型号及其它技术标识保留原文。提交前检查说明语言，不使用没有具体含义的占位标题。
- 新增、删除或改名项目文档时同步根 README 的直接链接；历史验证注明固定提交，不写成当前验收结论。

- 运行日志、串口/BLE 日志读取或调试开关改动，先读 `docs/RUNTIME_DEBUG_LOG.md`；保持量产关闭和原通信/低功耗门禁。
