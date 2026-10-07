# 第四轮维护入口简化

固定实施基线为 `5d95a6e29a962b5dbf2bd3f90cf93c500e2cda84`。需求为审核“完整审核与简化建议”最新第四轮方案，并实施可行部分。

## 审核结论与调整

历史命名、混合配置头、恒零 learning 诊断与零散状态 getter 确实增加阅读成本，适合本轮清理。保护参数保持原 65-word 字段顺序，采用现有字段的 designated initializer；不改成嵌套布局。AFE aux、feature、charge source 的采样资格不同，只审核消费者，不合并为通用快照。状态各归原所有者，不增加总 context，也不机械限制每模块全局变量数量。

原建议中的旧老化 no-op 删除是明确的开发期协议行为调整：`0x2E10=6` 将返回非法值；其他地址、缩放、端序、参数分组、更新编号、Flash/OTA 边界保持。上位机对应入口先检查真实维护分支消费者再处理。

## 第一批：参数所有者与默认值

- `param.c/h` 更名为 `bms_parameters.c/h`；`bms_parameters_init()` 读取并校验软件保护，`bms_parameters_startup()` 保留 Config/State/Event 启动资格。
- `bms_protection_params_t` 保留 65 个原字段顺序，`g_bms_protection_params` 代替只有 protect 字段的 `PARAM_T/g_tParam` wrapper。
- 没有生产消费者的 `SaveParam()` 删除。在线写入继续调用 `bms_protection_params_commit()`；候选校验和保存成功后才发布，不清除失败的启动门禁。
- 默认值继续由 Config default builder 持有，改为 `s_default_protection` 逐字段初始化。独立 AFE default builder 取得编译默认副本后沿原量化/覆盖路径构造，未读取运行保护参数。
- 删除旧默认宏、注释中的乱码；保留原来源版权信息，不将维护清理写成硬件参数签核。

四产品及 D008 三 profile 的修改前软件保护 65 字、AFE requested 35 字、串数/容量已在源码树外冻结，修改后逐项比较。CFG2 payload 322 bytes、State payload 44 bytes、journal schema 3 及参数更新编号保持。

## 验证边界

临时源快照、脚本、对象、ELF/MAP、日志位于 `%LOCALAPPDATA%/CodexTemp/bms-round4-20261007/`。本轮不生成固件 BIN，不烧录、不执行 OTA；host、链接/资源与实板证据分开。

实际 SDK 继续包含上一轮已记录的官方 Patch_0001 覆盖（`common/sdk_version.h`、`drivers/B85/clock.c`）。已冻结实际字节哈希，不能仅凭 Windows Git stat 判断源码一致。本轮不修改或提交这两个文件。

后续配置、诊断与命名批次的实现、验证结果在本文件追加，不把预期写成通过结果。

第一批验证：四产品 sources --check、开发 link/resources 通过，TC32 零错误/警告。六种配置的保护默认值、AFE requested、串数/容量和字段顺序与基线一致。完整 host 初次执行 110 组，其中六处旧名称夹具失败；修正 TU 清单和诊断夹具类型后，这六组重跑全部通过。最终固定提交全量回归将在各批完成后执行；初次失败日志保留。

## 第二批：拆除混合配置头

删除 `conf.h` 和 `UINT8/UINT16/UINT32/INT8/INT16/INT32` 别名，公共声明使用 `stdint.h`；State 的实现同步采用与声明一致的固定宽度类型。初始 SOC 60 保持，State/Event 保存周期分别归实现，共享失败重试周期归 storage platform。D008 的 200 mA 测量可信下限归 SOC 契约，AFE 消费者显式引用。产品宏和 SDK 类型由实际消费者显式 include。

修正了首次链接暴露的传递 include 依赖和一处旧测试夹具拼接；最终四产品开发 link/resources 零错误/警告。11 组相关回归在固定源码快照全部通过；六种配置默认值和 65 字字段顺序保持。main/IRQ、UART、SIF、BLE、board、app 的四产品预处理活动分支共 24 个 TU 与基线一致（仅归一化已审查的命名/类型变化）；该比较不代替硬件时序或寄存器值验证。中途测试因源码变化被 runner 判为未完成的记录保留，未用作固定快照验收。

## 第三批：单一产品配置入口

四产品各保留一个 `bms_product.h`，D008 的三种串数/chemistry 选择另保留 `d008_product_profile.h`。原三个或四个配置头合并；产品 GPIO、NTC、Rsense、身份/容量/通信与批准标志均保留原值和 guards。SH 默认输入仍共用 `sh3673510_defaults.h`，寄存器公式/隔离策略留在 SH 后端；实际消费者显式引入后端配置，避免产品头与编码头循环依赖。D011 PB5 safe=0、D013 heater/balance=0、D014 TS3 NC/TS4 MOS 与原配置保持。

