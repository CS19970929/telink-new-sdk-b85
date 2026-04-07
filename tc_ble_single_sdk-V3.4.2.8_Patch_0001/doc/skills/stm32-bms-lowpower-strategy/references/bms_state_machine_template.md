# BMS 状态机与任务切片模板

本文件用于快速产出 `bms_core` 的状态机、周期任务和关键接口。

## 推荐模块

```text
bms_core/
  bms_core.h
  bms_core.c
  bms_state.h
  bms_state.c
  bms_sample.c
  bms_protect.c
  bms_balance.c
  bms_fault.c
```

## 状态定义模板

```c
typedef enum {
    BMS_STATE_INIT = 0,
    BMS_STATE_SELF_TEST,
    BMS_STATE_IDLE,
    BMS_STATE_CHARGE,
    BMS_STATE_DISCHARGE,
    BMS_STATE_FULL,
    BMS_STATE_SLEEP_PREPARE,
    BMS_STATE_SLEEP,
    BMS_STATE_WAKEUP_RECOVER,
    BMS_STATE_FAULT,
} bms_state_t;
```

## 关键数据结构模板

```c
typedef struct {
    uint16_t cell_mv[16];
    uint16_t pack_mv;
    int32_t current_ma;
    int16_t ntc_ddegc[8];
} bms_sample_t;

typedef struct {
    uint32_t active_fault_mask;
    uint32_t latched_fault_mask;
    bms_state_t state;
    uint8_t charge_enable;
    uint8_t discharge_enable;
    uint8_t balance_enable;
} bms_runtime_t;
```

## 关键接口模板

```c
int bms_core_init(void);
void bms_core_task_1ms(void);
void bms_core_task_10ms(void);
void bms_core_task_100ms(void);
void bms_core_request_sleep(void);
void bms_core_on_wakeup(void);
const bms_runtime_t *bms_core_get_runtime(void);
```

## 周期任务建议

- `1ms`：快速故障采样触发、通信超时计数
- `10ms`：电压、电流、温度采样更新与状态机调度
- `100ms`：保护恢复判断、均衡控制、日志上报
- `1s`：SOC 输入同步、寿命统计、参数存盘申请

## 状态迁移约束

- `SELF_TEST` 未通过时不得进入 `CHARGE / DISCHARGE`
- `FAULT` 状态下禁止直接清故障，必须走 `fault_mgr`
- `SLEEP_PREPARE` 期间禁止启动新一轮高频采样
- `WAKEUP_RECOVER` 完成前禁止重新开放充放电 MOS

## 任务与模块职责

- `bms_sample`：驱动采样流程，不做保护动作
- `bms_protect`：只负责判定和动作建议
- `bms_balance`：读取运行态后输出均衡决策
- `bms_fault`：统一故障锁存、清除、日志接口
- `bms_state`：唯一合法状态切换入口

## 建议的主循环调用关系

```text
sample update
-> engineering convert
-> protection evaluate
-> fault update
-> state transition
-> mos/balance action
-> power condition evaluate
```
