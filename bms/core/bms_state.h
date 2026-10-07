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
    uint16_t u16Soc;             /* SOC，单位 %。 */
    uint16_t u16Soh;             /* SOH，单位 %。 */
    uint16_t u16CapacityNow;     /* 上报容量，单位 Ah*100。 */
    uint16_t u16CapacityFull;    /* 满容量，单位 Ah*100。 */
    uint16_t u16CapacityFactory; /* 工厂容量，单位 Ah*100。 */
    uint16_t u16Cycle_times;
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
        uint8_t b1StartUpBMS           : 1;
        uint8_t b1Status_MOS_PRE       : 1;
        uint8_t b1Status_MOS_CHG       : 1;
        uint8_t b1Status_MOS_DSG       : 1;
        uint8_t b1Status_Relay_PRE     : 1;
        uint8_t b1Status_Relay_CHG     : 1;
        uint8_t b1Status_Relay_DSG     : 1;
        uint8_t b1Status_Relay_MAIN    : 1;

        uint8_t b1Status_Heat          : 1;
        uint8_t b1Status_Cool          : 1;
        uint8_t b1Status_AFE1          : 1;
        uint8_t b1Status_AFE2          : 1;
        uint8_t b1Status_Balance       : 1;
        uint8_t b1Status_ToSleep       : 1;
        uint8_t b1Status_BnCloseIO     : 1;
        uint8_t b1Status_HeatCloseIO   : 1;

        uint8_t b1Status_SysLimits     : 1;
        uint8_t b1Status_CBCCloseIO    : 1;
        uint8_t b1Status_DriverExtCtrl : 1;
        uint8_t bReserved0             : 1;
        uint8_t b4Status_ProjectVer    : 4;
        uint8_t bReserved1;
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
    uint8_t b1CellOvp         : 1;
    uint8_t b1CellUvp         : 1;
    uint8_t b1BatOvp          : 1;
    uint8_t b1BatUvp          : 1;

    uint8_t b1IchgOcp         : 1;
    uint8_t b1IdischgOcp      : 1;
    uint8_t b1CellChgOtp      : 1;
    uint8_t b1CellDischgOtp   : 1;

    uint8_t b1CellChgUtp      : 1;
    uint8_t b1CellDischgUtp   : 1;
    uint8_t b1VcellDeltaBig   : 1;
    uint8_t b1TempDeltaBig    : 1;

    uint8_t b1SocLow          : 1;
    uint8_t b1TmosOtp         : 1;
    uint8_t b1Rcved1          : 1;
    uint8_t b1Rcved2          : 1;
} bms_fault_bits_t;

typedef union {
    uint16_t all;
    bms_fault_bits_t bits;
} bms_fault_reg_t;

typedef struct {
    uint16_t u16VCell[32];
    uint16_t u16VCellMax;          /* mV */
    uint16_t u16VCellMin;          /* mV */
    uint16_t u16VCellMaxPosition;
    uint16_t u16VCellMinPosition;
    uint16_t u16VCellDelta;        /* mV */
    uint16_t u16VCellTotle;        /* 电压 V*100，保留旧字段名。 */

    uint16_t u16Temperature[10];   /* 温度编码：(degC + 40) * 10。 */
    uint16_t u16TempMax;
    uint16_t u16TempMin;

    uint16_t u16Ichg;              /* A * 10 */
    uint16_t u16IDischg;           /* A * 10 */

    bms_soc_report_t SocElement;
    bms_fault_reg_t unMdlFault_First;
    bms_fault_reg_t unMdlFault_Second;
    bms_fault_reg_t unMdlFault_Third;

    uint16_t u16BalanceFlag1;
    uint16_t u16BalanceFlag2;
    uint8_t mac_public[6];
} bms_report_t;


extern bms_report_t g_bms_report;

#endif /* 头文件保护：BMS_STATE_H_。 */