六种配置默认值逐字一致，488 项产品宏输入逐项一致；四产品 sources、开发 link/resources 零错误/警告，14 组配置/门禁/清单回归全部通过。24 个关键 TU 活动分支仍一致。生产批准标志仍为待签核状态，未生成生产镜像；产品文档和入门路径同步更新，固定提交历史审查入口保留原路径。

## 第四批：运行快照与诊断入口

`bms_soc_diag_t` 删除十个恒零 learning 成员；一次 `bms_diag_runtime_soc(const bms_soc_diag_t *)` 发布基础/扩展诊断，取代十二参数调用和第二次扩展调用。旧 wire offsets 210/211/212、243..247 及 flags bit0/1 保持零，地址和版本保持；恒零兼容仅在诊断缓存中。一次提交变化序号，空指针和重复快照不改变序号。

九个纯 RAM getter 合并为 `bms_features_get_status()`。保留 outputs_blocked、charge_direction_blocked、diag_reasons 的决策入口和原状态所有者；DVC SOC 继续使用 active OR 最后一帧 COW，SH 继续使用当前 active。没有新增总 context。完整 feature 状态机、温度/电压资格和安全阻断决策保持。

AFE aux 消费者（app 的新鲜电流/时间、参数电流诊断、runtime raw diag）、feature snapshot 消费者（独立温度/有效性）、charge-source 消费者（board/backend 物理资格）已核对。三者资格不同，保留各自入口与判断；没有把 cached value 当作统一有效样本。

四产品开发 link/resources 零错误/警告；14 组相关回归初次有一处联合 SOC 夹具路径失败，修正函数抽取调用后又暴露模拟 heater/balance 位仍在旧 getter 外。夹具改为经 feature 状态输入，联合真实 Open-Wire/SOC 策略及原静置取消场景重跑通过；其余 13 组通过。保留失败记录，最终全量固定快照回归另证。既有 CSV 测试输出中的历史列保持零，不占用生产诊断结构。

## 第五批：状态名称与协议所有者

`g_bms_report/bms_report_t`、`g_bms_soc/bms_soc_state_t` 和 SOC/fault 子类型代替历史大小写名称。保留结构字段顺序、bit-field 顺序及原修改入口，不拆分 SOC/DVC 实现，不增加总 context。TC32 使用实际 `-fpack-struct/-fshort-enums` 编译后，四产品共 148 项尺寸/字段偏移观测与基线相同；99 项关键活动代码比较在归一化已审查名称和类型后一致，包括保护、AFE guard、SOC、通信、中断、低功耗和 feature 阻断决策。

`modbus_rtu.h` 只公开帧容量、CRC 和解析 API。DVC 配置/原始诊断窗口归 `dvc1124_config_service.h`，生产信息地址归 `bms_parameter_access.h`。业务 wire 地址改为有含义的常量，保持原数值；不用寄存器描述表。生产信息缓存仍由 Modbus 实现持有，`bms_product_info_refresh()` 仅刷新 RAM 信息。删除没有消费者的版本前后缀和重复 AFE 窗口定义。

删除 `0x2E10=6` 的成功 no-op；现在返回非法值 3，不写 Flash、不递增成功序号，授权会话也不改变该结果。软件/AFE/State 三个重置命令 1/2/3、SN 分段提交、电流校准和 AFE 独占授权继续保留。已有 FactoryMode 只读诊断仍为零，不能作为写入资格。

本批首次全量 host 为 97/110；失败定位到瘦头文件暴露的两处生产 TU 传递依赖，以及旧解析/诊断/SH 存储夹具。生产 TU 显式引入产品/时钟配置；夹具从真实所有者头读取命名地址、删除重复宏。13 组失败场景修正后全部重跑通过，原失败日志保留。最终固定提交的全量结果另列，不将拼接重跑当作一次完整验收。

## Windows 维护分支同步

在真实维护分支 `feature/windows-afe-hw-protection-editor-v2` 的已有工作区提交 `ca37d7f17088de87f4460ac33a0d4193be001db2`。删除老化按钮、写命令和 SN/电流校准的老化资格检查；敏感写入仍使用 AFE 授权、失败/完成清理会话和读回确认。客户版 SN 只读限制保持。保留 CLI 的 FactoryMode 只读导出字段以保持 JSON 结构，值不参与授权。

`test-d008-parameters.ps1` 通过；客户版、内部版、CLI 三个 Release 构建均为零错误/警告，输出位于源码树外。测试包含授权失败无 SN 写入、会话清理、完整分段 SN 写入、读回、未来协议版本拒绝以及不发送废弃命令。本地构建和模拟通信不证明实机 Windows/BLE/串口联调或产品签核。
