---
name: stm32-bms-lowpower-strategy
description: 用于 STM32 BMS 项目的驱动分层、常用采样与控制驱动、BMS 状态机与保护逻辑、低功耗进入与唤醒策略设计、代码审查与重构。适合需要生成模块边界、接口定义、状态机、保护表、低功耗流程和测试清单的任务。
---

# STM32 BMS 驱动与低功耗策略

## 何时使用

当用户要做以下任一工作时使用本 skill：

- 设计或重构 `STM32 + BMS` 软件架构
- 拆分 `BSP / drivers / devices / bms_core / power_mgr / app` 模块边界
- 生成或审查 `ADC / DMA / GPIO / TIM / I2C / SPI / UART / CAN / RTC / IWDG / EXTI` 驱动接口
- 设计 BMS 常用逻辑，如采样、滤波、故障判定、均衡、充放电控制、休眠唤醒
- 设计低功耗模式切换、唤醒源、时钟恢复、外设恢复顺序
- 生成测试清单、异常处理策略、调试插桩点

如果用户只是在问某个寄存器或单个外设初始化细节，不必强行套用整套架构，只抽取相关部分。

## 目标输出

默认输出应尽量覆盖以下内容：

- 软件分层与模块边界
- 核心数据结构与接口定义
- 驱动初始化与回调约定
- BMS 状态机与保护策略
- 低功耗进入、唤醒、恢复流程
- 异常处理与测试策略

除非用户明确要求直接写代码，否则优先先给结构、接口和策略，再落到代码模板。

## 按需读取的参考文件

当任务需要更具体模板时，按需读取以下文件，不要一次性全部展开：

- 驱动分层与接口模板：`references/driver_layer_template.md`
- BMS 状态机与任务切片模板：`references/bms_state_machine_template.md`
- 保护表与故障优先级模板：`references/protection_table_template.md`
- 低功耗进入、唤醒与恢复清单：`references/lowpower_strategy_checklist.md`
- 验证与回归测试清单：`references/test_checklist.md`

如果用户明确要求“直接给代码骨架”或“按模块拆文件”，优先读取 `driver_layer_template.md` 与 `bms_state_machine_template.md`。
如果用户重点在休眠功耗、唤醒异常、恢复顺序，优先读取 `lowpower_strategy_checklist.md`。
如果用户重点在保护逻辑和参数表，优先读取 `protection_table_template.md`。
如果用户要测试方案、产测、自检、回归项，优先读取 `test_checklist.md`。

## 首先收集的输入

优先确认以下信息；若缺失，可明确假设后继续：

- `STM32` 系列与开发库：`SPL / HAL / LL`
- 是否带 `FreeRTOS`
- 电池拓扑：串数、是否多温区、是否带均衡
- 电流采样方案：分流电阻、霍尔、AFE 内置
- 采样链路：`ADC` 直采还是外部 `AFE`
- 通信方式：`BLE / UART / CAN / RS485`
- 低功耗目标：平均电流、休眠周期、唤醒源
- 关键安全要求：故障锁存、上电自检、掉电保存

## 默认分层

除非用户已有强约束，否则使用以下分层：

```text
bsp/
drivers/
devices/
bms_core/
power_mgr/
app/
```

各层职责固定如下：

- `bsp/`：板级时钟、引脚、供电控制、基础硬件映射
- `drivers/`：通用 MCU 外设驱动，不承载业务语义
- `devices/`：具体器件驱动，如 `AFE`、`EEPROM`、温度采样前端
- `bms_core/`：状态机、保护、均衡、数据处理、故障管理
- `power_mgr/`：低功耗条件判断、进入休眠、唤醒恢复、功耗统计
- `app/`：整机策略、对外协议、参数配置、生产模式

禁止让 `app/` 直接操作寄存器或直接控制具体 `GPIO`。

## 驱动层设计规则

### 1. 驱动接口风格

驱动接口优先使用下列模式：

- `xxx_init()`
- `xxx_start() / xxx_stop()`
- `xxx_read() / xxx_write()`
- `xxx_irq_handler()` 或 `xxx_on_irq()`
- `xxx_get_state()`
- `xxx_set_callback()`

