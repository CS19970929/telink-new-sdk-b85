/*
 * 文件功能：BMS 测量报告、故障/状态及历史错误的共享所有者；
 * 保留现有协议字段布局与单位。
 * bms/core/bms_state.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_STATE_H_
#define BMS_STATE_H_

#include <stdint.h>

/* AFE 无关的 BMS 运行/报告状态；字段单位在定义处说明。 */

typedef struct {
    uint16_t soc_percent;                      /* SOC，单位 %。 */
    uint16_t soh_percent;                      /* SOH，单位 %。 */
    uint16_t remaining_capacity_0p01ah;        /* 上报容量，单位 Ah*100。 */
    uint16_t effective_capacity_0p01ah;        /* 满容量，单位 Ah*100。 */
    uint16_t nominal_capacity_0p01ah;          /* 工厂容量，单位 Ah*100。 */
    uint16_t cycle_count;
} bms_soc_report_t;

typedef enum {
    AFE1_TEMP1 = 0,
    AFE1_TEMP2,
    AFE1_TEMP3,
    AFE2_TEMP1,
    AFE2_TEMP2,
    AFE2_TEMP3,
    ENV_TEMP1,
    ENV_TEMP2,
    ENV_TEMP3,
    MOS_TEMP1,
    TEMP_NUM
} bms_temperature_index_t;

typedef union
{
    uint32_t all;
    struct
    {
        uint8_t startup_complete         : 1;
        uint8_t precharge_mos_status     : 1;
        uint8_t charge_mos_status        : 1;
        uint8_t discharge_mos_status     : 1;
        uint8_t precharge_relay_status   : 1;
        uint8_t charge_relay_status      : 1;
        uint8_t discharge_relay_status   : 1;
        uint8_t main_relay_status        : 1;

        uint8_t heater_status            : 1;
        uint8_t cooler_status            : 1;
        uint8_t afe1_status              : 1;
        uint8_t afe2_status              : 1;
        uint8_t balance_status           : 1;
        uint8_t sleep_requested          : 1;
        uint8_t balance_close_requested  : 1;
        uint8_t heater_close_requested   : 1;

        uint8_t system_limits            : 1;
        uint8_t cbc_close_requested      : 1;
        uint8_t external_driver_control  : 1;
        uint8_t reserved_0               : 1;
        uint8_t project_version          : 4;
        uint8_t reserved_1;
    } bits;
} bms_system_status_t;

typedef char bms_system_status_size_must_be_4[
    (sizeof(bms_system_status_t) == 4u) ? 1 : -1];

extern volatile bms_system_status_t g_bms_system_status;

typedef enum
{
    BMS_FAULT_CELL_OVP_FIRST = 1,
    BMS_FAULT_CELL_UVP_FIRST,
    BMS_FAULT_BAT_OVP_FIRST,
    BMS_FAULT_BAT_UVP_FIRST,
    BMS_FAULT_CHG_OCP_FIRST,
    BMS_FAULT_DSG_OCP_FIRST,
    BMS_FAULT_CHG_OTP_FIRST,
    BMS_FAULT_CHG_UTP_FIRST,
    BMS_FAULT_DSG_OTP_FIRST,
    BMS_FAULT_DSG_UTP_FIRST,
    BMS_FAULT_MOS_OTP_FIRST,
    BMS_FAULT_VDELTA_FIRST,
    BMS_FAULT_SOC_LOW_FIRST,

    BMS_FAULT_CELL_OVP_SECOND,
    BMS_FAULT_CELL_UVP_SECOND,
    BMS_FAULT_BAT_OVP_SECOND,
    BMS_FAULT_BAT_UVP_SECOND,
    BMS_FAULT_CHG_OCP_SECOND,
    BMS_FAULT_DSG_OCP_SECOND,
    BMS_FAULT_CHG_OTP_SECOND,
    BMS_FAULT_CHG_UTP_SECOND,
    BMS_FAULT_DSG_OTP_SECOND,
    BMS_FAULT_DSG_UTP_SECOND,
    BMS_FAULT_MOS_OTP_SECOND,
    BMS_FAULT_VDELTA_SECOND,
    BMS_FAULT_SOC_LOW_SECOND,

    BMS_FAULT_CELL_OVP_THIRD,
    BMS_FAULT_CELL_UVP_THIRD,
    BMS_FAULT_BAT_OVP_THIRD,
    BMS_FAULT_BAT_UVP_THIRD,
    BMS_FAULT_CHG_OCP_THIRD,
    BMS_FAULT_DSG_OCP_THIRD,
    BMS_FAULT_CHG_OTP_THIRD,
    BMS_FAULT_CHG_UTP_THIRD,
    BMS_FAULT_DSG_OTP_THIRD,
    BMS_FAULT_DSG_UTP_THIRD,
    BMS_FAULT_MOS_OTP_THIRD,
    BMS_FAULT_VDELTA_THIRD,
    BMS_FAULT_SOC_LOW_THIRD
} bms_fault_code_t;

