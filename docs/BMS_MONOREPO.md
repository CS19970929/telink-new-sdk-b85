# 四产品单一源码实现

日常交接入口见 [根 README](../README.md)、[上手指南](ONBOARDING.md) 与 [配置指南](CONFIGURATION_AND_BUILD_GUIDE.md)。本页保留组织选择及导入追溯背景。

## 选择及范围

实现采用同一个 Git repository 内的 monorepo。`bms/core` 是公共业务源码的唯一维护位置；
不是四个 `common` 副本，也没有 build 前复制脚本。每个产品的 `sources.txt` 直接引用这些相同的仓库相对路径。
一次提交同时包含公共行为、产品配置和回归，因此公共修改与四个产品的使用版本不会漂移。

| 方案 | 在当前项目中的结果 |
|---|---|
| Git submodule / 独立 bms-core repo | 能维护唯一库源码，但四仓库仍各自固定 commit，需要四次依赖升级；不满足当前直接使用要求 |
| Git subtree | 各项目仍有本地副本，更新要合并；适合分发，不能保证开发期间同步 |
| 源码同步工具 | 多副本和冲突仍存在，生成时机影响实际编译内容；不采用 |
| CMake library | 是编译组织方式，不能单独解决四份仓库的版本问题；这里用于真正独立于 SDK 的核心子集 |
| monorepo | 公共源码、板级配置和四目标验证处于同一提交；当前主方案 |

当前四个固件仍使用官方 Telink TC32 ABI、启动文件、链接脚本和 SDK 库。
没有把固件构建强行替换为 CMake，没有引入 RTOS、动态分配、通用事件总线或多层 service/manager。

## 文件与责任

```text
bms/
  core/                    # 公共保护、SOC、参数、存储记录、日志、Modbus、诊断、状态
  app/                     # 公共业务调度与 heater/balance/open-wire 策略
  afe/
    dvc1124/               # DVC 寄存器、测量、配置、诊断和恢复实现
    sh3673510/             # D011/D013/D014 共用 SH 驱动与恢复实现
  platform/telink/          # SDK/Flash/UART/BLE/启动/板级 GPIO
  products/
    d008/                  # 产品选择、D008 profile、板级输入和源码清单
    d011/                  # 产品数据和源码清单
    d013/
    d014/
    baselines.json         # 发布快照、对照 commit 与缺失历史记录
CMakeLists.txt             # 不依赖 SDK 的 bms_core 静态库和 host tests
```

产品目录没有业务 `.c`。SH 的寄存器组合、换算和芯片状态机仅有一份；三个 SH 产品只给真实不同的输入。
应用层中 DVC 与 SH 的启动、低功耗和 SDK 回调仍有编译期分支，这是已存在的两种硬件实现边界。
SDK `vendor/ble_sample` 只保留 `app_config.h` 转发入口，旧业务源码已移除。

| 模块 | 共享内容 | 仍受硬件或产品约束的内容 |
|---|---|---|
| 保护 | 同一软件阈值验证、滤波、恢复、分级报警与阻断实现 | AFE 硬件锁存/阈值量化/物理恢复窗口；软件与硬件 profile 独立 |
| SOC | 同一积分、OCV、休息判定、容量学习、配置和诊断 | 输入有效性、样本时间、AFE 测量方向、板级串数和化学体系 |
| 参数 | 同一定义、校验、默认生成、访问和持久化格式 | 容量、串数、Rsense、产品身份与真实能力属于产品数据 |
| 日志/存储 | 同一事件 ID、重复计数、批量 checkpoint、记录 CRC/序号/commit/轮转 | Flash 实现、地址区域、SDK OTA/写保护会话属于平台 |
| Modbus | 同一解析、长度/地址检查、事务预检、参数及诊断映射 | UART/DMA/RS485 时序；DVC 专用寄存器和 SH 实际量化值 |
| 诊断 | 同一快照、公共原因、参数/存储/运行时字段 | 各 AFE 提供缓存的芯片详情，不能绕过 bus silence |
| 校准 | 同一 offset/gain 存储、整数运算和公共入口 | DVC 上电零点学习及 AFE ADC 换算留在驱动内 |
| CRC | 公共 Modbus CRC16；公共存储记录引擎内 CRC32 | DVC 总线 CRC8 是芯片协议的一部分 |
| 状态机 | 同一错误/状态定义、软件保护状态、公共 AFE 授权/失败恢复 | 芯片初始化、sleep/wake、SC/OCD/OCC 实际恢复状态机 |

## 产品事实不能用“统一”抹掉

| 产品 | 当前编译输入 | heater / balance | 说明 |
|---|---|---|---|
| D008 | DVC1124；当前开发默认 profile 为 16S LFP，生产显式选择 | 按 D008 板级配置 | 另保留显式 24S LFP、20S NMC profile 选择；不把历史注释当默认实物配置 |
| D011 | SH3673510；10S；250 µΩ | 支持 / 支持 | PB5 fuse 保持已验证的安全电平 |
| D013 | SH3673510；4S；100 µΩ | 不支持 / 不支持 | 不因共用 feature 模块而启用硬件不存在的路径 |
| D014 | SH3673510；8S；667 µΩ | 不支持 / 支持 | TS3 NC；TS4 MOS NTC 独立软件保护；TS4 硬件保护位关闭 |

这些是导入源码的编译事实。D014 的 `CapacityFactory=116` 等仍是开发默认，不能称为实板签核值。
无效串位保持协议值 `61001`，不进入有效串数、min/max、SOC、均衡或开线计算。

## 参数、单位与新存储格式

