# STM32 BMS 低功耗策略检查清单

本文件用于输出 `power_mgr` 设计、休眠前检查、唤醒后恢复顺序和常见风险点。

## 建议模块

```text
power_mgr/
  power_mgr.h
  power_mgr.c
  wakeup_mgr.h
  wakeup_mgr.c
```

## 关键接口模板

```c
typedef enum {
    PWR_MODE_RUN = 0,
    PWR_MODE_SLEEP,
    PWR_MODE_STOP,
    PWR_MODE_STANDBY,
} pwr_mode_t;

typedef enum {
    WAKEUP_SRC_NONE = 0,
    WAKEUP_SRC_RTC,
    WAKEUP_SRC_EXTI,
    WAKEUP_SRC_COMM,
    WAKEUP_SRC_FAULT,
} wakeup_src_t;

int power_mgr_init(void);
int power_mgr_request_sleep(pwr_mode_t mode);
int power_mgr_prepare_sleep(void);
int power_mgr_enter_sleep(void);
int power_mgr_resume_from_wakeup(void);
wakeup_src_t power_mgr_get_wakeup_source(void);
```

## 休眠申请拒绝条件

以下任一成立时，默认拒绝进入深度低功耗：

- 存在未完成 `Flash` 擦写
- 存在活跃 `BLE` 连接或关键业务通信窗口
- 当前处于故障处理关键阶段
- 采样前端仍在稳定等待时间内
- 充放电控制动作正在切换
- 唤醒源未配置完成

## 进入低功耗前动作

```text
1. 锁定状态机切换
2. 停止高频任务和采样触发
3. 保存关键上下文
4. 关闭可关断外设和前端使能
5. 配置 RTC / EXTI / 通信唤醒源
6. 清 pending flag
7. 喂狗
8. 进入目标低功耗模式
```

## 唤醒恢复动作

```text
1. 获取唤醒源
2. 恢复系统时钟和 SysTick
3. 恢复 GPIO 默认输出态
4. 恢复 ADC / DMA / TIM / UART / CAN / I2C / SPI
5. 等待 AFE 或模拟前端稳定
6. 执行一次快速采样自检
7. 重新开放周期任务
8. 解除状态机锁定
```

## 常见遗漏点

- `Stop` 唤醒后未重新配置系统时钟
- `ADC` 校准值、采样通道或 `DMA` 指针未恢复
- `RTC/EXTI` 中断标志未清，导致重复唤醒
- `BLE stack` 早于时钟恢复启动
- 唤醒后立即做高风险 `Flash` 写入

## 功耗设计建议

- 先按 `Stop` 模式闭环，再决定是否下探到 `Standby`
- 把“周期唤醒采样”和“事件唤醒处理”拆开
- 用统一接口统计最近一次休眠时长、唤醒源、恢复耗时
