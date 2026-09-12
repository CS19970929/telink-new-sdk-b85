#ifndef BMS_STATE_H_
#define BMS_STATE_H_

#include <stdint.h>

/*
 * AFE-independent BMS runtime/report state.
 *
 * Field names are intentionally kept compatible with the existing firmware so
 * this extraction does not change layout, protocol mapping or runtime logic.
 * All modules include this header directly; the former sci_upper.h compatibility
 * include has been removed so the core state has one obvious definition site.
 */

struct SOC_CAL_ELEMENT_UPPER {
    uint16_t u16Soc;             /* SOC, % */
    uint16_t u16Soh;             /* SOH, % */
    uint16_t u16CapacityNow;     /* reported capacity, Ah * 100 */
    uint16_t u16CapacityFull;    /* full capacity, Ah * 100 */
    uint16_t u16CapacityFactory; /* factory capacity, Ah * 100 */
    uint16_t u16Cycle_times;
};

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
    BMS_FAULT_SOC_HIGH_FIRST,

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
    BMS_FAULT_SOC_HIGH_SECOND,

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
    BMS_FAULT_SOC_HIGH_THIRD
} bms_fault_code_t;

typedef char bms_fault_codes_must_match_protocol[
    (BMS_FAULT_CELL_OVP_SECOND == 14 &&
     BMS_FAULT_CELL_OVP_THIRD == 27 &&
     BMS_FAULT_SOC_HIGH_THIRD == 39) ? 1 : -1];

typedef enum
{
    BMS_FAULT_LEVEL_FIRST = 1,
    BMS_FAULT_LEVEL_SECOND,
    BMS_FAULT_LEVEL_THIRD
} bms_fault_level_t;

#define BMS_FAULT_HISTORY_DEPTH 10u

void bms_fault_history_record(bms_fault_code_t fault);
uint8_t bms_fault_history_recent(bms_fault_level_t level, uint8_t age);
uint16_t bms_lookup_u16(const uint16_t *table, uint16_t table_size, uint16_t input);

struct MDLCHGFAULT_BITS {
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
};

union MDLCHGFAULT_REG {
    uint16_t all;
    struct MDLCHGFAULT_BITS bits;
};

struct stCell_Info {
    uint16_t u16VCell[32];
    uint16_t u16VCellMax;          /* mV */
    uint16_t u16VCellMin;          /* mV */
    uint16_t u16VCellMaxPosition;
    uint16_t u16VCellMinPosition;
    uint16_t u16VCellDelta;        /* mV */
    uint16_t u16VCellTotle;        /* V * 100, legacy field name kept */

    uint16_t u16Temperature[10];   /* (degC + 40) * 10 */
    uint16_t u16TempMax;
    uint16_t u16TempMin;

    uint16_t u16Ichg;              /* A * 10 */
    uint16_t u16IDischg;           /* A * 10 */

    struct SOC_CAL_ELEMENT_UPPER SocElement;
    union MDLCHGFAULT_REG unMdlFault_First;
    union MDLCHGFAULT_REG unMdlFault_Second;
    union MDLCHGFAULT_REG unMdlFault_Third;

    uint16_t u16BalanceFlag1;
    uint16_t u16BalanceFlag2;
    uint8_t mac_public[6];
};

/* Preferred neutral names for new template code. */
typedef struct SOC_CAL_ELEMENT_UPPER bms_soc_report_t;
typedef struct MDLCHGFAULT_BITS bms_fault_bits_t;
typedef union MDLCHGFAULT_REG bms_fault_reg_t;
typedef struct stCell_Info bms_state_t;

extern struct stCell_Info g_stCellInfoReport;

#endif /* BMS_STATE_H_ */