根据用户指令，开发板旧参数不兼容、不迁移。
Config 使用 `CFG2` magic `0x43464732`，内部 journal schema 3，CFG2 格式的 322-byte payload：
4-byte 产品标识、130-byte 软件保护、20-byte 系统数据、70-byte AFE profile、24-byte BT suffix、8-byte SOC 设置、54-byte 用户参数、12-byte 分组更新编号。
所有整数显式按 LE 编码；没有保存 C struct 原始 padding。不同产品标识的 CRC-valid 记录也不会被套用。
State 使用 `0x53544200 + BMS_PRODUCT_ID`、schema 3、44-byte payload（删除老化计时）。
Event 使用 `0x45563200 + BMS_PRODUCT_ID`、schema 3、404-byte payload，包含 100 条事件、重复次数和更新编号。
不读取旧 schema；同一新 schema 内按各产品的独立更新编号保留或更新参数，见 [OTA_PARAMETERS.md](OTA_PARAMETERS.md)。
启动路径先验证和持久化需要更新的数据，再允许业务输出。
保存失败不发布新 RAM 值，启动阶段失败不能被后续 `SaveParam` 单独绕过。

公共测量电流统一为 mA、正值放电/负值充电；SH 在测量边界转换，旧协议充/放电幅值字段保持原来的语义。
公共 current offset/gain 在这个方向下生效；gain 使用 ppm，避免 TC32 缺失的 64-bit 乘除运行库。
温度暂保留已有 `(°C + 40) * 10` 编码，参数字段和协议均明确这个单位；没有仅改名字就改变 wire scaling。
容量内部名义值为 0.1 Ah、协议容量为 0.01 Ah，范围校验保持一致。

通信 numeric 产品 ID 保持已有值，D014 与 D011 仍使用 wire ID 2。
存储用内部 `BMS_PRODUCT_ID` 分别为 8/11/13/14，避免二者的参数相互误用。
SH `0x2180..0x218A` 的旧实际量化值诊断保留；公共 `0x2500` profile、`0x2A00` 诊断和 `0x2E00` 参数块由同一实现维护。
恢复默认值统一使用 `0x2E10` 命令：值 1 恢复软件保护，值 2 恢复 AFE profile，值 3 恢复业务参数；
值 6 保留当前有效授权检查后成功空操作，不再进入老化模式或写 Flash。AFE profile 写入仍需要当前有效授权。
旧 SH 路径的 `0x1102=3` 不再执行恢复，`0x2E07=1` 明确公布此规则；
`0x2E05=2` 公布 CFG2 payload 格式版本，内部 journal schema 为 3。上位机应读取公共参数接口能力后使用对应命令。

## STM32 接入边界

`bms_core` CMake target 包含软件保护、ETA 估算、公共状态、运行日志、诊断、CRC 和 storage_record；实际编译成员以根 `CMakeLists.txt` 为准。
这部分没有 Telink include，可在本机编译，也可由未来 STM32 工程链接。
SOC、参数、语义存储和 Modbus 当前四产品共享，所需产品、时钟或平台依赖由各模块显式引入；
不能把它们称为已经完成 STM32 移植。真正加入 STM32 时，把相应时钟、串口、Flash 和调度调用落实到具体平台，
继续使用本仓库中的公共算法文件；保持现有 StdPeriph 或 vendor 驱动体系。
本次没有虚构 STM32 引脚、Flash 布局、AFE 驱动或板级测试。

## 后续开发与迁移

1. 四产品从 monorepo 分支开发，旧产品分支作为来源保留；不继续双向同步公共代码。
2. 公共行为只改 `bms/core` 或公共 `bms/app`。板级默认值和能力只改产品输入。
3. 新增公共 `.c` 后更新四个产品清单并审查顺序；`sources --check` 会拒绝漏列文件和失效路径。
4. 一次公共修改运行四目标编译/链接和相应 host 回归；同一提交即四产品的使用版本。
5. 明确需要 BIN 时按目标产品生成和验证镜像，之后分别完成实板低功耗、保护、校准、Flash 掉电与 OTA 验收。
6. 未来固件 release 用仓库 commit/tag 冻结全部公共和产品输入；无需给四份 common 分别打版本。

固定来源见 `bms/products/baselines.json`。本次没有覆盖原工作树 `d014-485-test` 的未提交文件；
该测试分支在选定 D014 基线之后的实验修改不自动算入这里，需以独立功能变更审查和回归后引入。

## 2026-10-05 构建和追溯整改

SH 产品的 GPIO、NTC role/capability、静态 AFE 输入及独立 HW 默认值均由各自
`bms/products/<product>/bms_sh3673510_config.h` 提供；共享后端保留寄存器组合与芯片算法。
生产模式使用 `--production`，D008 额外指定 `--d008-profile`；生产策略作用于所有 TC32 C 编译单元。
输出按模式/profile/产品隔离，输入收据、manifest、resources 均记录配置；诊断 build flags bit2 表示生产。
生产镜像槽必须保留至少 8 KiB，低于该值链接命令失败；开发模式保留警告。

`0x2E00=0xD008` 是历史参数接口 magic，并非硬件产品 ID；与上位机维护分支的
`Shared/D008Parameters.cs` 对照后保留原值，命名为 `BMS_PARAMETER_INTERFACE_MAGIC`。

原 D008/D011/D013 导入 SHA 未上传到远端，不能恢复或伪装为已审计历史。
`baselines.json` 保留这些缺失 SHA，另锚定可下载的 `fd50730` 导入结果快照和各远端对照 commit。
对照 commit 不声称与缺失导入完全等价；`python bms_tools/verify_baselines.py --fetch`
验证可发布证据的可解析性。完整修复与验证边界见 [整改记录](history/BMS_MONOREPO_REMEDIATION.md)。
