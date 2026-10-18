# 源码导览：8 个停靠点

本页是"第一次读 BMS 源码"的顺序导览，配合 [代码阅读指南](CODE_READING_GUIDE.md) 与
[架构说明](ARCHITECTURE.md) 使用。每站给出入口片段与"停下来想一下"的问题。
先读 [当前规则](CURRENT_RULES.md) 了解哪些规则仍然有效。

## 1. 启动与主循环 — `bms/platform/telink/main.c`

```c
int deepRetWakeUp = pm_is_MCU_deepRetentionWakeup();
gpio_init(!deepRetWakeUp);
/* D008：先设置供电锁存电平再启用输出，低脉冲会切断 MCU 电源。 */
gpio_set_func(BMS_BOARD_MCU_LDO_PIN, AS_GPIO);
gpio_write(BMS_BOARD_MCU_LDO_PIN, 1u);
```

- D008 的 MCU_LDO 必须先写高再使能输出，否则会自断电；
- 深睡保留唤醒走 `user_init_deepRetn()`，普通启动走 `user_init_normal()`；
- SH 的 `while (app_power_boot_sleep_hold()) {}` 让"开关 OFF"在应用初始化之前就保持深睡；
- 主循环只做三件事：喂狗、推进 `bms_time_ms()`、`main_loop()`。

想一下：为什么 `main` 和 `irq_handler` 都在 RAM code 里？（Flash 深睡/擦写时不可取指。）

## 2. 业务调度 — `bms/app/app.c`

`app_init()` 的顺序就是安全启动顺序：注册采样唤醒 → 记录 `set_fets(1,1)` 请求 →
刷新产品信息 → 最后才 `set_output_enabled(1)`。

```c
/* 低功耗唤醒回调只置位；主循环消费后执行采样，不在回调中跑保护/SOC。 */
static volatile u8 s_sample_due;
```

想一下：为什么中断里不直接采样？（总线事务、Flash、SOC 积分都不是中断安全的。）

## 3. 应用契约与唯一授权 — `bms/core/bms_afe.h`、`bms_afe_guard.c`

```c
/* OK 表示本轮完整读取成功；转换完成事件只在后端用于校准及保护恢复。 */
typedef enum { BMS_AFE_SAMPLE_OK, BMS_AFE_SAMPLE_NOT_READY,
               BMS_AFE_SAMPLE_ERROR, BMS_AFE_SAMPLE_READ_ERROR } ...;
```

`READ_ERROR` 只作废本轮测量，`ERROR` 才可能禁止输出；请求（`set_fets`）与执行
（`apply_outputs`）分开，所有输出最终由 guard 授权。

想一下：为什么"意图"和"执行"要分开记录？

## 4. 一次真实采样 — `bms/afe/dvc1124/dvc1124.c`

```c
raw20 = ((uint32_t)data[DVC1124_REG_CC2_H] << 12) |
        ((uint32_t)data[DVC1124_REG_CC2_M] << 4) |
        ((uint32_t)data[DVC1124_REG_CC2_L_FLAGS] >> 4);
cc2 = dvc_sign_extend20(raw20);
current_ma = dvc_cc2_to_raw_current_ma(cc2);
s_snapshot.raw_current_ma = current_ma;
current_ma = bms_config_calibrate_current(current_ma);
```

三条全项目约定：原始值与校准值都保存；正放电负充电；0.1 A 报告字段有独立死区与饱和。

## 5. 软件保护 — `bms/core/bms_sw_protection.c`

三级共用一个循环，只有 Third 阻断 MOS；`bms_sw_filter_update` 内含两种滤波
（触发累积/恢复连续）；状态只有 active + 一个阶段计数。

想一下：电流类保护为什么还要 `recovery_allowed`？（关 MOS 后零电流不是恢复证据。）

## 6. SOC 主流程 — `bms/core/bms_soc.c`

```c
if (!g_soc_input_valid) { ... return; }              /* 首帧不补算 */
if (elapsed_ms == 0u) return;                        /* 重复时间戳 */
if (elapsed_ms > BMS_SOC_MAX_SAMPLE_GAP_MS) { ... }  /* 长间断作废 */
while (g_soc_strategy_pending_ms >= quantum_ms) { ... }
```

`soc_strategy_update` 的优先级链：满充 → 强制空 → 放电末端 → 静置空 → OCV。
满充可向上，OCV 只向下。

## 7. 掉电安全 — `bms/core/storage_record.c`

```c
program_bytes(header, OFF_COMMIT0);
program_bytes(payload, payload_size);
program_bytes(commit, sizeof(commit));   /* 提交标记最后写 */
```

写中断 → 无 commit → 当无效槽忽略，旧记录仍有效；CRC 覆盖元数据 + payload。

想一下：load 为什么要扫全部槽？（写失败会跳槽，最大序号不一定在扇区末尾。）

## 8. 对外协议 — `bms/core/modbus_rtu.c`

窗口优先级：调试日志 → 诊断 → 事件 → AFE 实际/请求 → 通用寄存器。
每个窗口自己负责长度、越界与异常码；地址空间是协议契约。

## 跟读工具

- 断点：`bms_afe_sample()` 返回处、`bms_afe_apply_outputs()` 入口、
  `SOC_Result_Pass()`、`storage_record_save()` 的 `done:` 标签。
- 练习：跑 `tests/sh_recovery_host_check.py` 对照第 4/5 站；把 `main_loop()` 排成
  200 ms / 1 s / 60 s 三条时间轴；从 `bms_product.h` 追到 `sources.txt` 确认产品差异。

## 下一步

- 通信与保护联动：[AFE 通信策略](AFE_COMMUNICATION_POLICY.md) + `bms_afe_guard.c`
- 存储：[存储说明](STORAGE.md) + `storage_record.c`
- SOC：[SOC 说明](SOC.md) + 四个 anchor
- 产品差异：`bms/products/dXXX/AGENTS.md`
