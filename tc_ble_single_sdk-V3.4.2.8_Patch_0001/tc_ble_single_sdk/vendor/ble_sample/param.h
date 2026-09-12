#ifndef PARAM_H_
#define PARAM_H_

#include "conf.h"

#define PARAM_VER 0xFFF0u
#define SNum      SeriesNum

/*
 * Protection parameter layout is compatibility-sensitive:
 * - Modbus maps 0x2100..0x2140 by field offset.
 * - cold KV uses field offsets as stable key mappings.
 * Keep field order unchanged unless a migration is implemented deliberately.
 */
struct PRT_E2ROM_PARAS
{
    u16 u16VcellOvp_First;
    u16 u16VcellOvp_Second;
    u16 u16VcellOvp_Third;
    u16 u16VcellOvp_Rcv;
    u16 u16VcellOvp_Filter;

    u16 u16VcellUvp_First;
    u16 u16VcellUvp_Second;
    u16 u16VcellUvp_Third;
    u16 u16VcellUvp_Rcv;
    u16 u16VcellUvp_Filter;

    u16 u16VbusOvp_First;
    u16 u16VbusOvp_Second;
    u16 u16VbusOvp_Third;
    u16 u16VbusOvp_Rcv;
    u16 u16VbusOvp_Filter;

    u16 u16VbusUvp_First;
    u16 u16VbusUvp_Second;
    u16 u16VbusUvp_Third;
    u16 u16VbusUvp_Rcv;
    u16 u16VbusUvp_Filter;

    u16 u16IchgOcp_First;
    u16 u16IchgOcp_Second;
    u16 u16IchgOcp_Third;
    u16 u16IchgOcp_Rcv;
    u16 u16IchgOcp_Filter;

    u16 u16IdsgOcp_First;
    u16 u16IdsgOcp_Second;
    u16 u16IdsgOcp_Third;
    u16 u16IdsgOcp_Rcv;
    u16 u16IdsgOcp_Filter;

    u16 u16TChgOTp_First;
    u16 u16TChgOTp_Second;
    u16 u16TChgOTp_Third;
    u16 u16TChgOTp_Rcv;
    u16 u16TChgOTp_Filter;

    u16 u16TchgUTp_First;
    u16 u16TchgUTp_Second;
    u16 u16TchgUTp_Third;
    u16 u16TchgUTp_Rcv;
    u16 u16TchgUTp_Filter;

    u16 u16TdischgOTp_First;
    u16 u16TdischgOTp_Second;
    u16 u16TdischgOTp_Third;
    u16 u16TdischgOTp_Rcv;
    u16 u16TdischgOTp_Filter;

    u16 u16TdischgUTp_First;
    u16 u16TdischgUTp_Second;
    u16 u16TdischgUTp_Third;
    u16 u16TdischgUTp_Rcv;
    u16 u16TdischgUTp_Filter;

    u16 u16TmosOTp_First;
    u16 u16TmosOTp_Second;
    u16 u16TmosOTp_Third;
    u16 u16TmosOTp_Rcv;
    u16 u16TmosOTp_Filter;

    u16 u16VdeltaOvp_First;
    u16 u16VdeltaOvp_Second;
    u16 u16VdeltaOvp_Third;
    u16 u16VdeltaOvp_Rcv;
    u16 u16VdeltaOvp_Filter;

    u16 u16SocUp_First;
    u16 u16SocUp_Second;
    u16 u16SocUp_Third;
    u16 u16SocUp_Rcv;
    u16 u16SocUp_Filter;
};

#define COV_1            3750u
#define COV_2            3750u
#define COV_3            3750u
#define COV_recover      3500u
#define COV_filter3      100u

#define CUV_1            3000u
#define CUV_2            3000u
#define CUV_3            3000u
#define CUV_recover      3100u
#define CUV_filter3      1000u

#define BOV_1            (350u * SNum)
#define BOV_2            (360u * SNum)
#define BOV_3            (365u * SNum)
#define BOV_recover      (350u * SNum)
#define BOV_filter3      100u

