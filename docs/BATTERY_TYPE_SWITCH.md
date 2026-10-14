# 四产品参数配置与电池类型切换

2026-10-14，基线 `fb70ea5a`，按用户确认在 `codex-remove-openwire` 开发。本轮仅源码与 Git 差异审查，未测试、编译、检查资源、生成镜像、OTA 或操作实板。

## 参数入口

| 产品 | 专用文件 |
|---|---|
| D008 | [d008_protection_config.h](../bms/products/d008/d008_protection_config.h) |
| D011 | [d011_protection_config.h](../bms/products/d011/d011_protection_config.h) |
| D013 | [d013_protection_config.h](../bms/products/d013/d013_protection_config.h) |
| D014 | [d014_protection_config.h](../bms/products/d014/d014_protection_config.h) |

文件集中软件保护、独立 AFE 保护、SOC 调参、两种 OCV 曲线、容量、加热/均衡和保护性休眠的首次默认。软件保护按一级、二级、三级、恢复、滤波连续排列。`*_FOR(c)` 条件左侧为 LFP、右侧为 NMC；下方别名选择编译默认，通常只编辑有数值的行。

单位：容量为 0.1 Ah，电流为 0.1 A，软件滤波为 10 ms，温度为 `(℃+40)*10`。软件总压按每串 mV 编辑，再换算为整包 10 mV。GPIO、Rsense、串数、能力、身份和通信留在 `bms_product.h`。SH 公共板级头不再持有第二份保护阈值，软件与 AFE 数值独立。

数值按当前源码搬迁，没有从旧文档恢复旧值。D008 当前 LFP 软件欠压为 3200/3100/2600/3100 mV，AFE 欠压/恢复为 2500/2800 mV；NMC 软件欠压为 3200/3100/3000/3100 mV，AFE 为 3000/3100 mV。当前 D008 过流也保留，不能按旧迁移记录的 10/15/20 A 推断。

## 已有板子 OTA 更新参数

1. 修改产品参数文件，核对实际板型、AFE、串数、类型和容量。
2. Ctrl+Shift+B 构建窗口选择更新组：软件保护选 `sw`，AFE 保护选 `afe`，容量/加热/均衡选 `business`，SOC 类型/profile/调参选 `soc`。
3. 改串数或持久电池类型须四组全选，否则保持启动输出禁止。`soc_state` 独立重置 SOC/循环，仅确实需要时选。
4. 工具自动生成新 `BMS_OTA_UPDATE_ID_0..5`，与 mask 编入镜像。无需修改 `bms_parameter_policy.h` 历史编号或增加旧 revision，也不要求客户读取旧编号。
5. OTA 后核对实际参数、启动状态及 AFE requested/effective/寄存器读回；同一更新包不重复恢复客户在线修改。详见 [OTA 参数规则](OTA_PARAMETERS.md)。

修改编译默认 OCV 曲线点时同步递增本产品 `BMS_SOC_OCV_LFP_VERSION` / `BMS_SOC_OCV_NMC_VERSION`，用于诊断追踪。`soc` 组控制 CFG2 中的持久类型/profile 和三项回退调参；设备在线保存的完整 SOC profile 位于独立 `SCP1` journal，优先于编译默认。保护性休眠和 `SCP1` 都不属于这四个 OTA 更新组；改首次默认不会覆盖健康在线记录。校准、身份和事件也不在 OTA 更新选项中。

## 一键切换与密码

App、Windows 和公共客户端代码都在 `feature/windows-afe-hw-protection-editor-v2` 的 `bms-tool-windows/` 维护。App 客户版原“进入服务设置”增加密码 `hs456`，断开后退出；Windows 客户版复用现有高级功能密码。内部 Windows/Android 的类型切换也要求密码。当前是代码默认密码，没有新增可修改密码的 Flash 字段。

切换前确认实际电芯类型，断开充电器和负载。固件检查启动参数有效、AFE 总线访问资格、三次新鲜采样资格、最新样本年龄及电流是否在当前 SOC 死区内。这是配置操作门禁，不是过流/短路物理恢复证据。

切换恢复该产品整套软件保护、整套独立 AFE 保护、加热/均衡阈值、SOC 全部算法阈值与对应 OCV 曲线。目标类型的完整 SOC profile 先写入独立 `SCP1` journal，CFG2 随后原子提交；CFG2 失败会尝试恢复原 profile。保留串数、容量、均衡许可、实测电流校准、身份、当前 SOC/循环/放电累计、事件、独立休眠记录和 OTA 执行标记。OCV 曲线切换不等于实测电量校准，当前 SOC 继续由原算法按静置资格修正。D013 heater/balance、D014 heater/TS3 能力边界不变。

