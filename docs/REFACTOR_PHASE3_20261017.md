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

## 验证与证据约定

基线已在干净提交上完成四产品 development/production ELF/MAP/resources，
D008 使用源码 custom 20S LFP；另保存 D008/D014 服务诊断开发配置。
四产品基线七天 SOC CSV 及 D008 共同业务轨迹已保存。后续结果按实际执行追加，
不把基线或上一轮报告写成本轮终态通过。

对象、CSV、日志和机器报告均在源码树外
`%LOCALAPPDATA%/CodexTemp/telink-refactor/20261017-four-steps/`。
本轮不执行镜像、烧录、OTA 或硬件操作；host/ELF/MAP 不替代均衡电流/关闭、
Gate/WDT、Flash 掉电、低功耗及唤醒验收。