避免把业务逻辑写入驱动回调。回调只做事件上报、置位标志、推送消息。

### 2. 常用驱动清单

在 BMS 场景中，优先考虑以下驱动：

- `ADC + DMA`：电压、电流、NTC、母线采样
- `GPIO`：充放电 MOS、采样使能、故障输入、唤醒输入
- `TIM`：周期调度、采样触发、脉冲输出
- `I2C / SPI`：外部 `AFE`、温度芯片、存储器
- `UART / CAN`：诊断口、上位机、VCU、充电机
- `RTC`：周期唤醒、时间戳、休眠唤醒基准
- `EXTI`：按键、插枪、充电检测、故障唤醒
- `IWDG`：系统卡死恢复
- `Flash / EEPROM`：参数、故障日志、校准值

### 3. 驱动设计约束

- 中断上下文中不做阻塞操作
- 中断上下文中不直接改业务状态机
- `ADC/DMA` 数据必须区分原始值、滤波值、工程值
- 所有驱动初始化都必须有返回值或错误码
- 所有外设恢复流程必须可重复执行
- 低功耗前后会丢失配置的外设，必须显式重新初始化

## BMS 核心逻辑

### 默认状态机

除非用户已有定义，默认使用以下状态：

- `INIT`
- `SELF_TEST`
- `IDLE`
- `CHARGE`
- `DISCHARGE`
- `FULL`
- `SLEEP_PREPARE`
- `SLEEP`
- `WAKEUP_RECOVER`
- `FAULT`

### 默认状态切换原则

- `INIT -> SELF_TEST`：基础硬件初始化完成
- `SELF_TEST -> IDLE`：关键器件与参数检查通过
- `IDLE -> CHARGE`：检测到充电器且允许充电
- `IDLE -> DISCHARGE`：检测到负载且允许放电
- `IDLE -> SLEEP_PREPARE`：满足休眠条件
- `SLEEP_PREPARE -> SLEEP`：完成外设关断和唤醒源配置
- `SLEEP -> WAKEUP_RECOVER`：收到 `RTC` 或 `EXTI` 唤醒
- `WAKEUP_RECOVER -> IDLE`：时钟和采样恢复成功
- 任意状态 `-> FAULT`：触发不可忽略故障

### 推荐核心子模块

- `sample_mgr`：采样调度、滤波、工程量转换
- `protect_mgr`：阈值比较、延时、去抖、锁存与恢复
- `balance_mgr`：均衡判定、互斥条件、热限制
- `contactor_mgr` 或 `mos_mgr`：充放电通路控制
- `fault_mgr`：故障记录、上报、恢复策略
- `param_mgr`：阈值、校准、序列号、老化参数

## 保护策略设计

输出保护方案时，默认至少给出下表的字段：

- 保护项名称
- 检测量
- 触发阈值
- 触发延时
- 恢复阈值
- 恢复延时
- 是否锁存
- 影响对象
- 优先级

至少覆盖以下保护项：

- `Cell OV`
- `Cell UV`
- `Pack OC`
- `Short Circuit`
- `Charge OT`
- `Discharge OT`
- `Charge UT`
- `Discharge UT`
- `Sensor Open/Short`
- `AFE Communication Fault`
- `Sampling Abnormal`

若用户没给阈值，不要编造精确数值；应输出“待参数化”的策略表结构。

## 低功耗策略

### 总体原则

低功耗不要散落在各模块中，统一由 `power_mgr` 管理：

- 休眠条件判定
- 休眠申请与拒绝原因
- 外设关断顺序
- 唤醒源配置
- 唤醒后恢复顺序
- 与 BMS 状态机联动

### 休眠进入前检查

进入低功耗前至少确认：

- 无关键故障处理中的任务
- 无进行中的 `Flash` 写入
- 无进行中的 `BLE` 关键通信窗口
- 采样结果稳定，且允许延迟下一次采样
- 充放电通路处于预期状态
- 唤醒源已配置完成

