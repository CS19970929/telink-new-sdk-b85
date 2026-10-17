# SOC、D008 均衡与 State 时钟四项优化

基线：`d7deee936be4f1171fb30cc55701baf2d6f0fdb6`，分支
`codex-bms-deep-simplification-20261016`。用户授权按四项方案实施、分别中文提交，
执行相关 host 回归及四产品 sources/link/resources，不生成 BIN。

## 兼容边界

协议地址、帧格式、CRC、缩放、无效串位 61001、CFG2、schema 3、Flash/OTA/APP
布局不变。SOC 五种策略及其优先级、满空锁存、物理保护恢复、watchdog bus silence、
三次有效样本、MOS/均衡互锁、普通 Suspend 和强制深睡门禁保持。

## 第一步：SOC 诊断编译裁剪

ETA、OCV 展示字段和端点事件标志只在现有
`BMS_SOC_DIAG_STATE_ENABLE = BMS_SERVICE_DIAG_ENABLE || BMS_SOC_BOARD_TEST_ENABLE`
下保存和计算。业务仍使用 OCV 上界、连续静置计数、端点状态及原资格判断。
诊断关闭时不引用 ETA 函数；服务诊断和独立板测保留原信息和算法。

`tests/d008_power_soc_host_check.py` 增加三种宏组合的共同业务轨迹，关闭诊断时
不链接 ETA 实现以检查残留引用。场景覆盖 LFP/NMC、OCV、满充显示、UV 后充电、
静置空电、方向变化、无效/重复/长间隔样本、tick 回绕、重启和独立板测结束恢复。
外围参数/Flash/硬件是显式替身，生产 SOC 函数体完整编译；不代表实板 SOC 精度。

第一步相关 host：四产品原长时 SOC 场景通过，七天 CSV 与各自基线逐字节一致。
每产品三种宏组合的共同业务输出均为 91201 行（含表头），逐字节一致；D008 还与
修改前保存的业务输出一致。严格编译发现关闭诊断后的局部变量无消费者，已同步裁剪；
未降低 `-Werror`。成功日志在树外 `step1-ok/`，初次失败日志保留在 `step1/`。

## 第二步：显示 SOC 单一所有者

第二步将显示 SOC 唯一保存在 `g_bms_report.soc.soc_percent`，删除私有镜像和周期复制。
`set_dispsoc()` 初始化/设置，`soc_display_follow_real()` 按原节拍修改；诊断、满充显示
资格和板测保存均读同一字段。生产没有其它字段写入者或运行中清零报告的入口；
启动在开放通信前完成 `soc_param_lib_init()`。报告结构、协议字段宽度不变。

第二步四产品相关 SOC host 再次通过，七天 CSV、三种诊断宏共同业务轨迹及 D008
修改前业务轨迹逐字节一致，包含满充显示到 100% 和板测状态恢复。D014 core contract
通过；终态再按四产品执行相关 contract。成功日志在树外 `step1-step2/`。

## 第三步：D008 均衡同一次回读确认和发布

`dvc_write_balance_hw()` 使用一组独立三字节读缓冲完成写后比较和实际报告更新，
不再先通用 verified-block 再刷新。比较原始三字节，范围外位也须为请求值；报告
仍屏蔽无效串。配置寄存器的通用 verified-block 不变。保留原有界次数和 1 ms
失败退避，只有成功确认才更新续期时间；失败/不一致不发布请求值。

原厂依据：DVC1124-2 RM V1.2 原 PDF 第 21 页，0x67～0x69 为 R/W 均衡控制，
60 s 自动复位；本轮不改寄存器值、续期阈值或策略。完整器件后缀及 20S 装配仍待实板确认。

寄存器 peer 执行真实 refresh/write/set/get 函数体，验证正常变更/续期
3 读＋1 写减为 2 读＋1 写；已 OFF 保持 1 读。新增计数断言在修改前失败（exit 3），
修改后通过。读失败禁止凭缓存开启；仍尽力 OFF；有界写/读重试、缺位、OFF 反弹、
范围外残留、失败不续期、实际报告及 tick 回绕均通过。计数不是物理 I2C 波形或最坏时间。
前后故障注入日志分别在树外 `step3-red.log`、`step3-green.log`。
另通过 D008 寄存器场景、固定策略及四产品 feature 回归，日志在 `step3-related/`。

## 验证与证据约定

基线已在干净提交上完成四产品 development/production ELF/MAP/resources，
D008 使用源码 custom 20S LFP；另保存 D008/D014 服务诊断开发配置。
四产品基线七天 SOC CSV 及 D008 共同业务轨迹已保存。后续结果按实际执行追加，
不把基线或上一轮报告写成本轮终态通过。

对象、CSV、日志和机器报告均在源码树外
`%LOCALAPPDATA%/CodexTemp/telink-refactor/20261017-four-steps/`。
本轮不执行镜像、烧录、OTA 或硬件操作；host/ELF/MAP 不替代均衡电流/关闭、
Gate/WDT、Flash 掉电、低功耗及唤醒验收。
