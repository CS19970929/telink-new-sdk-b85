# D008 ACC 开关与保电深睡眠

用户最新授权取代旧“不实现ACC逻辑”约束。引脚不变：PA0=ACC-MCU输入，PC4=MCU-LDO输出，PB1仍不是充电器检测。

| 条件/模式 | 行为 | 唤醒 |
|---|---|---|
| ACC低 | 保持正常采样/保护/通信；原轻量suspend可用 | 正常运行 |
| ACC高稳定200ms | 保存SOC/事件→AFE shutdown成功→PC4保持高→MCU DEEPSLEEP_MODE | 仅PAD PA0低电平，完整启动 |
| deepsleep_en显式指令 | 保留既有AFE shutdown→PC4低断电，优先于ACC | 外部恢复电源 |
| 自动低压断电 | 保留既有阈值/持续时间和PC4断电路径 | 外部恢复电源 |

ACC高期间不再累计自动低压断电时间。低电平恢复不绕过保护或升级门禁。BLE连接先发送terminate并等待断开，待发送回应先排空；OTA、Flash保护未恢复、总线不在OWC_IDLE时延后，不强制中断事务。连续串口流量可能延长等待。200ms为开关去抖，不是保护阈值。

保存/AFE shutdown失败时保持正常主循环与供电，每5秒有界重试。高电平取消则清除ACC等待状态；已经请求的BLE断开不可撤销，可重新连接。shutdown成功后禁止继续主循环的I2C/Flash/采样，仅进行PAD深睡眠。若PA0在进入前后变低导致SDK立即返回，则start_reboot走完整启动，不使用已shutdown的AFE继续运行。

使用现有B85 SDK `cpu_set_gpio_wakeup(ACC_MCU_PIN, Level_Low, 1)` 与 `cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0)`；不用SRAM retention或定时唤醒。main.c在完整启动时设置PC4高，并经现有AFE接口使能→I2C唤醒→reset/init/readback→三次采样资格恢复。未知睡眠时长不补SOC积分/静置时间。配置与Flash格式、阈值/revision均不变；不新增ISR存储操作。

Host tests执行实际app.c PM函数，覆盖高电平去抖/取消/回绕、PC4保持、低电平PAD、OTA/Flash/总线/BLE待发与断连、保存/事件/AFE失败、5秒重试、shutdown/入睡瞬间低电平重启、显式断电指令优先和原PM/SOC回归。SDK和GPIO由mock替代，不能代替真实唤醒、PC4保持及电流测量。

TODO_VERIFY_HW：PA0实际电平/抖动、深睡眠PC4和3V3保持、PAD低电平最小保持时间、唤醒后AFE初始化/物理MOS、睡眠电流、watchdog及外部反向供电。未进行烧录或设备故障注入。

## 本轮验证及交付

2026-09-17，源码基线a89bd1f8，原refactor/d008-common-bms-features分支。未创建开发分支，保留用户脏文件，未推送或烧录。

- source-order 94 objects；15项Host/contract、22项tooling unittest、当前16S参数校验/启动测试通过。
- 默认SW/HW=1/1及1/0、0/1、0/0四种TC32 clean build/check-fw/MAP/manifest/verify通过；当前16S SW0/HW1另行通过。
- cppcheck：31应用单元，覆盖缺口0；0 error/warning，94 style，MISRA未执行。
- 默认1/1 text=102280、data=4000、bss=7156 bytes；比上一版增加Flash464、静态RAM8bytes，MAP约束通过；最坏运行栈尚未实测。
- 正式交付：outputs/d008-acc-sleep-20260917/D008_16S_SW0_HW1_ACC_SLEEP.bin；保留当前16S SW0/HW1、已确认欠压参数和已有LED设置，为台架配置。Build ID为a89bd1f8，精确工作区差异和manifest在evidence。

无需更新Windows上位机。先确认ACC输入能够可靠拉低再升级测试；上电时ACC持续高也会在初始化后进入此休眠。验证高/低电平循环、BLE连接及OTA期间的延后，再用示波器和电流表确认PC4保持及真实深睡眠电流。实板未决项保持TODO_VERIFY_HW。