### 推荐的进入顺序

```text
1. 冻结业务状态切换
2. 停止高频采样与非必要定时器
3. 保存关键上下文与故障标志
4. 关闭外设电源或采样使能
5. 配置 RTC / EXTI 唤醒源
6. 清中断标志
7. 切换到 Sleep / Stop / Standby
```

### 推荐的唤醒恢复顺序

```text
1. 识别唤醒源
2. 恢复系统时钟
3. 恢复 GPIO 缺省态
4. 恢复 ADC / DMA / TIM / 通信外设
5. 等待采样前端稳定
6. 执行快速自检
7. 重新打开状态机调度
```

### 低功耗模式选择建议

- `Sleep`：唤醒快，适合短周期待机
- `Stop`：功耗和恢复时间折中，适合多数 BMS 周期唤醒方案
- `Standby`：功耗最低，但上下文丢失更多，只在极低静态功耗目标下优先考虑

如果用户没有给功耗目标，优先建议从 `Stop` 模式开始设计。

## BLE 联动约束

若系统含 `BLE`，默认追加以下检查：

- 休眠前确认广播、连接、参数更新、OTA 等状态
- 唤醒后先恢复时钟与基础驱动，再恢复 `BLE stack`
- 不要让 `BLE callback` 直接控制 `MOS` 或状态机跳转
- 通信活动期与深休眠切换之间要有明确仲裁层

## 异常处理策略

默认输出时至少覆盖以下异常：

- 采样超时
- `DMA` 完成但数据异常
- 外设初始化失败
- 唤醒后外设未恢复
- 参数区损坏
- 看门狗复位
- 突发故障反复抖动

默认建议：

- 故障判定采用“阈值 + 延时 + 去抖”
- 恢复采用独立恢复阈值，避免抖动
- 严重故障进入锁存态，并记录来源与时间戳
- 无法恢复的初始化失败应阻止进入正常充放电

## 输出代码或文档时的模板倾向

### 接口定义

优先输出能长期稳定复用的接口，而不是把寄存器细节暴露给上层：

```c
typedef struct {
    int16_t current_ma;
    uint16_t cell_mv[16];
    int16_t ntc_ddegc[8];
} bms_sample_data_t;

typedef enum {
    BMS_STATE_INIT,
    BMS_STATE_IDLE,
    BMS_STATE_CHARGE,
    BMS_STATE_DISCHARGE,
    BMS_STATE_SLEEP,
    BMS_STATE_FAULT,
} bms_state_t;

int bms_core_init(void);
void bms_core_task_10ms(void);
void bms_core_on_wakeup(void);
```

### 目录模板

当用户要工程骨架时，优先建议：

```text
application/
  app_main.c
  app_comm.c
bsp/
  bsp_clock.c
  bsp_gpio.c
drivers/
  drv_adc.c
  drv_dma.c
  drv_rtc.c
devices/
  dev_afe_xxx.c
  dev_ntc.c
bms_core/
  bms_core.c
  bms_state.c
  bms_protect.c
  bms_balance.c
power_mgr/
  power_mgr.c
  wakeup_mgr.c
```

## 测试清单

默认测试项至少包含：

- 上电初始化路径
- 自检失败路径
- 充电进入与退出
- 放电进入与退出
- 保护触发与恢复
- 周期休眠与唤醒
- 唤醒后首次采样是否正确
- 参数掉电保存与恢复
- 通信活跃时是否错误进入休眠
- 看门狗复位后的故障记录保留

## 回答风格要求

输出时遵循以下规则：

- 明确区分事实、假设、建议
- 优先给结构化方案，不给空泛建议
- 先画模块边界，再写函数
- 先定义状态机与约束，再写低功耗流程
- 若信息不全，先列假设，再继续产出可执行方案

## 不要做的事

- 不要把 `GPIO` 控制直接散落在业务代码
- 不要在中断中完成整套保护处理
- 不要把低功耗逻辑分散在多个业务模块
- 不要把采样、判定、执行混在一个函数
- 不要在未确认电压安全前执行高风险 `Flash` 写入
