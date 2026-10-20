/*
 * 文件功能：SOC 积分、OCV 校正、端点约束与循环 SOH；明确有效样本、时间差和持久状态之
 * 间的边界。
 * bms/core/bms_soc.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_SOC_H_
#define BMS_SOC_H_

#include <stdint.h>
#include "bms_timing.h"
#include "bms_state_store.h"
#include "bms_soc_defs.h"
#include "bms_diag.h"

/* 充放电方向和恢复的证据门槛；不屏蔽 SOC 电流积分。 */
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u

/* 实板临时积分测试只改变 SOC 输入；正常固件不编译测试状态。 */
#ifndef BMS_SOC_BOARD_TEST_ENABLE
#define BMS_SOC_BOARD_TEST_ENABLE 0
#endif
#if BMS_SOC_BOARD_TEST_ENABLE && defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD
#error "Production firmware must disable SOC board test"
#endif

#define BMS_SOC_OCV_WAIT_CURRENT   0u
#define BMS_SOC_OCV_PREPARE        1u
#define BMS_SOC_OCV_READY          2u
#define BMS_SOC_OCV_CORRECT_DOWN   3u

#define BMS_SOC_ENDPOINT_NORMAL          0u
#define BMS_SOC_ENDPOINT_FULL_APPROACH   1u
#define BMS_SOC_ENDPOINT_CONFIRMED_FULL  2u
#define BMS_SOC_ENDPOINT_EMPTY_APPROACH  3u
#define BMS_SOC_ENDPOINT_CONFIRMED_EMPTY 4u

#include "bms_soc_eta.h"

#define BMS_SOC_SOH_SOURCE_ESTIMATED_CYCLE 1u

/*
 * 与硬件无关的 SOC 输入；单位属于 ABI：
 * mV、mA、既有 (degC+40)*10 温度和毫秒时间。应用按后端有效快照填写结构，
 * SOC 不依赖 DVC/SH 寄存器或传输细节。
 */
typedef struct
{
    uint32_t timestamp_ms;         /* 应用时间；有效缓存也计算新时间区间。 */
    uint32_t timestamp_32k;        /* 仅兼容既有诊断/板测记录，不参与积分。 */
    int32_t current_ma;             /* 负值充电，正值放电。 */
    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    uint16_t cell_delta_mv;
    uint16_t pack_voltage_10mv;
    /* 全部配置串位，不能将断线/失效串位当成未装配而跳过。 */
    uint16_t cell_mv[24];
    uint8_t cell_count;
    uint8_t voltage_calibration_valid;
    uint8_t voltage_check_active;
    uint8_t balance_active;
    uint16_t temperature_min_x10;
    uint16_t temperature_max_x10;
    uint8_t sample_valid;
    uint8_t voltage_valid;
    uint8_t temperature_valid;
    uint8_t afe_fault;
    uint8_t temperature_fault;
    uint8_t current_fault;
    uint8_t pack_fault;
    uint8_t third_cell_ovp;
    uint8_t third_cell_uvp;
    uint8_t third_pack_uvp;
} bms_soc_sample_t;

typedef struct {
    uint32_t nominal_capacity_as10;            /* As*10 */
    uint8_t soc_estimate_percent;              /* 估计 SOC，范围 0..100。 */
    uint32_t remaining_capacity_as10;          /* As*10 */
    uint8_t discharge_fraction_percent;        /* 等效放电百分比累计器。 */
    uint32_t cycle_count;
    uint32_t effective_capacity_as10;          /* As*10 */
    uint8_t soh;
    uint8_t full_confirmed;
    uint32_t full_basis;
} bms_soc_state_t;

extern bms_soc_state_t g_bms_soc;

/* 整套 Config 或曲线已提交后刷新运行配置；不重复保存。 */
uint8_t bms_soc_reload_configuration(void);
/* 取得 SOC 运行诊断快照。 */
#if BMS_SERVICE_DIAG_ENABLE
void bms_soc_get_diag(bms_soc_diag_t *diag);
#endif

/*
 * 应用每 200 ms 提交最新有效电流，按实际应用间隔积分。
 * 应用间隔超过 400 ms 不补算盲区；AFE 活性由后端独立验证。
 * 毫秒时钟通过无符号减法跨回绕。
 */
#define BMS_SOC_MAX_SAMPLE_GAP_MS BMS_SAMPLE_MAX_POLL_GAP_MS
/* 检查样本与时间差后执行积分和 SOC 策略。 */
void bms_soc_process_sample(const bms_soc_sample_t *sample);
#if BMS_SOC_BOARD_TEST_ENABLE
void bms_soc_board_test_prepare(bms_soc_sample_t *sample, uint32_t observation_tick_32k);
void bms_soc_board_test_note(void);
void bms_soc_board_test_suspend_exits(uint32_t count);
uint8_t bms_soc_board_test_active(void);
uint8_t bms_soc_board_test_keep_awake(void);
uint16_t bms_soc_board_test_word(uint16_t offset);
#endif
/* 人工重建内部电量并撤销判满资格；显示按统一限速追赶。 */
void set_soc_param(uint8_t soc);
/* 取得内部计算的真实 SOC 百分比。 */
uint8_t get_soc_real(void);
/* 初始化 SOC 参数、持久状态与策略计数。 */
void soc_param_lib_init(const bms_state_store_data_t *soc);
/* 根据循环次数估算 SOH。 */
uint8_t bms_soh_from_cycle(uint16_t cycle);

/* 名义容量变更后重算容量并重建积分与显示状态。 */
void bms_soc_nominal_capacity_changed(void);
/* 只读运行状态快照；存储不得自行向 SOC 取值或改变资格。 */
bms_state_store_data_t bms_soc_state_snapshot(void);
/* 电压校准候选需要一次新诊断；应用只转交请求，不在 SOC 内操作硬件。 */
uint8_t bms_soc_voltage_check_requested(void);
/* 循环服务操作独立于人工设置 SOC，保留高精度比例。 */
void bms_soc_cycle_count_changed(uint32_t cycle);

#endif /* 头文件保护：BMS_SOC_H_。 */
