# 源码配置、固定 OTA 标识与直接构建

四产品统一采用源码作为参数依据。Ctrl+Shift+B 的窗口是源码编辑入口；构建与发送读取已保存的配置，不以本机缓存或临时编译宏覆盖参数。Git 未提交修改和未跟踪文件不阻止构建或 OTA 发送。

## 配置所有者

| 内容 | D008 | D011 / D013 / D014 |
|---|---|---|
| AFE、串数、化学体系 | `d008_product_profile.h` 的默认 profile / CUSTOM 两项；AFE 固定 DVC1124 | 各产品 `bms_product.h` 的 `BMS_BUILD_AFE_MODEL`、`SH3673510_BOARD_CELL_COUNT`、`BMS_PRODUCT_CHEMISTRY` |
| 容量、保护、SOC、OCV 等默认值 | `d008_protection_config.h` | 保护/SOC/OCV：共用 `sh3673510_protection_config.h`；容量：各产品 `dXXX_protection_config.h` |
| 本包 OTA 更新组、固定标识和输入摘要 | 上述独立参数文件的 `OTA_SOURCE_POLICY` 区域 | 同左 |

原有保护默认值、GPIO、Rsense、实装能力、持久记录、协议、Flash 分区和 OTA 格式保持。D008 旧数字 profile 与显式工程验证入口保留；交付镜像所选 profile 必须与源码的默认选择一致。23S LFP 仍不扩大生产签核范围。

## 窗口操作

1. 选择产品。窗口读取该产品源码中的 AFE、串数、类型、容量和 OTA 更新组。本机旧 Profiles 缓存不再覆盖源码，只保留项目及开发/生产模式选择。
2. 修改配置。拓扑或类型变化继续联动四组 Config；容量变化联动 BUSINESS，SOC_STATE 由用户显式选择。整组更新与均衡许可保留规则见 [OTA 参数说明](OTA_PARAMETERS.md)。
3. 点击“保存到源码”。工具校验范围后修改该产品参数文件及必要的板级/装配文件；不自动暂存或提交其他修改。保存失败回滚本次已写入的文件。多文件写入不宣称断电原子性。
4. 可审查 Git 差异并按需要提交；这不是构建或发送门禁。开启“保存时制作新参数更新包”可在参数完全相同时明确创建新的执行标识，这个选择必须先保存到源码。
5. 点击“按源码编译”。窗口参数必须等于已保存源码，允许工作树存在未提交修改或未跟踪文件。保留原“仅生成 BIN / 发送安卓并请求 OTA”选项，但这些镜像操作仍须用户明确授权。

六个源码开关是 `BMS_OTA_UPDATE_SW`、`BMS_OTA_UPDATE_AFE`、`BMS_OTA_UPDATE_BUSINESS`、`BMS_OTA_UPDATE_SOC`、`BMS_OTA_UPDATE_SOC_STATE`、`BMS_OTA_UPDATE_FACTORY_RESET`。全部为 0 表示保参，标识和内容摘要也全零。公共策略按原位定义合成 mask，不新建持久格式。

## 固定标识与输入摘要

保存开启更新组的配置时，工具生成原格式的 96-bit 标识（首 word 为 0，后 80 bit 随机），写入六个 `BMS_OTA_UPDATE_ID_0..5`。同一配置重复保存、读取、编译或发送不生成新标识；参数输入或更新组变化时保存新标识。普通升级应显式选择保参。

`BMS_OTA_UPDATE_CONTENT_SHA256` 绑定产品板级/装配、参数头、公共电池默认、SOC 定义/曲线及默认值构造代码，同时包含实际装配、容量和更新组，排除标识与摘要自身以避免循环。摘要保守包含相关文件字节；注释或这些文件内其他逻辑变化也可能要求重新保存更新包。它不是物理参数读回或语义等价证明。

手工编辑参数后摘要不匹配时，工具拒绝构建，要求重新保存。工具还核对该产品 Git 历史，拒绝将历史中已使用的同一标识绑定到不同摘要。标识保存在源码中，不用 commit SHA 回写标识，避免循环依赖。

普通组由 Config/State 各自保存执行标识；出厂包先持久化 Config 进行中标记，重置 State/Event 后完成标记，同一包后续重启不重复恢复。A → B → A 的回刷语义保持原样；校准、身份、事件及均衡许可的保留规则不变。

## 命令行

读取源码，不修改或构建：

```powershell
python bms_tools/build_options.py show --product d014
```

以下示例只编辑源码，不编译或发送；200 的单位为 0.1 Ah：

```powershell
python bms_tools/build_options.py save --product d014 --capacity-0p1ah 200 --update-groups business
git diff -- bms/products/d014/
# 核对源码配置；正式归档时可另加 --require-clean：
python bms_tools/build_options.py verify --product d014
```

`save --new-package` 明确生成新标识；`save --update-groups` 后不提供任何组可保存保参策略。窗口脚本的 `-SaveOnly` 提供同等编辑入口。`-PlanOnly` 只显示当前源码配置和计划，不生成标识、BIN 或执行 OTA。

`BMS_BUILD_OPTIONS` 已停用。交付构建/发送拒绝非空 `EXTRA_DEFINES`；开发 compile/link 仍可按既有规则使用诊断宏。`android_ota.py` 不再接受容量、类型、串数、AFE 或更新组的临时选项。

## 交付追溯与验证边界

manifest 记录当前 Git commit 与 dirty 状态、产品/profile、模式、源码配置、固定标识、`parameter_inputs_sha256`、实际编译输入/工具哈希及 BIN SHA-256。发送前重新核对当前源码输入和配置，再验证文件大小及 SHA-256。未提交源码可以测试构建和 OTA；正式归档仍建议提交，但提交不代替生产批准、资源门禁或实板验收。

本轮按用户要求只执行 host、源码清单、ELF 链接和资源验证，不生成 BIN 或执行 OTA。实际结果与提交身份在交付时单独记录；源码追溯、软件模型、目标链接和实板证据分别保留，不将旧验证记录写成本轮成绩。

## 旧板按全新板初始化

- **有线烧录**：使用 BDT 擦除对应芯片的完整业务 Flash 区，并读回确认全 `FF`，再烧录当前 BIN。
  运行 `python bms_tools/bms.py flash-help` 查看 512 KiB / 1 MiB / 2 MiB 业务范围。
  不要整片擦除 SDK、MAC、配对校准或双固件 OTA metadata。
- **无线 OTA / 无法安全擦业务区的旧板**：在 Ctrl+Shift+B 中单独勾选“恢复出厂”，
  保存源码后生成固定一次性 ID，再构建并发送该 BIN。同一次出厂包不得勾选其它更新组。
- **普通升级**：全部不勾选，保持已保存参数；若设备仅有非空白旧格式，
  普通固件将禁止输出，必须重新用上述方式进行出厂初始化。
- 出厂包完成后参数均以新固件默认值为准；实板仍须确认串数、AFE、采样电阻、
  输出保护、MAC 和 SDK 信息未遭误擦。新板与旧板对比解码后的实际参数，不比较记录序号。