#define BUV_1            (300u * SNum)
#define BUV_2            (300u * SNum)
#define BUV_3            (290u * SNum)
#define BUV_recover      (300u * SNum)
#define BUV_filter3      100u

#define OTC_1            ((40 + 40) * 10)
#define OTC_2            ((50 + 40) * 10)
#define OTC_3            ((55 + 40) * 10)
#define OTC_recover      ((50 + 40) * 10)
#define OTC_filter3      100u

#define UTC_1            ((5 + 40) * 10)
#define UTC_2            ((3 + 40) * 10)
#ifdef __FUNC__HEAT__
#if (AFE_TYPE == sh36xx)
#define UTC_3            ((-20 + 40) * 10)
#elif (AFE_TYPE == bq76xx_afe)
#define UTC_3            ((-28 + 40) * 10)
#endif
#else
#define UTC_3            ((0 + 40) * 10)
#endif
#define UTC_recover      ((3 + 40) * 10)
#define UTC_filter3      100u

#define OTD_1            ((50 + 40) * 10)
#define OTD_2            ((50 + 40) * 10)
#define OTD_3            ((60 + 40) * 10)
#define OTD_recover      ((50 + 40) * 10)
#define OTD_filter3      100u

#define UTD_1            ((-10 + 40) * 10)
#define UTD_2            ((-15 + 40) * 10)
#define UTD_3            ((-20 + 40) * 10)
#define UTD_recover      ((-10 + 40) * 10)
#define UTD_filter3      100u

#define mos_1            ((75 + 40) * 10)
#define mos_2            ((85 + 40) * 10)
#define mos_3            ((95 + 40) * 10)
#define mos_recover      ((80 + 40) * 10)
#define mos_filter3      100u

#define VDELTER_1        600u
#define VDELTER_2        800u
#define VDELTER_3        1000u
#define VDELTER_recover  800u
#define VDELTER_filter3  100u

#define socLow_1         20u
#define socLow_2         10u
#define socLow_3         5u
#define socLow_recover   11u
#define socLow_filter3   100u

#define OCC_1            100u
#define OCC_2            150u
#define OCC_3            200u
#define OCC_recover      100u
#define OCC_filter3      10u

#define ODC_1            100u
#define ODC_2            150u
#define ODC_3            200u
#define ODC_recover      100u
#define ODC_filter3      10u

#define E2P_PROTECT_DEFAULT_PRT { \
    COV_1, COV_2, COV_3, COV_recover, COV_filter3, \
    CUV_1, CUV_2, CUV_3, CUV_recover, CUV_filter3, \
    BOV_1, BOV_2, BOV_3, BOV_recover, BOV_filter3, \
    BUV_1, BUV_2, BUV_3, BUV_recover, BUV_filter3, \
    OCC_1, OCC_2, OCC_3, OCC_recover, OCC_filter3, \
    ODC_1, ODC_2, ODC_3, ODC_recover, ODC_filter3, \
    OTC_1, OTC_2, OTC_3, OTC_recover, OTC_filter3, \
    UTC_1, UTC_2, UTC_3, UTC_recover, UTC_filter3, \
    OTD_1, OTD_2, OTD_3, OTD_recover, OTD_filter3, \
    UTD_1, UTD_2, UTD_3, UTD_recover, UTD_filter3, \
    mos_1, mos_2, mos_3, mos_recover, mos_filter3, \
    VDELTER_1, VDELTER_2, VDELTER_3, VDELTER_recover, VDELTER_filter3, \
    socLow_1, socLow_2, socLow_3, socLow_recover, socLow_filter3 \
}

typedef struct
{
    u16 ParamVer;
    struct PRT_E2ROM_PARAS protect;
} PARAM_T;

extern PARAM_T g_tParam;

void LoadParam(void);
void SaveParam(void);
void Param_UpgradeReset_Apply(void);

#endif /* PARAM_H_ */
