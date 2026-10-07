# 代码阅读：从产品到状态变化

先完成 [上手指南](ONBOARDING.md) 的环境验证，再按以下顺序阅读。用符号搜索追调用，不以同名函数或旧分支内容推断当前实现。路径相对仓库根。

## 第一轮：选对产品，追启动

读目标产品 `bms_product_config.h`、`bms_product_conf.h`、`sources.txt`，再读 `bms/platform/telink/main.c`、`app_ble.c`、`bms/app/app.c`。

```text
SDK startup → main → user_init_normal（app_ble.c）→ app_init（app.c）
  board_init：先关闭业务输出授权
  bms_parameters_startup：Config/State/Event 启动校验及更新
  bms_parameters_init：读取并校验 g_bms_protection_params
  bms_afe_init → 首次 bms_afe_sample
  State → soc_param_lib_init
  UART/SIF、名称、事件、Runtime、采样唤醒初始化
  mos_update → bms_afe_set_output_enabled(1)
  main 循环 → main_loop → bms_stack_monitor_poll
```

这里列出 BMS 关键顺序，BLE/SDK 初始化的完整顺序以函数为准。最后请求开启授权仍受参数有效性和通信资格约束，不表示 MOS 已物理导通。DVC/SH 的 `main_loop` 和 `app_sample_task` 有编译期分支。

完成标准：说出哪个 `sources.txt` 决定后端，当前产品哪些功能被禁用；在代码中找到启动授权失败时的门禁。

## 第二轮：一次采样如何影响输出

读 `bms/app/app.c`、`bms/core/bms_afe_guard.c`、本产品 `dvc1124_bms.c` 或 `sh3673510_bms.c`、`bms/core/bms_sw_protection.c`、`bms/app/bms_features.c`。

```text
app_sample_wakeup：置采样到期标志
main_loop → app_sample_task（约 200 ms）
  bms_afe_sample → guard → 当前 backend 采样/恢复/测量发布
  backend → bms_sw_protection_update；合并芯片硬件故障
  guard → 合格样本及 bms_features_service
  app_update_soc_from_sample → bms_soc_process_sample
  mos_update → bms_afe_set_fets → guard → backend 最终仲裁/写寄存器
  runtime diagnostics → 下次采样唤醒安排
```

软件保护滤波在后端发布时执行，不能误以为 SOC 调用后才执行。无效样本仍将“无效”交给 SOC，以作废盲区时段；不能以缓存补帧。请求、允许输出、软件保护、AFE 锁存与寄存器缓存是不同状态；DVC 单侧保护 AUTO_DIODE 与 SH 的 FET 控制方式也不同。

完成标准：沿“温度无效 → TEMP_BREAK → 阻断”走通一遍，指出 SC/OCD/OCC 的恢复证据在哪里；不能只用关 MOS 后电流为零解释恢复。

## 第三轮：默认值为何不一定生效

读 `bms/core/bms_parameters.c`、`bms_config_store.c`、`bms_update_policy.h`、共享 `bms/products/bms_parameter_policy.h`、`bms_parameter_access.c`。

```text
产品/公共默认 → Config default builder
有效同产品内部 journal schema 3 记录 → 按类别比较更新编号
候选校验 → journal commit → 发布 RAM 值
启动任一域失败 → s_storage_startup_valid=0 → 输出资格不成立
```

在线软件写入走校验和保存；AFE 写入另走授权、完整 35-word 事务、apply/readback/rollback。参见 [参数策略](OTA_PARAMETERS.md) 和 [架构](ARCHITECTURE.md)。

完成标准：解释“只改 CapacityFactory / 改 BUSINESS 编号 / 在线写 0x2318”三者的范围差别，以及 BUSINESS 更新为何还影响 heater/balance。

## 按任务继续阅读

| 任务 | 下一组入口 |
|---|---|
| SOC 积分/OCV | `bms/core/bms_soc.c`、`bms_soc.h`、`bms_soc_profile.h`、`bms_soc_eta.c`；[SOC 说明](SOC.md) |
| Flash 保存失败 | `bms_config_store.c`、`bms_state_store.c`、`storage_record.c`、`bms/platform/telink/bms_storage_platform_telink.c` |
| AFE 硬件配置 | `bms_afe_hw_profile.c`、`bms_afe_hw_access.c`、对应 AFE control/backend 与产品配置 |
| Modbus/RS485 | `bms/core/modbus_rtu.c`、`bms_parameter_access.c`、`bms/platform/telink/modbus_uart.c` |
| BLE/OTA | `bms/platform/telink/app_att.c`、`ble_ota.c`，`app_ble.c` 的连接/SDK PM/Flash 回调及 `app.c` 的业务低功耗门禁 |
| 日志诊断 | `bms/core/bms_diag.c`、`bms_debug_log.c`、`bms/platform/telink/bms_runtime_diag.c` |
| heater/balance/open-wire | `bms/app/bms_features.c`、所选 AFE `*_feature_backend.c`、产品能力输入 |
| 状态保存 | `bms_state_store.c`；SOC/放电累计/循环统一 checkpoint，老化模式已删除 |

每次修改前确认：输入从哪里来、单位是什么、谁拥有状态、失败如何处理、哪些产品编译此文件。然后选择对应 host 测试及 [构建验证](BUILD_AND_TEST.md)。本仓库采用静态对象和明确模块边界，不为简单修改引入新 manager/service 或同步副本。