D008 保留首次/OTA 的既有均衡测试组。在线切换 LFP 使用 3300/30/15 mV，NMC 使用 4100/30/15 mV；关闭测试宏后分别为 3400/50/30 和 4100/50/30 mV。D008 24S 拒绝 4.20 V NMC，沿用现有 100 V 上限。

## 事务和失败

候选整体校验后，先撤销输出授权，再保存完整 SOC profile 并将其余四组参数写入同一条 CFG2 journal 记录，成功后发布运行值并由原后端应用 AFE。CFG2 322-byte payload、schema 3、分区、CRC、最后提交标记和旧地址不变；`SCP1` 使用原未分配区的两个 sector，自有 CRC、序号和最后提交标记。

保存失败保留旧 RAM cache 并保持 inhibit；掉电后选择完整旧/新记录。AFE 应用失败保留整套已提交的新参数，保持 inhibit 并返回失败；需重新读取并按现有启动路径验证，不声称已经回滚或已经物理关断。成功应用后仍需三次新鲜样本资格，原保护故障、AFE latch/物理恢复、watchdog bus silence 和远程 MOS 许可继续约束输出。

在线恢复软件、AFE 及业务均衡默认跟随当前持久化学体系，避免 NMC 混入编译 LFP 阈值。OTA 的编译默认入口独立，继续按镜像更新选择处理。

## 新增协议

`0x2E02` 的 bit8 表示整套类型切换，bit9 表示完整 SOC profile 读写；公共参数接口版本继续为 2，避免破坏现有客户端的向前兼容门禁。客户端先探测能力和 SOC profile 独立版本，不支持时不发送写命令。

| 地址 | 内容 |
|---|---|
| `0x2EA0 / 0x2EA1` | magic `0x4348` / 类型接口版本 1 |
| `0x2EA2 / 0x2EA3` | 当前 chemistry / SOC profile：LFP=1、NMC=2 |
| `0x2EA4` | 0 尚未切换，1 已提交待应用，2 已应用待样本资格，3 失败需重新读取；RAM 状态重启清零 |
| `0x2EA5..0x2EA7` | SOC 死区 mA、静置准备 s、OCV 误差带 % |
| `0x2EA8` | 整条 4-word `0x10`：类型、`0x6873`、`0x3435`、`0x3600`，即 `hs456\0` |

请求 17 bytes，适配 MTU=23。错误长度/类型返回 3，错误密码返回 2，静置、存储或应用失败返回 4。客户端不自动重试；成功后读回类型、软件保护、AFE requested/effective、SOC 调参与均衡；TX/超时日志隐藏密码。密码为维护操作门禁，没有新增链路加密。

完整 SOC profile 使用 `0x2EB0..0x2EE6` 读取 magic/version、50-word 当前值、持久状态、stage generation 和状态。50-word 值包含 chemistry/profile/version、三项算法调参、有效电压、满/空端点、压差/末端阈值以及最多 15 个 `mV + SOC%` OCV 点。`0x2F20..0x2F51` 每次最多写 4 word 暂存；该区间避开现有独立 MOS 控制窗口 `0x2F10..0x2F17`。`0x2EF8` 使用 5-word 命令（op、generation、`hs456\0`）执行 begin、commit 或恢复当前类型默认。最大请求为 19 bytes，适配 MTU=23。只有完整候选校验、journal 保存和运行重载都成功才报告完成；App/Windows 随后完整读回核对。

Android 服务模式验证成功后停留在“设备”页并展开明确入口，不再自动跳转，避免密码确认触摸落到下一页危险按钮。“软件/硬件保护”改为单一外层滚动页；整套类型切换和 SOC/OCV 编辑归到“业务参数”。Android 与两个 Windows GUI 均显示构建时 Git commit；若源码有未提交改动，显示 `-dirty`，不得把 HEAD 冒充为可复现源码。

## 依据与验收边界

沿用 DVC1124-2 RM V1.2 原 PDF p20（SCD）、p23–24（电压保护）及 SH36735XX CV1.0A 原 PDF p49（电压范围）；寄存器编码、量化和物理恢复策略未变。实装型号后缀、硅版本、接线和阈值签核限制见 [AFE 指南](AFE_REFERENCE_GUIDE.md) 与 [硬件验证](HARDWARE_VALIDATION.md)。

补充四产品两种编译类型下的运行默认场景，以及存储夹具中的密码/长度/24S NMC、静置/样本年龄、逐字节写中断、保留域、应用失败门禁场景，均未执行。仍需构建/资源、Flash 真实掉电、BLE/串口、手机/Windows UI、AFE 读回及物理保护验收。

客户端工作树存在本轮之前的大量未提交修改，全部保留，不混入固件提交。客户端增量 patch 以本轮开始时的工作树为基线，不能视为针对客户端旧 HEAD 的独立提交。
