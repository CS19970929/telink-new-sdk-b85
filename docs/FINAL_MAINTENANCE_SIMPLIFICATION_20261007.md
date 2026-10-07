# 最后一轮启动与电源职责简化

实施基线为 `cf04da12`，需求来自“完整审核与简化建议”最新对话。按当前[本机验证约定](../AGENTS.md#本机验证约定)，本轮只做源码与差异审查；未运行编译、host、ASan/UBSan、静态分析、资源检查或远端 CI。历史测试结果不代表本轮修改已通过。

## 启动与 CI 选择

`bms_parameters_init()` 仍独立尝试 Config、State、Event，再检查整体启动资格。`parameters_load_protection()` 仅从 Config 缓存读取软件保护；缓存不可用时加载 RAM 默认、报告错误并保持无效，不再次初始化 Config，也不在此写回默认。有效 State 的 SOC/放电累计/循环缓存仍可加载；在线保护提交仍不能清除启动失败门禁。

同步启动夹具和真实 journal/语义存储夹具：Config 硬失败时只尝试一次，首个失败诊断保留，State/Event 仍尝试。夹具已更新但未执行。

CI sanitizer 命令增加 `--only core_contract_check`，其产品归属由 `tests/validation_catalog.py` 定义为四产品。现有选择预期由 37 组增加到 41 组；这只是执行范围修改，实际执行数、结果和耗时需以后运行 CI 确认。上一轮证据中的本机 WSL 41 组与旧远端 workflow 的 37 组分别理解，不修改固定提交历史结果。

## D008 板级名称与固定输入

直接替换八个旧引脚名，不保留 alias；原 GPIO 编号和所有读写电平保持：

| 旧名称 | 当前名称 | GPIO |
|---|---|---|
| `RF_EN_PIN` | `BMS_BOARD_HEATER_FUSE_PIN` | PD4 |
| `AFE1_PRO_EN_PIN` | `BMS_BOARD_AFE_ENABLE_PIN` | PD7 |
| `ACC_MCU_PIN` | `BMS_BOARD_ACC_PIN` | PA0 |
| `HEATER_EN_PIN` | `BMS_BOARD_HEATER_ENABLE_PIN` | PA1 |
| `CHG_IN_PIN` | `BMS_BOARD_LOAD_DETECT_PIN` | PB1 |
| `OWC_TX_PIN` / `OWC_RX_PIN` | `BMS_BOARD_OWC_TX_PIN` / `BMS_BOARD_OWC_RX_PIN` | PC2 / PC3 |
| `MCU_LDO_PIN` | `BMS_BOARD_MCU_LDO_PIN` | PC4 |

依据为 [D008 reference](D008_PRODUCT_REFERENCE.md) 第 1.2/3 节和 [HS-D008-24S100A-V1 原图](../references/hardware/d008/HS-D008-24S100A-V1.pdf)第 1 页。PD4 名称表达现有熔断请求路径，不代表 F1 支路已完成实装验收；PB1 保留负载移除/保护恢复的既有用途，不作为自动加热的充电资格。没有修改 LED 引脚、D011 PB5 安全电平或其他产品能力。

删除恒真的 `DVC1124_D008_PROJECT` 与身份宏的 `#undef`；串数仍由 `D008_PRODUCT_PROFILE` 选择。`dvc1124_product_defaults.h` 收至 185 行，固定接线、采样/供电、watchdog、body diode、I2C timeout/reset 和零点数值直接定义。保留软件/硬件/温度保护及启动零点四个台架开关的 `#ifndef`，保留 production 和数值范围门禁。普通配置的值与表达式未改；固定输入不再提供命令行覆盖入口。

同步现有消费者、夹具和当前产品说明；固定提交审查报告保持历史原样。未运行这些夹具，也未执行预处理或目标构建，因此不宣称已证明编译等价或资源余量。

## 应用与电源状态所有权

`app.c` 收至 413 行，继续负责启动、GPIO 初始化、采样到期标志、AFE/SOC/MOS、通信和 State checkpoint。原电源计时、重试、ACC/关机提交状态及 DVC/SH PM 分支移入 `app_power.c`；四产品 `sources.txt` 在 `app.c` 后加入此文件，其余源码顺序保留。

公共入口只有 `app_power_prepare_loop()`、`app_power_process()` 及原 SDK 回调 `task_sleep_enter()`：

```text
main_loop
  app_power_prepare_loop
    D008 已提交 ACC 休眠：保持动作并返回，不进入后续任务
    D008 参数/存储/AFE 诊断轮询
    D008 已提交关机：定时 suspend 并返回，不进入后续任务
    其他情况：继续
  BLE → 到期采样 → 事件 → 通信 → State checkpoint
  app_power_process：评估原 DVC/SH 电源策略
```

没有新初始化 wrapper。`board_init()` 和 `app_init()` 的 GPIO 唤醒配置保留原位置；SDK 回调名、签名、注册点与行为保留。200 ms 采样/关机保持间隔共用 `app.h` 的 `APP_SAMPLE_PERIOD_US`，删除无消费者的重复 ADV/CONN idle 宏。

`s_sample_due` 仍由 `app.c` 的唤醒回调置位、采样任务消费。唯一生产调用传入非空 `&s_sample_due`；PM 不保存指针、不修改标志，每次读取保留 `volatile` 语义，避免改成传值后隐藏调用期间的新到期事件。`deepsleep_en` 由电源模块定义、Modbus 按原地址锁存，保持 SDK `bool` 类型。DVC 的短测量新鲜度 helper 在两文件各自保持私有，用同一阈值与时间差表达式，没有新增公共安全 API。

源码对照保留 ACC 去抖与断连、显式命令应答排空、State/事件/AFE shutdown/PC4 的顺序，以及 OTA、Flash、总线、UART、PAD、样本、电流和退避门禁。SH 默认固定 UART 阻断仍存在，保存失败策略和各后端 AFE 事务不变。SOC、保护算法、guard、journal 编码及外部地址均未在此拆分中修改。

现有 scheduler、D008 PM、SH sleep、产品合同及检查目录已适配新入口/文件；scheduler 夹具仍覆盖正常顺序、ACC 保持和已关机保持，断言预期保留。仅审阅源码和 Git 差异，未执行夹具、编译、预处理、`sources --check`、静态分析或资源检查。跨文件拆分后的链接与 Flash/RAM 余量尚未重新验证。

本轮没有镜像、烧录、实板、Flash 掉电、Sleep/Wake 或 OTA 验收。SOC 诊断类型的归属调整是原方案的可选后续项，本轮未纳入。
