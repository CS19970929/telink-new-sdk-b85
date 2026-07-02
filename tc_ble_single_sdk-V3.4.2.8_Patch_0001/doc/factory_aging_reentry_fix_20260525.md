# 老化模式重新进入修复说明

## 问题现象

`FW_UPGRADE_RESET_RUNTIME_EPOCH` 修改后，设备仍可能表现为不能重新进入老化计时。判断重点不是 MOS 当前开关状态，而是 `Runtime_Init()` 最终是否让 Runtime 留在 `MODE_FACTORY`。

充电唤醒或充电器接入时，业务上应优先进入充电控制，即执行 `open_chg_close_dsg()`。这不应被当作“没有进入老化计时”的根因；老化计时应由 Runtime 状态决定。

## 根因

`Runtime_Init()` 原来在 runtime Flash 布局不可用时会执行：

```c
g_runtime_min = FACTORY_TIME_LIMIT_MIN;
g_mode = MODE_NORMAL;
```

这会把 3 天老化时间直接判满。只要 `runtime_flash_base()` 返回 `0`，设备启动时就会跳过 `MODE_FACTORY`，表现为不能重新进入老化。

runtime Flash 布局不可用的常见原因包括 OTA 多地址布局与当前保留区冲突，例如 512K Flash 下当前 multi boot 地址不是 `MULTI_BOOT_ADDR_0x20000`。

## 修复策略

- `runtime_flash_base() == 0u` 时不再把 `g_runtime_min` 置为 `FACTORY_TIME_LIMIT_MIN`，避免把老化时间直接判满。
- runtime 存储未就绪时不做周期性落盘，避免每分钟触发一次存储错误。
- 保留 `Runtime_ReenterFactoryMode()`，用于上位机写 `0x1102 = 0x0003` 时先清 runtime，再重新进入老化计时。
- 启动和 200ms 充电检测仍保持原有优先级：检测到充电时进入充电控制，不强行切到老化 MOS。
- 老化模式在无充电时必须尊重物理开关：开关闭合才允许 `enter_fac_mode(true)` 打开双 MOS；开关断开时执行 `close_dsg()`，并允许 `_DI_SWITCH_SYS_ONOFF` 的无充电/开关断开休眠计时生效。充电接入时忽略开关，优先执行 `open_chg_close_dsg()`。

## 验证重点

- 新板或被 runtime reset 的板，启动后 Runtime 应处于 `MODE_FACTORY`，老化时间从 0 开始累计。
- 老化完成只能由真实运行时间累计到 `FACTORY_TIME_LIMIT_MIN` 后触发，不能由初始化直接置满。
- 充电器接入时允许执行 `open_chg_close_dsg()`，但 Runtime 仍应按实际运行时间累计。
- 老化模式下断开开关后，DSG 应关闭，且无充电条件下应按既有 3s 逻辑进入 deep sleep。
- 写 `0x1102 = 0x0003` 后，应清 runtime 并重新进入 `MODE_FACTORY`。
