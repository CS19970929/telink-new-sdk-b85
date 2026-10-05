# D014 保护与恢复的软件闭环

## 已复现的问题与修复

基线 `464b6ee53c94d9bed2d3d994fc93c5302c937fac` 上，软件 Third OCD 触发并关闭 DSG 后，只要测得电流降到 Recover 以下，即使负载仍连接，也会按 Filter 自动恢复。新的联合场景先得到 `continuous_load_zero_current expected=DSG_OFF actual=DSG_ON`，再修改生产逻辑。

SH 后端现在向公共软件保护提供明确的恢复输入：是否要求物理证据、充/放电侧解除条件、本次是否有完整新 ADC 样本。软件保护继续独占 Third 状态与滤波计数；SH 后端独占寄存器检测模式、物理证据及硬件故障。没有新增第二份软件过流锁存。

- 软件 OCD 也会选择负载检测。稳定的 `LOADOFF && !LOADON` 或既有 `CHGING` 反向状态，才允许软件放电过流按电流回差恢复。
- 软件 OCC 使用本次有效 C+ 拔除证据或既有 `DSGING` 反向状态。C+ 与负载检测的互斥、切换等待及 900/2100 mV 判据沿用 SH 硬件恢复策略；这些板级判据仍需实测签核。
- 新鲜、完整的电流/电压样本才能推进恢复计数。正常 CADC 等待只暂停；解除条件消失会清零。通信错误与重新建立采样窗口也清零部分恢复计数。
- AFE 初始化、feature 初始化、通信恢复、Sleep/Wake 保留已经触发的软件 Third OC。其他故障仍按各自所有者阻断，软件 OC 消失不等于允许最终 MOS 导通。
- First/Second 告警行为不变；关闭某软件保护项仍按现有参数语义清除该项。D008 尚未提供这组物理输入，保持原行为，不能据此声称 DVC 软件过流恢复已闭环。

`bms_sw_protection_init()` 在启用物理恢复输入后保留两个 OC active 位，但重新建立其恢复窗口；`bms_sw_protection_clear()` 仍是显式清除全部软件状态的入口。所有状态留在主循环，无 ISR、Flash schema、协议或产品默认参数变更。

## 可重复执行的测试

```powershell
$env:CC = 'C:/qp/qtools/MinGW32/bin/cc.exe'
$env:PYTHONDONTWRITEBYTECODE = '1'
python tests/run_host_regression.py --product d014 --only d014_safety_loop_host_check
```

`tests/d014_safety_loop_host_check.py` 直接编译产品 sources.txt 中的 22 个原始生产 TU，不抽取或重写函数体：参数/Config、State、Event、journal、CRC、SOC 默认与校验、软件保护、feature、guard、板级能力、SH control/backend/feature/NTC/SPI 驱动等。公共回归入口已登记；CI 的普通回归与插桩场景均覆盖本测试。

夹具仅实现 Flash 端口、逐字节 SPI 对端、GPIO 和时钟；SDK host 头文件替换平台声明，产品配置仍为真实 D014。Modbus 完整帧回调未进入本场景，只有一处调用即终止的链接占位，不返回伪成功。SOC TU 的链接只为执行真实默认/校验，不能由此推断覆盖 SOC 整个算法。

| 场景 | 观察与约束 |
|---|---|
| 空白 Flash、启动保存失败 | 真实启动门禁阻断请求；未签核默认不会被测试声称为合适的电池参数 |
| 参数提交、非法回差、部分写入失败 | 校验拒绝与持久提交失败均不发布候选；保留旧参数 |
| 持续负载/充电器与零电流 | 软件 OCD/OCC 保持阻断；迟滞区不能作为 C+ 移除 |
| 新采样与重新连接 | 未完成 CADC 不推进恢复；重新连接丢弃原计数 |
| AFE 重初始化与 Sleep/Wake | 重新满足启动等待和采样资格，原 OC 状态仍在 |
| 温度断线、硬件短路并发 | 软件 OC 恢复后，其他保护仍独立关断 |
| 保护寄存器读回错误 | 进入 guard inhibit，重初始化不能清软件过流 |
| 总线失联、watchdog 静默 | 静默期间无 SPI；到期重初始化后仍保留 OC |
| ADC 过期、tick 回绕 | 过期后输出阻断，回绕后重新建立资格 |
| 新进程重启 | 真实 journal 重新加载之前提交的参数；不沿用测试进程中的缓存 |

模拟输入使用 8 路 3300 mV、10 kΩ NTC、受控电流，测试用过流阈值经真实参数提交入口写入模拟 Flash。它们不是修改后的产品出厂值。

## 证据不能跨越的边界

这是 D014 的保护采样任务闭环，尚未纳入 `app_sample_task()` 的真实主循环调度、ISR、BLE/Modbus 入站和真实 MCU ABI。SPI 模型不实现完整硅片：ADC 标志由场景注入，open-wire 完成被脚本化为完整线束，MOS 检查针对实际写出的 SCONF2 命令。模型不证明 Gate/Vgs、负载断开、AFE 比较器、转换时延、通信失效时硬件自主关断或真实掉电行为。

**MCU 复位会清除 RAM 中的软件 OC 状态**，与 AFE 通信重初始化不同。本测试明确验证该边界；未增添跨 MCU 复位的故障持久化，也未决定掉电重启后的锁存/人工恢复策略。该策略与实际 C+/LOAD 判据列为产品/台架待签核项，不能把 host 通过当作解除发布阻断。

固定提交的软件、生产 ELF/MAP/资源及插桩结果在验证完成后记录于本页；历史自动化基线见 [自动化验证](AUTOMATED_VALIDATION.md)。实板未完成项继续见 [硬件验证](HARDWARE_VALIDATION.md)。
