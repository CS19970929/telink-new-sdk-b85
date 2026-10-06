/*
 * 文件功能：SOC 积分、OCV 校正、端点约束与容量学习；明确有效样本、时间差和持久状态之
 * 间的边界。
 * bms/core/bms_soc.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_SOC_H_
#define BMS_SOC_H_

#include "conf.h"
#include "bms_state_store.h"
#include "bms_soc_defs.h"
#include "bms_diag.h"

#define BMS_SOC_OCV_WAIT_CURRENT   0u
#define BMS_SOC_OCV_PREPARE        1u
#define BMS_SOC_OCV_READY          2u
#define BMS_SOC_OCV_CORRECT_DOWN   3u

#define BMS_SOC_LEARNING_NONE          0u
#define BMS_SOC_LEARNING_EMPTY_TO_FULL 1u
#define BMS_SOC_LEARNING_FULL_TO_EMPTY 2u

#define BMS_SOC_ENDPOINT_NORMAL          0u
#define BMS_SOC_ENDPOINT_FULL_APPROACH   1u
#define BMS_SOC_ENDPOINT_CONFIRMED_FULL  2u
#define BMS_SOC_ENDPOINT_EMPTY_APPROACH  3u
#define BMS_SOC_ENDPOINT_CONFIRMED_EMPTY 4u

#include "bms_soc_eta.h"

#define BMS_SOC_SOH_SOURCE_ESTIMATED_CYCLE 1u
#define BMS_SOC_SOH_SOURCE_CAPACITY       2u

#define BMS_SOC_LEARNING_REJECT_NONE                   0u
#define BMS_SOC_LEARNING_REJECT_INVALID_SAMPLE         1u
#define BMS_SOC_LEARNING_REJECT_SAMPLE_GAP             2u
#define BMS_SOC_LEARNING_REJECT_REBOOT                  3u
#define BMS_SOC_LEARNING_REJECT_DIRECTION_REVERSE       4u
#define BMS_SOC_LEARNING_REJECT_OPEN_WIRE               5u
#define BMS_SOC_LEARNING_REJECT_CELL_IMBALANCE          6u
#define BMS_SOC_LEARNING_REJECT_TEMPERATURE              7u
#define BMS_SOC_LEARNING_REJECT_PROTECTION               8u
#define BMS_SOC_LEARNING_REJECT_LOW_QUALITY_EMPTY        9u
#define BMS_SOC_LEARNING_REJECT_LOW_QUALITY_FULL        10u
#define BMS_SOC_LEARNING_REJECT_CAPACITY_RANGE          11u
#define BMS_SOC_LEARNING_REJECT_CANDIDATE_INCONSISTENT  12u
#define BMS_SOC_LEARNING_REJECT_AFE_COMMUNICATION       13u
#define BMS_SOC_LEARNING_REJECT_CALIBRATION_CHANGED     14u
#define BMS_SOC_LEARNING_REJECT_BALANCING               15u
#define BMS_SOC_LEARNING_REJECT_HEATING                 16u
#define BMS_SOC_LEARNING_REJECT_CHARGER_CHANGE          17u
#define BMS_SOC_LEARNING_REJECT_LOAD_CHANGE             18u

/*
 * 与硬件无关的 SOC 输入；单位属于 ABI：
 * mV、mA、既有 (degC+40)*10 温度和 SDK 32 kHz时间。AFE 后端填写结构，
 * SOC 不依赖 DVC/SH 寄存器或传输细节。
 */
typedef struct
{
    uint32_t timestamp_32k;
    uint32_t pack_voltage_mv;
    int32_t current_ma;             /* 负值充电，正值放电。 */
    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    uint16_t cell_delta_mv;
    uint16_t temperature_min_x10;
    uint16_t temperature_max_x10;
    uint8_t sample_valid;
    uint8_t voltage_valid;
    uint8_t temperature_valid;
    uint8_t balancing_active;
    uint8_t heating_active;
    uint8_t open_wire_active;       /* 有效激励或当前诊断电压帧。 */
    uint8_t open_wire_suspected;
    uint8_t afe_fault;
    uint8_t temperature_fault;
    uint8_t current_fault;
    uint8_t pack_fault;
    uint8_t third_cell_ovp;
    uint8_t third_cell_uvp;
    uint8_t charger_state_known;
    uint8_t charger_present;
    uint8_t load_state_known;
    uint8_t load_present;
} bms_soc_sample_t;

typedef struct
{
    uint8_t chemistry;                  /* 化学体系选择：AUTO/LFP/NMC。 */
    uint8_t profile_id;                 /* 配置选择：AUTO/通用 LFP/通用 NMC。 */
    uint16_t current_deadband_ma;       /*
     * 不超过此值的电流忽略，D008 可信下限也生效。
     */
    uint16_t ocv_rest_prepare_s;        /* 允许 OCV 修正前所需的连续静置时间。 */
    uint8_t ocv_error_band_percent;     /* OCV 中心上下的百分比点范围。 */
    uint8_t capacity_learning_enable;   /* 默认关闭。 */
    uint8_t hide_capacity_until_learned;/* 仅学习使能时有效。 */
} bms_soc_config_t;

struct SOC_CALCULATE_ELEMENT
{
    UINT32 u32CapFactory;         /* As*10 */
    uint8_t u8SOC_Now;            /* 估计 SOC，范围 0..100。 */
    UINT32 u32CapNow;             /* As*10 */
    uint8_t u8DSG_SOC_Int;        /* 等效放电百分比累计器。 */
    UINT32 u32Cycle_times;
    UINT32 u32CapFull;            /* As*10 */
    uint8_t soh;
};

extern struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;

/* 构造 SOC 算法默认值与自动化学体系设置。 */
void bms_soc_get_default_config(bms_soc_config_t *config);
/* 校验 SOC 配置范围与化学体系标识。 */
uint8_t bms_soc_config_valid(const bms_soc_config_t *config);
/* 应用有效 SOC 配置并刷新曲线与容量状态。 */
uint8_t bms_soc_configure(const bms_soc_config_t *config);
/* 取得 SOC 运行诊断快照。 */
void bms_soc_get_diag(bms_soc_diag_t *diag);

/*
 * 400 ms 等于两个名义样本；更长/未观测间隔不积分也不计静置。
 * SDK 32K 时钟通过无符号减法跨回绕。
 */
#define BMS_SOC_TIME_TICKS_PER_SECOND 32000u
#define BMS_SOC_MAX_SAMPLE_GAP_32K    12800u
/* 检查样本与时间差后执行积分和 SOC 策略。 */
void bms_soc_process_sample(const bms_soc_sample_t *sample);
/* 更新 SOC 参数与相关容量状态。 */
void set_soc_param(uint8_t soc, uint8_t sync_display);
/* 取得内部计算的真实 SOC 百分比。 */
uint8_t get_soc_real(void);
/* 初始化 SOC 参数、持久状态与策略计数。 */
void soc_param_lib_init(const bms_state_store_data_t *soc);
/* 根据循环次数估算 SOH。 */
uint8_t bms_soh_from_cycle(uint16_t cycle);

/* 名义容量变更后重算容量并复位相关学习状态。 */
void bms_soc_nominal_capacity_changed(void);

#endif /* 头文件保护：BMS_SOC_H_。 */
