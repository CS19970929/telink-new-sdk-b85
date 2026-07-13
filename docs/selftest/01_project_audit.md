# 工程审计记录

审计日期：2026-07-13；目标分支：`renzheng-new-new-new`；基线提交：`740a3cc`。

## 真实目标与启动链

- 构建目标 `project/tlsr_tc32/B85/825x_ble_sample` 定义 `__PROJECT_8258_BLE_SAMPLE__=1` 和 `CHIP_TYPE_825x`，对应 TLSR8258/B85。
- `boot/B85/cstartup_825x.S` 建立 IRQ/SVC 栈、清 BSS、复制 DATA；第 330 行新增早期自检调用。
- `project/tlsr_tc32/B85/boot.link` 显式保留 `.bms_manifest`（第 74 行）和 `.bms_stack_guard`（第 121 行），保留原 600 字节栈余量断言。
- `vendor/ble_sample/main.c` 第 74 行在时钟初始化后进入板级自检，第 95 行运行分时自检。
- `vendor/ble_sample/app.c` 在 SH367309 首帧有效后执行启动测试；所有 CHG/DSG/CTLC/RF_EN 开启路径增加 `BMS_FailSafe_AllowOutputs()` 门控。

## 时钟、RAM、低功耗与看门狗

- 系统时钟为 16 MHz；工程选择内部 32 kHz RC。系统 tick 和 Timer0 都依赖 MCU 时钟树，不能视为独立参考。
- 启动文件定义 64 KiB SRAM，地址 `0x840000`～`0x84FFFF`。
- 当前 `PM_DEEPSLEEP_RETENTION_ENABLE=0`，BLE 低功耗打开但不使用 deep-retention。
- 原工程看门狗周期 2 s，原来每轮无条件清狗；现改为 AFE、保护、MOS、自检四类心跳全部到达后才清狗，致命 MCU 故障停止喂狗。

## BMS 安全输出与 AFE

- AFE 为 SH367309；`MTP_CONF` 的 CHGMOS/DSGMOS/PCHMOS 位控制充/放/预充 MOS，`0x41/0x42` 为均衡寄存器。
- 已由原关闭函数确认 PA1/CTLC 低、PB6/AFE_CTL 低及 MOS 位清零为关闭路径；PD4/RF_EN 高为危险驱动，安全态拉低。
- PD7/AFE1_PRO_EN 的真实安全极性无法从软件唯一确认，未在早期阶段盲目翻转，列为硬件待确认项。
- `SH367309_VerifyAfeConfig()` 回读并比较 25 字节配置镜像；AFE 通信、配置和 MOS 反馈被纳入应用诊断。

## Flash 与 OTA

- 应用槽从 `0x00000` 开始，原配置最大固件 124 KiB；OTA 第二槽从 `0x20000` 开始。
- 512 KiB 布局中事件日志从 `0x40000`、蓝牙名称从 `0x50000`、运行参数从 `0x51000`、运行 KV 从 `0x53000`、软保护参数从 `0x5B000` 开始；MAC/校准/SMP 区受 SDK 宏和实际容量影响。
- 本次最终 Manifest ELF 地址 `0x1764C`，最终 BIN 96,580 字节，未越过 `0x1F000` 应用上限。
- 实际板卡是 512 KiB、1 MiB 还是 2 MiB 仍须通过料号、原理图或运行时 MID 读数确认；软件不得据文件名推定硬件容量。

## 审计边界

参考 PDF、Word、Excel 已用于建立结构和术语，但未复制其“通过”结论。未取得 MCU 完整安全手册、SH367309 硬件诊断覆盖率、原理图、量产烧录规范和独立认证机构意见，因此本文只证明代码/构建事实，不证明标准合格。