typedef char bms_fault_codes_must_match_protocol[
    (BMS_FAULT_CELL_OVP_SECOND == 14 &&
     BMS_FAULT_CELL_OVP_THIRD == 27 &&
     BMS_FAULT_SOC_LOW_THIRD == 39) ? 1 : -1];

typedef enum
{
    BMS_FAULT_LEVEL_FIRST = 1,
    BMS_FAULT_LEVEL_SECOND,
    BMS_FAULT_LEVEL_THIRD
} bms_fault_level_t;

#define BMS_FAULT_HISTORY_DEPTH 10u

/* 将故障编号追加到 RAM 历史队列。 */
void bms_fault_history_record(bms_fault_code_t fault);
/* 取得指定位置的最近故障编号。 */
uint8_t bms_fault_history_recent(bms_fault_level_t level, uint8_t age);
/* 在成对的 16 位查表数据中插值，越界时返回端点值。 */
uint16_t bms_lookup_u16(const uint16_t *table, uint16_t table_size, uint16_t input);

typedef struct {
    uint8_t cell_ovp                 : 1;
    uint8_t cell_uvp                 : 1;
    uint8_t pack_ovp                 : 1;
    uint8_t pack_uvp                 : 1;

    uint8_t charge_ocp               : 1;
    uint8_t discharge_ocp            : 1;
    uint8_t charge_otp               : 1;
    uint8_t discharge_otp            : 1;

    uint8_t charge_utp               : 1;
    uint8_t discharge_utp            : 1;
    uint8_t cell_delta_high          : 1;
    uint8_t temperature_delta_high   : 1;

    uint8_t soc_low                  : 1;
    uint8_t mos_otp                  : 1;
    uint8_t reserved_0               : 1;
    uint8_t reserved_1               : 1;
} bms_fault_bits_t;

typedef union {
    uint16_t all;
    bms_fault_bits_t bits;
} bms_fault_reg_t;

typedef struct {
    uint16_t cell_voltage_mv[32];
    uint16_t cell_max_mv;                      /* mV */
    uint16_t cell_min_mv;                      /* mV */
    uint16_t cell_max_index;
    uint16_t cell_min_index;
    uint16_t cell_delta_mv;                    /* mV */
    uint16_t pack_voltage_10mv;                /* 电压 V*100。 */

    uint16_t temperature_x10[10];              /* 温度编码：(degC + 40) * 10。 */
    uint16_t temperature_max_x10;
    uint16_t temperature_min_x10;

    uint16_t charge_current_a10;               /* A * 10 */
    uint16_t discharge_current_a10;            /* A * 10 */

    bms_soc_report_t soc;
    bms_fault_reg_t fault_first;
    bms_fault_reg_t fault_second;
    bms_fault_reg_t fault_third;

    uint16_t balance_bits_low;
    uint16_t balance_bits_high;
    uint8_t mac_public[6];
} bms_report_t;


extern bms_report_t g_bms_report;

#endif /* 头文件保护：BMS_STATE_H_。 */
