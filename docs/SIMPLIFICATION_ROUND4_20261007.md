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
