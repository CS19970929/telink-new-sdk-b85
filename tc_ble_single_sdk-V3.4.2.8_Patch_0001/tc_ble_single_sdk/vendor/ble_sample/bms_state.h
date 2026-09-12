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

#endif /* BMS_STATE_H_ */
