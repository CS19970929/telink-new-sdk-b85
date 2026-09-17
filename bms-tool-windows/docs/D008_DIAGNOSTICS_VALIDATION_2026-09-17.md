# D008 诊断交付验证记录 — 2026-09-17

## 源码与交付

- 固件功能提交：`eb058d98262881d9b02ac17e751e6ef14e7c038a`，直接位于 `refactor/d008-common-bms-features`，没有新建 BMS 开发分支。
- Windows 功能提交：`d2e7570`，补充模式解码/真实超时测试：`56b7241`，位于 `feature/windows-afe-hw-protection-editor-v2`。
- 本次仅创建本地提交，未 push、未烧录、未 OTA。
- 正式交付目录：固件仓库 `outputs/d008-diagnostics-eb058d98/`，包含两版自包含 EXE、默认/隔离组合/当前工作区 BIN、ELF、MAP、manifest、完整证据与 SHA-256 `delivery.json`。
- 用户原有 `conf.h`、`param.h`、`d008_product_profile.h`、`dvc1124_project_config.h` 修改未纳入提交。工作区保留 16S/SW=0 及用户参数；另有对应台架 BIN，明确标注 git dirty、Build ID=0。默认交付从干净提交的临时 detached 快照构建，24S LFP / SW=1 / HW=1。

## 已执行结果

| 检查 | 结果 |
|---|---|
| 固件 source-order | 94 条固定链接输入，通过 |
| Host/contract | 14 个脚本均通过，包括真实 Modbus 入口、storage、guard、SOC、PM 与原有保护/配置契约 |
| 保护组合 | 1/1、1/0、0/1、0/0 均 clean build、check-fw、MAP、manifest、verify 通过 |
| 当前工作区台架配置 | 独立输出 rebuild/check-fw/MAP/manifest/verify 通过，没有修改台架宏 |
| 固件 verify | 默认 BIN 长度、SHA-256、Telink CRC、94 个对象、source-order、链接脚本、Vendor libs 全部一致 |
| Windows | 两版 .NET 8 / win-x64 自包含发布成功；编译 0 warning / 0 error |
| Windows diagnostics | 实际 BmsClient + 分片模拟通道；解码、旧固件、异常码、真实 4s timeout、取消、Trace 有界重试、部分 ZIP、只读帧采集通过 |
| Windows 原事件协议 | C008/100 回归通过；既有 net7 测试脚本出现 NETSDK1138 提示，交付应用使用 net8 |
| WPF 离屏检查 | 两版实际程序集构造、诊断入口、四个只读表格及模拟数据渲染通过；没有连接实板 |
| cppcheck 2.21.0 | 31 个应用 C 单元，覆盖缺口 0；0 error、0 warning，94 style。按 file/id/message 对比基线，新增 0、移除 0；不是 MISRA 合规声明 |

关键 Host 用例：Config 失败两次、启动短路后 Event 独立尝试、空白默认不计硬件故障、首次失败冻结不被覆盖、保存失败门禁、OTA/锁/退避原因、Requested ON/Driver OFF、多重阻断、无效反馈、跨区写整帧拒绝、CRC、广播、地址回绕、读取无 I/O、Trace 覆盖及 tick 回绕。

## 资源与栈边界

对比干净基线 `c8dc730`，默认 1/1：

| 项 | 基线 | 诊断版本 | 增量 |
|---|---:|---:|---:|
| text | 98,056 B | 100,984 B | +2,928 B |
| data | 3,996 B | 3,996 B | 0 B |
| bss | 4,992 B | 7,056 B | +2,064 B |

默认 BIN 为 105,140 B，SHA-256：`cf9cea3b551ddb01db0c5cb8f945a94430075cedf8a7a0b15eb241df9e69c7c4`。

MAP `_ram_use_end_=0x846010`，到 SDK TLSR8251 SRAM 末 `0x848000` 静态地址差为 8,176 B。**这不是已测栈高水位**。构建工具原有目标配置风险仍存在：声明 TLSR8251，但继承 `MCU_STARTUP_8258`/`STARTUP_SRAM_END=0x850000`；本次不修改启动/链接器，须核对实际芯片与原批准基线后单独处理。诊断读取没有新增大栈快照，仅直接编码主循环 RAM；实测最坏调用栈仍待完成。

## 构建证据与复现

临时源码/对象/缓存均位于用户 `LOCALAPPDATA/CodexTemp/d008-diagnostics`。`evidence/run_build.py` 仅为既有 `bms.py` 适配外部构建输出路径、manifest 路径表示和独立 junction；不改变固件源码/编译选项/完整性检查。正式组合通过 `EXTRA_DEFINES` 注入 `BMS_DIAG_BUILD_ID=0xeb058d98u` 及两个保护开关。默认/各台架目录包含独立 manifest；原 manifest 保留当时对象路径，脱离验证环境时以 `delivery.json` 核对 BIN。

所有诊断功能使用原协议以外的新增只读窗口；旧寄存器、Flash schema/地址、硬件固定策略和保护结果保持不变。软件读取诊断不能恢复 MOS，也不能把 AFE flag 解释为物理导通。

## TODO_VERIFY_HW

- 实板从开机开始采集，确认 boot address/layout 与两次存储错误实际因果；目前根因未确认。
- BLE/串口长帧及 Trace 读取对 200 ms 采样最坏延迟、实际栈高水位和功耗。
- Gate/Vgs/电流等物理输出、Flash 真实失败/供电时序。当前 Physical Feedback=unavailable。

操作：连接后进入“BMS 诊断”→“读取完整诊断”→“导出诊断 ZIP”；无需打开 Factory Session 或解除保护门禁。

工作区台架 BIN 构建完成于 2026-09-17 11:08:26 +08:00，包含当时的 16S/SW=0 及配置；不包含 11:09:29 后新增的 app.c LED 初始化/周期翻转。此后用户修改继续保留在工作区，未纳入诊断提交。
