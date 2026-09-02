#include "sh367309_datadeal.h"
#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "sci_upper.h"
#include "app.h"
#include "param.h"
#include "bms_cold_kv_store.h"

int AFE_PARAM_WRITE_Flag = 1;
int AFE_ResetFlag = 0;
extern struct stCell_Info g_stCellInfoReport;
extern uint32_t g_u32CS_Res_AFE;

UINT32 u32_ChgCur_mA = 0;
UINT32 u32_DsgCur_mA = 0;
u32 System_ERROR_UserCallback(enum SYSTEM_ERROR_COMMAND errorCode);
volatile union System_Status SystemStatus;

static void DataLoad_ClearCurrent(void)
{
    u32_ChgCur_mA = 0u;
    u32_DsgCur_mA = 0u;
    g_stCellInfoReport.u16Ichg = 0u;
    g_stCellInfoReport.u16IDischg = 0u;
}

static void DataLoad_ClearAfeReportPreserveSoc(void)
{
    struct SOC_CAL_ELEMENT_UPPER soc = g_stCellInfoReport.SocElement;
    u8 mac_public[sizeof(g_stCellInfoReport.mac_public)];

    memcpy(mac_public, g_stCellInfoReport.mac_public, sizeof(mac_public));
    memset(&g_stCellInfoReport, 0, sizeof(g_stCellInfoReport));
    g_stCellInfoReport.SocElement = soc;
    memcpy(g_stCellInfoReport.mac_public, mac_public, sizeof(mac_public));
}

UINT8 FaultPoint_First2;
UINT8 FaultPoint_Second2;
UINT8 FaultPoint_Third2;

UINT16 Fault_record_First2[Record_len];
UINT16 Fault_record_Second2[Record_len];
UINT16 Fault_record_Third2[Record_len];

UINT8 Monitor_TempBreak(UINT16 *temp_AD)
{
    static UINT8 su8_Recover_Cnt = 0;
    static UINT8 su8_StartUp_Flag = 0;
    static UINT8 su8_Delay_Cnt = 0;
    UINT8 result = 0;

    switch (su8_StartUp_Flag)
    {
    case 0: // 鍒氬紑鏈猴紝涓嶈兘鍒ゆ柇锛屽洜涓烘煡璇FE鍑芥暟宸茬粡琚垎鍓诧紝涓嶈兘鎷垮埌鏁版嵁锛屾鏃跺垽鏂繀涓洪敊
        if (++su8_Delay_Cnt >= 20)
        {
            su8_Delay_Cnt = 0;
            su8_StartUp_Flag = 1;
        }
        break;

    case 1:
        if (*temp_AD < 110)
        {
            ++result;
            *temp_AD = 110; // 瀹氭鍦�-29鎽勬皬搴︺�備互闃蹭笂浣嶆満鏄剧ずNA浠ヤ负娌￠棶棰�
            System_ERROR_UserCallback(ERROR_TEMP_BREAK);
            su8_Recover_Cnt = 0;
        }
        else
        {
            if (System_ERROR_UserCallback(ERROR_STATUS_TEMP_BREAK))
            {
                if (++su8_Recover_Cnt >= 50)
                { // 鍒ゆ柇50娆¤嚜鍔ㄥ鍘燂紝绾︿负200*50=10s
                    su8_Recover_Cnt = 0;
                    System_ERROR_UserCallback(ERROR_REMOVE_TEMP_BREAK);
                }
            }
        }
        break;

    default:
        su8_StartUp_Flag = 0;
        break;
    }

    return result;
}

const unsigned char SeriesSelect_AFE1[16][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 1涓�
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 2涓�
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 3
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 4
    {0, 1, 2, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 5
    {0, 1, 2, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 6
    {0, 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 7
    {0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0},      // 8
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0},      // 9
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0},      // 10
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0, 0},     // 11
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0, 0, 0},    // 12
    {0, 1, 2, 3, 4, 5, 6, 7, 9, 9, 10, 11, 12, 0, 0, 0},   // 13
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 0},  // 14
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0}, // 15
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15} // 16
};

#define LENGTH_TBLTEMP_AFE_10K ((UINT16)56)
const UINT16 iSheldTemp_10K_AFE[LENGTH_TBLTEMP_AFE_10K] = {
    // AD(k惟*100)		(Temp+40)*10
    11611,
    100, //-30
    8935,
    150, //-25
    6943,
    200, //-20
    5442,
    250, //-15
    4300,
    300, //-10
    3422,
    350, //-5
    2751,
    400, // 0
    2214,
    450, // 5
    1801,
    500, // 10
    1470,
    550, // 15
    1209,
    600, // 20
    1000,
    650, // 25
    831,
    700, // 30
    694,
    750, // 35
    583,
    800, // 40
    492,
    850, // 45
    416,
    900, // 50
    355,
    950, // 55
    303,
    1000, // 60
    260,
    1050, // 65
    224,
    1100, // 70
    193,
    1150, // 75
    167,
    1200, // 80
    146,
    1250, // 85
    127,
    1300, // 90
    111,
    1350, // 95
    98,
    1400, // 100
    86,
    1450, // 105
};

u16 iSheldTemp_10K_NTC[141] = {20375, 19204, 18115, 17100, 16152, 15266, 14437, 13661, 12934, 12251,
                               11611, 11008, 10442, 9909, 9407, 8935, 8489, 8068, 7672, 7297,
                               6943, 6608, 6292, 5993, 5710, 5442, 5188, 4948, 4720, 4504,
                               4300, 4105, 3921, 3746, 3580, 3422, 3272, 3130, 2994, 2866,
                               2751, 2627, 2516, 2410, 2310, 2214, 2123, 2036, 1953, 1874,
                               1801, 1726, 1658, 1592, 1530, 1470, 1413, 1358, 1306, 1256,
                               1209, 1163, 1119, 1078, 1038, 1000, 963, 928, 894, 862,
                               831, 801, 773, 746, 719, 694, 670, 647, 625, 604,
                               583, 563, 544, 526, 509, 492, 476, 460, 445, 431,
                               416, 403, 390, 378, 366, 355, 343, 333, 322, 312,
                               303, 294, 285, 276, 268, 260, 252, 244, 237, 230,
                               224, 217, 211, 205, 199, 193, 188, 182, 177, 172,
                               167, 163, 158, 154, 150, 146, 142, 138, 134, 131,
                               127, 124, 120, 117, 114, 111, 108, 106, 103, 100,
                               98};

// 閸擄拷26娑擃亜鐦庣�涙ê娅掓妯款吇閸欏倹鏆�
u8 ucMTPBuffer[26] = {
    BYTE_00H_SCONF1, BYTE_01H_SCONF2, BYTE_02H_OVT_LDRT_OVH, BYTE_03H_OVL, BYTE_04H_UVT_OVRH,
    BYTE_05H_OVRL, BYTE_06H_UV, BYTE_07H_UVR, BYTE_08H_BALV, BYTE_09H_PREV,
    BYTE_0AH_L0V, BYTE_0BH_PFV, BYTE_0CH_OCD1V_OCD1T, BYTE_0DH_OCD2V_OCD2T, BYTE_0EH_SCV_SCT,
    BYTE_0FH_OCCV_OCCT, BYTE_10H_MOST_OCRT_PFT, BYTE_11H_OTC, BYTE_12H_OTCR, BYTE_13H_UTC,
    BYTE_14H_UTCR, BYTE_15H_OTD, BYTE_16H_OTDR, BYTE_17H_UTD, BYTE_18H_UTDR,
    BYTE_19H_TR};

const u16 AFE_OCD1V_OCCV[16] = {20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 160, 180, 200};                    // 娑擄拷缁狙勬杹閻絻绻冨ù浣告嫲閸忓懐鏁告潻鍥ㄧウ閿涘苯宕熸担宄瑅
const u16 AFE_OCD2V[16] = {30, 40, 50, 60, 70, 80, 90, 100, 120, 140, 160, 180, 200, 300, 400, 500};                    // 娑擄拷缁狙勬杹閻絻绻冨ù浣告嫲閸忓懐鏁告潻鍥ㄧウ閿涘苯宕熸担宄瑅
const u16 AFE_SCV[16] = {50, 80, 110, 140, 170, 200, 230, 260, 290, 320, 350, 400, 500, 600, 800, 1000};                    // 閻叀鐭炬穱婵囧Б閻㈤潧甯囬敍灞藉礋娴ｅ超v
const u16 AFE_OVT_UVT[16] = {100, 200, 300, 400, 600, 800, 1000, 2000, 3000, 4000, 6000, 8000, 10000, 20000, 30000, 40000}; // 鏉╁洤甯囨担搴″竾瀵よ埖妞傞弮鍫曟？閵嗗倸宕熸担宄瑂
const u16 AFE_SCT[16] = {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};                      // 閻叀鐭惧鑸垫,閸楁洑缍卽s閵嗭拷
const u16 AFE_OCD1T[16] = {50, 100, 200, 400, 600, 800, 1000, 2000, 4000, 6000, 8000, 10000, 15000, 20000, 30000, 40000};   // 閺�鍓ф暩鏉╁洦绁�1瀵よ埖妞傞妴鍌氬礋娴ｅ超s
const u16 AFE_OCCT_OCD2T[16] = {10, 20, 40, 60, 80, 100, 200, 400, 600, 800, 1000, 2000, 4000, 8000, 10000, 20000};

#define SH309_ARRAY_SIZE(a) ((u16)(sizeof(a) / sizeof((a)[0])))

#define SH309_AFE_PARAM_FIELD_LIST(X) \
    X(SH309_AFE_PARAM_VCELL_OVP, u16VcellOvp) \
    X(SH309_AFE_PARAM_VCELL_OVP_RCV, u16VcellOvp_Rcv) \
    X(SH309_AFE_PARAM_VCELL_OVP_FILTER, u16VcellOvp_Filter) \
    X(SH309_AFE_PARAM_VCELL_UVP, u16VcellUvp) \
    X(SH309_AFE_PARAM_VCELL_UVP_RCV, u16VcellUvp_Rcv) \
    X(SH309_AFE_PARAM_VCELL_UVP_FILTER, u16VcellUvp_Filter) \
    X(SH309_AFE_PARAM_ICHG_OCP_FIRST, u16IchgOcp_First) \
    X(SH309_AFE_PARAM_ICHG_OCP_FILTER_FIRST, u16IchgOcp_Filter_First) \
    X(SH309_AFE_PARAM_ICHG_OCP_SECOND, u16IchgOcp_Second) \
    X(SH309_AFE_PARAM_ICHG_OCP_FILTER_SECOND, u16IchgOcp_Filter_Second) \
    X(SH309_AFE_PARAM_IDSG_OCP_FIRST, u16IdsgOcp_First) \
    X(SH309_AFE_PARAM_IDSG_OCP_FILTER_FIRST, u16IdsgOcp_Filter_First) \
    X(SH309_AFE_PARAM_IDSG_OCP_SECOND, u16IdsgOcp_Second) \
    X(SH309_AFE_PARAM_IDSG_OCP_FILTER_SECOND, u16IdsgOcp_Filter_Second) \
    X(SH309_AFE_PARAM_TCHG_OTP, u16TChgOTp) \
    X(SH309_AFE_PARAM_TCHG_OTP_RCV, u16TChgOTp_Rcv) \
    X(SH309_AFE_PARAM_TCHG_UTP, u16TchgUTp) \
    X(SH309_AFE_PARAM_TCHG_UTP_RCV, u16TchgUTp_Rcv) \
    X(SH309_AFE_PARAM_TDSG_OTP, u16TdischgOTp) \
    X(SH309_AFE_PARAM_TDSG_OTP_RCV, u16TdischgOTp_Rcv) \
    X(SH309_AFE_PARAM_TDSG_UTP, u16TdischgUTp) \
    X(SH309_AFE_PARAM_TDSG_UTP_RCV, u16TdischgUTp_Rcv) \
    X(SH309_AFE_PARAM_SC_CURRENT, u16CBC_Cur_DSG) \
    X(SH309_AFE_PARAM_SC_DELAY, u16CBC_DelayT)

static AFE_Value_Typedef *sh309_afe_param_field(AFE_Parameters_RS485_Typedef *params, u16 index)
{
    if (params == NULL) {
        return NULL;
    }
    switch (index) {
#define SH309_FIELD_CASE(index_value, field_name) case index_value: return &params->field_name;
        SH309_AFE_PARAM_FIELD_LIST(SH309_FIELD_CASE)
#undef SH309_FIELD_CASE
    default:
        return NULL;
    }
}

static const AFE_Value_Typedef *sh309_afe_param_field_const(const AFE_Parameters_RS485_Typedef *params, u16 index)
{
    return sh309_afe_param_field((AFE_Parameters_RS485_Typedef *)params, index);
}

static void sh309_afe_params_to_words(const AFE_Parameters_RS485_Typedef *params, u16 *words)
{
    u16 i;
    for (i = 0u; i < SH309_AFE_PARAM_REG_COUNT; ++i) {
        const AFE_Value_Typedef *field = sh309_afe_param_field_const(params, i);
        words[i] = field->curValue;
    }
}

static u8 sh309_afe_words_to_params(AFE_Parameters_RS485_Typedef *params, const u16 *words)
{
    u16 i;
    if ((params == NULL) || (words == NULL)) {
        return 0u;
    }
    for (i = 0u; i < SH309_AFE_PARAM_REG_COUNT; ++i) {
        AFE_Value_Typedef *field = sh309_afe_param_field(params, i);
        if (field == NULL) {
            return 0u;
        }
        field->curValue = words[i];
    }
    return 1u;
}

static u8 sh309_find_scaled_code(u16 value, u16 scale, const u16 *table, u16 count, u8 *code)
{
    u16 i;
    u32 target = (u32)value * (u32)scale;
    for (i = 0u; i < count; ++i) {
        if ((u32)table[i] == target) {
            if (code != NULL) {
                *code = (u8)i;
            }
            return 1u;
        }
    }
    return 0u;
}

static u8 sh309_find_ocp_current_code(u16 current_0p1a, const u16 *mv_table, u16 count, u8 *code)
{
    u16 i;
    if (g_u32CS_Res_AFE == 0u) {
        return 0u;
    }
    for (i = 0u; i < count; ++i) {
        if (((u32)mv_table[i] * g_u32CS_Res_AFE) == ((u32)current_0p1a * 100u)) {
            if (code != NULL) {
                *code = (u8)i;
            }
            return 1u;
        }
    }
    return 0u;
}

static u8 sh309_find_sc_current_code(u16 current_a, u8 *code)
{
    u16 i;
    if (g_u32CS_Res_AFE == 0u) {
        return 0u;
    }
    for (i = 0u; i < SH309_ARRAY_SIZE(AFE_SCV); ++i) {
        if (((u32)AFE_SCV[i] * g_u32CS_Res_AFE) == ((u32)current_a * 1000u)) {
            if (code != NULL) {
                *code = (u8)i;
            }
            return 1u;
        }
    }
    return 0u;
}

static u8 sh309_temp_value_valid(u16 value, u16 min_value, u16 max_value)
{
    return ((value >= min_value) && (value <= max_value) && ((value % 10u) == 0u)) ? 1u : 0u;
}

u8 SH367309_AfeParamValidate(const AFE_Parameters_RS485_Typedef *p)
{
    if (p == NULL) {
        return 0u;
    }
    if ((p->u16VcellOvp.curValue < 3600u) || (p->u16VcellOvp.curValue > 4500u) || ((p->u16VcellOvp.curValue % 5u) != 0u)) return 0u;
    if ((p->u16VcellOvp_Rcv.curValue < 3300u) || (p->u16VcellOvp_Rcv.curValue > 4500u) || ((p->u16VcellOvp_Rcv.curValue % 5u) != 0u)) return 0u;
    if (p->u16VcellOvp_Rcv.curValue >= p->u16VcellOvp.curValue) return 0u;
    if (!sh309_find_scaled_code(p->u16VcellOvp_Filter.curValue, 10u, AFE_OVT_UVT, SH309_ARRAY_SIZE(AFE_OVT_UVT), NULL)) return 0u;
    if ((p->u16VcellUvp.curValue < 2000u) || (p->u16VcellUvp.curValue > 3100u) || ((p->u16VcellUvp.curValue % 20u) != 0u)) return 0u;
    if ((p->u16VcellUvp_Rcv.curValue < 2000u) || (p->u16VcellUvp_Rcv.curValue > 3600u) || ((p->u16VcellUvp_Rcv.curValue % 20u) != 0u)) return 0u;
    if (p->u16VcellUvp_Rcv.curValue <= p->u16VcellUvp.curValue) return 0u;
    if (!sh309_find_scaled_code(p->u16VcellUvp_Filter.curValue, 10u, AFE_OVT_UVT, SH309_ARRAY_SIZE(AFE_OVT_UVT), NULL)) return 0u;
    if (p->u16IchgOcp_First.curValue != p->u16IchgOcp_Second.curValue) return 0u;
    if (p->u16IchgOcp_Filter_First.curValue != p->u16IchgOcp_Filter_Second.curValue) return 0u;
    if (!sh309_find_ocp_current_code(p->u16IchgOcp_First.curValue, AFE_OCD1V_OCCV, SH309_ARRAY_SIZE(AFE_OCD1V_OCCV), NULL)) return 0u;
    if (!sh309_find_scaled_code(p->u16IchgOcp_Filter_First.curValue, 10u, AFE_OCCT_OCD2T, SH309_ARRAY_SIZE(AFE_OCCT_OCD2T), NULL)) return 0u;
    if (!sh309_find_ocp_current_code(p->u16IdsgOcp_First.curValue, AFE_OCD1V_OCCV, SH309_ARRAY_SIZE(AFE_OCD1V_OCCV), NULL)) return 0u;
    if (!sh309_find_scaled_code(p->u16IdsgOcp_Filter_First.curValue, 10u, AFE_OCD1T, SH309_ARRAY_SIZE(AFE_OCD1T), NULL)) return 0u;
    if (!sh309_find_ocp_current_code(p->u16IdsgOcp_Second.curValue, AFE_OCD2V, SH309_ARRAY_SIZE(AFE_OCD2V), NULL)) return 0u;
    if (!sh309_find_scaled_code(p->u16IdsgOcp_Filter_Second.curValue, 10u, AFE_OCCT_OCD2T, SH309_ARRAY_SIZE(AFE_OCCT_OCD2T), NULL)) return 0u;
    if (!sh309_temp_value_valid(p->u16TChgOTp.curValue, 850u, 1100u)) return 0u;
    if (!sh309_temp_value_valid(p->u16TChgOTp_Rcv.curValue, 800u, 1100u) || (p->u16TChgOTp_Rcv.curValue >= p->u16TChgOTp.curValue)) return 0u;
    if (!sh309_temp_value_valid(p->u16TchgUTp.curValue, 200u, 500u)) return 0u;
    if (!sh309_temp_value_valid(p->u16TchgUTp_Rcv.curValue, 200u, 550u) || (p->u16TchgUTp_Rcv.curValue <= p->u16TchgUTp.curValue)) return 0u;
    if (!sh309_temp_value_valid(p->u16TdischgOTp.curValue, 850u, 1200u)) return 0u;
    if (!sh309_temp_value_valid(p->u16TdischgOTp_Rcv.curValue, 800u, 1200u) || (p->u16TdischgOTp_Rcv.curValue >= p->u16TdischgOTp.curValue)) return 0u;
    if (!sh309_temp_value_valid(p->u16TdischgUTp.curValue, 0u, 500u)) return 0u;
    if (!sh309_temp_value_valid(p->u16TdischgUTp_Rcv.curValue, 0u, 550u) || (p->u16TdischgUTp_Rcv.curValue <= p->u16TdischgUTp.curValue)) return 0u;
    if (!sh309_find_sc_current_code(p->u16CBC_Cur_DSG.curValue, NULL)) return 0u;
    if (!sh309_find_scaled_code(p->u16CBC_DelayT.curValue, 1u, AFE_SCT, SH309_ARRAY_SIZE(AFE_SCT), NULL)) return 0u;
    return 1u;
}

static void sh309_normalize_charge_alias(AFE_Parameters_RS485_Typedef *p, u8 prefer_legacy_second)
{
    if (prefer_legacy_second) {
        p->u16IchgOcp_First.curValue = p->u16IchgOcp_Second.curValue;
        p->u16IchgOcp_Filter_First.curValue = p->u16IchgOcp_Filter_Second.curValue;
    } else {
        p->u16IchgOcp_Second.curValue = p->u16IchgOcp_First.curValue;
        p->u16IchgOcp_Filter_Second.curValue = p->u16IchgOcp_Filter_First.curValue;
    }
}

u8 SH367309_AfeParamLoad(void)
{
    AFE_Parameters_RS485_Typedef defaults = AFE_PARAMETERS_RS485_STRUCTION_DEFAULT;
    AFE_Parameters_RS485_Typedef candidate = defaults;
    u16 words[SH309_AFE_PARAM_REG_COUNT];
    u16 canonical[SH309_AFE_PARAM_REG_COUNT];
    u16 i;
    u8 uninitialised = 1u;
    u8 repaired_invalid = 0u;

    if (!bms_cold_kv_store_get_afe_params(words, SH309_AFE_PARAM_REG_COUNT)) {
        AFE_Parameters_RS485_Struction = defaults;
        sh309_afe_params_to_words(&defaults, canonical);
        (void)bms_cold_kv_store_set_afe_params(canonical, SH309_AFE_PARAM_REG_COUNT);
        return 0u;
    }
    for (i = 0u; i < SH309_AFE_PARAM_REG_COUNT; ++i) {
        if (words[i] != 0xFFFFu) {
            uninitialised = 0u;
            break;
        }
    }
    if (!uninitialised) {
        if (!sh309_afe_words_to_params(&candidate, words)) {
            candidate = defaults;
            repaired_invalid = 1u;
        } else {
            sh309_normalize_charge_alias(&candidate, 1u);
            if (!SH367309_AfeParamValidate(&candidate)) {
                candidate = defaults;
                repaired_invalid = 1u;
            }
        }
    }
    if (!SH367309_AfeParamValidate(&candidate)) {
        AFE_Parameters_RS485_Struction = defaults;
        return 0u;
    }
    AFE_Parameters_RS485_Struction = candidate;
    sh309_afe_params_to_words(&candidate, canonical);
    if (uninitialised || repaired_invalid || (memcmp(words, canonical, sizeof(canonical)) != 0)) {
        if (!bms_cold_kv_store_set_afe_params(canonical, SH309_AFE_PARAM_REG_COUNT)) {
            return 0u;
        }
    }
    return repaired_invalid ? 0u : 1u;
}

u8 SH367309_AfeParamReadReg(u16 reg, u16 *value)
{
    const AFE_Value_Typedef *field;
    u16 index;
    if ((value == NULL) || (reg < SH309_AFE_PARAM_REG_BASE) || (reg > SH309_AFE_PARAM_REG_END)) return 0u;
    index = (u16)(reg - SH309_AFE_PARAM_REG_BASE);
    field = sh309_afe_param_field_const(&AFE_Parameters_RS485_Struction, index);
    if (field == NULL) return 0u;
    *value = field->curValue;
    return 1u;
}

sh309_afe_param_result_t SH367309_AfeParamWriteRegs(u16 start_reg, const u16 *values, u16 count)
{
    AFE_Parameters_RS485_Typedef candidate = AFE_Parameters_RS485_Struction;
    u16 current_words[SH309_AFE_PARAM_REG_COUNT];
    u16 candidate_words[SH309_AFE_PARAM_REG_COUNT];
    u16 i;
    u16 index;
    u8 chg_current_seen = 0u;
    u8 chg_delay_seen = 0u;
    u16 chg_current = 0u;
    u16 chg_delay = 0u;

    if ((values == NULL) || (count == 0u) || (start_reg < SH309_AFE_PARAM_REG_BASE) ||
        (start_reg > SH309_AFE_PARAM_REG_END) ||
        ((u32)start_reg + (u32)count - 1u > (u32)SH309_AFE_PARAM_REG_END)) {
        return SH309_AFE_PARAM_RESULT_INVALID;
    }
    for (i = 0u; i < count; ++i) {
        AFE_Value_Typedef *field;
        index = (u16)((start_reg - SH309_AFE_PARAM_REG_BASE) + i);
        if ((index == SH309_AFE_PARAM_ICHG_OCP_FIRST) || (index == SH309_AFE_PARAM_ICHG_OCP_SECOND)) {
            if (chg_current_seen && (chg_current != values[i])) return SH309_AFE_PARAM_RESULT_INVALID;
            chg_current_seen = 1u;
            chg_current = values[i];
            continue;
        }
        if ((index == SH309_AFE_PARAM_ICHG_OCP_FILTER_FIRST) || (index == SH309_AFE_PARAM_ICHG_OCP_FILTER_SECOND)) {
            if (chg_delay_seen && (chg_delay != values[i])) return SH309_AFE_PARAM_RESULT_INVALID;
            chg_delay_seen = 1u;
            chg_delay = values[i];
            continue;
        }
        field = sh309_afe_param_field(&candidate, index);
        if (field == NULL) return SH309_AFE_PARAM_RESULT_INVALID;
        field->curValue = values[i];
    }
    if (chg_current_seen) {
        candidate.u16IchgOcp_First.curValue = chg_current;
        candidate.u16IchgOcp_Second.curValue = chg_current;
    }
    if (chg_delay_seen) {
        candidate.u16IchgOcp_Filter_First.curValue = chg_delay;
        candidate.u16IchgOcp_Filter_Second.curValue = chg_delay;
    }
    if (!SH367309_AfeParamValidate(&candidate)) return SH309_AFE_PARAM_RESULT_INVALID;
    sh309_afe_params_to_words(&AFE_Parameters_RS485_Struction, current_words);
    sh309_afe_params_to_words(&candidate, candidate_words);
    if (memcmp(current_words, candidate_words, sizeof(candidate_words)) == 0) return SH309_AFE_PARAM_RESULT_OK;
    if (!bms_cold_kv_store_set_afe_params(candidate_words, SH309_AFE_PARAM_REG_COUNT)) return SH309_AFE_PARAM_RESULT_STORE_ERROR;
    AFE_Parameters_RS485_Struction = candidate;
    AFE_PARAM_WRITE_Flag = 1;
    return SH309_AFE_PARAM_RESULT_OK;
}
         // 閺�鍓ф暩鏉╁洦绁�2閸滃苯鍘栭悽浣冪箖濞翠礁娆㈤弮韬诧拷鍌氬礋娴ｅ超s

const u8 CRC8Table[] = { // 120424-1			CRC Table
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3};

AFE_ROM_PARAMETERS_TypeDef AFE_ROM_PARAMETERS_Struction = {0};
AFE_Parameters_RS485_Typedef AFE_Parameters_RS485_Struction = AFE_PARAMETERS_RS485_STRUCTION_DEFAULT;
SH367309_REG_STORE SH367309_Reg_Store;
sh367309_ram_t ram_reg_309;
struct SH367309_Read SH367309_Read_AFE1;

// u8 CRC8cal(u8 *p, u8 Length)
// { // look-up table calculte CRC
//     u8 crc8 = 0;

//     for (; Length > 0; Length--)
//     {
//         crc8 = CRC8Table[crc8 ^ *p];
//         p++;
//     }

//     return (crc8);
// }
u8 CRC8cal(const u8 *data, u32 len)
{
    u8 crc = 0x00;
    for (u32 i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (u8 b = 0; b < 8; b++)
        {
            if (crc & 0x80)
                crc = (u8)((crc << 1) ^ 0x07);
            else
                crc = (u8)(crc << 1);
        }
    }
    return crc;
}

#define SH309_I2C_MAX_XFER_LEN      64u
#define SH309_I2C_RETRY_CNT         3u
#define SH309_AFE_PARAM_IMAGE_BYTES 25u
#define SH309_MTP_PROGRAM_DELAY_MS  40u

void Delay1ms(u8 delaycnt);
u8 MTPWriteROM(u8 WrAddr, u8 Length, const u8 *WrBuf);

static inline u8 sh309_is_crc_readable_addr(u8 addr)
{
    return ((addr <= MTP_TR) || (addr >= MTP_CONF && addr <= MTP_RSTSTAT));
}

static inline u8 sh309_skip_write_verify(u8 addr, u8 value)
{
    if (!sh309_is_crc_readable_addr(addr))
    {
        return 1;
    }

    if ((addr == MTP_CONF) && (value & BIT(1)))
    {
        return 1;
    }

    return 0;
}

static u8 sh309_i2c_read_with_crc(u8 slave_id, u8 rd_addr, u8 length, u8 *rd_buf)
{
    u8 attempt;
    u8 rx_buf[SH309_I2C_MAX_XFER_LEN + 1];
    u8 crc_buf[SH309_I2C_MAX_XFER_LEN + 4];

    if ((rd_buf == NULL) || (length == 0u) || (length > SH309_I2C_MAX_XFER_LEN))
    {
        return 0;
    }

    for (attempt = 0; attempt < SH309_I2C_RETRY_CNT; ++attempt)
    {
        i2c_read_series(((u16)rd_addr << 8) | length, 2, (unsigned char *)rx_buf, length + 1);

        crc_buf[0] = slave_id;
        crc_buf[1] = rd_addr;
        crc_buf[2] = length;
        crc_buf[3] = (u8)(slave_id | 0x01);
        memcpy(&crc_buf[4], rx_buf, length);

        if (rx_buf[length] == CRC8cal(crc_buf, (u32)length + 4u))
        {
            memcpy(rd_buf, rx_buf, length);
            return 1;
        }

        Delay1ms(1);
    }

    return 0;
}

static u8 sh309_i2c_write_with_crc(u8 slave_id, u8 wr_addr, u8 length, const u8 *wr_buf)
{
    u8 tx_buf[SH309_I2C_MAX_XFER_LEN + 1];
    u8 crc_input[SH309_I2C_MAX_XFER_LEN + 2];

    if ((wr_buf == NULL) || (length == 0u) || (length > SH309_I2C_MAX_XFER_LEN))
    {
        return 0;
    }

    crc_input[0] = slave_id;
    crc_input[1] = wr_addr;
    memcpy(&crc_input[2], wr_buf, length);

    memcpy(tx_buf, wr_buf, length);
    tx_buf[length] = CRC8cal(crc_input, (u32)length + 2u);

    i2c_write_series(wr_addr, 1, (unsigned char *)tx_buf, length + 1);
    return ((reg_i2c_status & FLD_I2C_NAK) == 0u);
}

static u8 sh309_i2c_write_byte_checked(u8 slave_id, u8 wr_addr, u8 value)
{
    u8 attempt;
    u8 read_back = 0;

    for (attempt = 0; attempt < SH309_I2C_RETRY_CNT; ++attempt)
    {
        if (!sh309_i2c_write_with_crc(slave_id, wr_addr, 1, &value))
        {
            Delay1ms(1);
            continue;
        }

        if (sh309_skip_write_verify(wr_addr, value))
        {
            return 1;
        }

        if (sh309_i2c_read_with_crc(slave_id, wr_addr, 1, &read_back) && (read_back == value))
        {
            return 1;
        }

        Delay1ms(1);
    }

    return 0;
}

void Delay1ms(u8 delaycnt)
{
    WaitMs(delaycnt);
}
u8 TwiWrite(u8 SlaveID, u16 WrAddr, u8 Length, const u8 *WrBuf)
{
    if ((WrAddr > 0xFFu) || (Length == 0u) || (WrBuf == NULL))
    {
        return 0;
    }

    return sh309_i2c_write_with_crc(SlaveID, (u8)WrAddr, Length, WrBuf);
}

int Choose_Right_Value(u16 cur_Value, const u16 *AFE_list)
{
    int i = 0;
    for (i = 0; i < 15; i++)
    {
        if (cur_Value <= AFE_list[i])
        {
            break;
        }
    }
    return i;
}

u8 MTPWrite(u8 WrAddr, u8 Length, const u8 *WrBuf)
{
    u8 i;
    Feed_IWatchDog;

    if ((Length == 0u) || (WrBuf == NULL))
    {
        return 0;
    }

    for (i = 0; i < Length; i++)
    {
        if (!sh309_i2c_write_byte_checked(AFE_ID, WrAddr, *WrBuf))
        {
            return 0;
        }

        WrAddr++;
        WrBuf++;
        Delay1ms(1);
    }

    return 1;
}
u8 TwiRead(u8 SlaveID, u16 RdAddr, u8 Length, u8 *RdBuf)
{
    if (RdAddr > 0xFFu)
    {
        return 0;
    }

    return sh309_i2c_read_with_crc(SlaveID, (u8)RdAddr, Length, RdBuf);
}

u8 MTPRead(u8 RdAddr, u8 Length, u8 *RdBuf)
{
    return TwiRead(AFE_ID, RdAddr, Length, RdBuf);
}

static u8 sh309_read_param_image(u8 *rd_buf)
{
    if (rd_buf == NULL)
    {
        return 0;
    }

    return MTPRead(0x00, SH309_AFE_PARAM_IMAGE_BYTES, rd_buf);
}

static int sh309_param_image_diff_state(const u8 *expected, u8 *current)
{
    if ((expected == NULL) || (current == NULL))
    {
        return -1;
    }

    if (!sh309_read_param_image(current))
    {
        return -1;
    }

    return (memcmp(current, expected, SH309_AFE_PARAM_IMAGE_BYTES) != 0) ? 1 : 0;
}

static u8 sh309_program_param_byte_verified(u8 wr_addr, u8 expected_value)
{
    u8 attempt;
    u8 verify_value = 0;

    for (attempt = 0; attempt < SH309_I2C_RETRY_CNT; ++attempt)
    {
        if (MTPWriteROM(wr_addr, 1, &expected_value))
        {
            Delay1ms(SH309_MTP_PROGRAM_DELAY_MS);

            if (MTPRead(wr_addr, 1, &verify_value) && (verify_value == expected_value))
            {
                return 1;
            }
        }

        Delay1ms(1);
    }

    return 0;
}

u8 MTPWriteROM(u8 WrAddr, u8 Length, const u8 *WrBuf)
{
    u8 i;

    if ((Length == 0u) || (WrBuf == NULL))
    {
        return 0;
    }

    for (i = 0; i < Length; ++i)
    {
        if (!TwiWrite(AFE_ID, WrAddr, 1, WrBuf))
        {
            return 0;
        }

        ++WrAddr;
        ++WrBuf;
    }

    return 1;
}

uint32_t g_u32CS_Res_AFE = CS_Res_Num * 1000 / CS_Res;
// g_u32CS_Res_AFE = ((u32)g_tParam.other.u16Sys_CS_Res_Num * 1000) / g_tParam.other.u16Sys_CS_Res;
static u8 Refresh_Parameters(void)
{
    int i;
    int temp;
    u8 TR = 0u;
    u8 code = 0u;
    u16 AFE_TEMPERATURE[8] = {0};

    if (!SH367309_AfeParamValidate(&AFE_Parameters_RS485_Struction)) {
        System_ERROR_UserCallback(ERROR_EEPROM_STORE);
        return 0u;
    }
    if (!MTPRead(MTP_TR, 1, &TR)) {
        System_ERROR_UserCallback(ERROR_AFE1);
        return 0u;
    }
    SH367309_Reg_Store.TR_ResRef = 680u + 5u * (TR & 0x7Fu);
    ucMTPBuffer[25] = TR & 0x7Fu;
    memcpy((u8 *)&AFE_ROM_PARAMETERS_Struction, ucMTPBuffer, 26u);
    AFE_ROM_PARAMETERS_Struction.m00H_01H.CN = SeriesNum % 16u;
    AFE_ROM_PARAMETERS_Struction.m00H_01H.CTLC = 3u;

    AFE_ROM_PARAMETERS_Struction.m02H_03H.OVH = ((AFE_Parameters_RS485_Struction.u16VcellOvp.curValue / 5u) >> 8) & 0x3u;
    AFE_ROM_PARAMETERS_Struction.m02H_03H.OVL = (AFE_Parameters_RS485_Struction.u16VcellOvp.curValue / 5u) & 0xFFu;
#ifdef FAC_TEST
    AFE_ROM_PARAMETERS_Struction.m02H_03H.OVT = 0u;
#else
    if (!sh309_find_scaled_code(AFE_Parameters_RS485_Struction.u16VcellOvp_Filter.curValue, 10u, AFE_OVT_UVT, SH309_ARRAY_SIZE(AFE_OVT_UVT), &code)) return 0u;
    AFE_ROM_PARAMETERS_Struction.m02H_03H.OVT = code;
#endif
    AFE_ROM_PARAMETERS_Struction.m04H_05H.OVRH = ((AFE_Parameters_RS485_Struction.u16VcellOvp_Rcv.curValue / 5u) >> 8) & 0x3u;
    AFE_ROM_PARAMETERS_Struction.m04H_05H.OVRL = (AFE_Parameters_RS485_Struction.u16VcellOvp_Rcv.curValue / 5u) & 0xFFu;
#ifdef FAC_TEST
    AFE_ROM_PARAMETERS_Struction.m04H_05H.UVT = 0u;
#else
    if (!sh309_find_scaled_code(AFE_Parameters_RS485_Struction.u16VcellUvp_Filter.curValue, 10u, AFE_OVT_UVT, SH309_ARRAY_SIZE(AFE_OVT_UVT), &code)) return 0u;
    AFE_ROM_PARAMETERS_Struction.m04H_05H.UVT = code;
#endif
    AFE_ROM_PARAMETERS_Struction.m06H_07H.UV = (AFE_Parameters_RS485_Struction.u16VcellUvp.curValue / 20u) & 0xFFu;
    AFE_ROM_PARAMETERS_Struction.m06H_07H.UVR = (AFE_Parameters_RS485_Struction.u16VcellUvp_Rcv.curValue / 20u) & 0xFFu;

    if (!sh309_find_ocp_current_code(AFE_Parameters_RS485_Struction.u16IdsgOcp_First.curValue, AFE_OCD1V_OCCV, SH309_ARRAY_SIZE(AFE_OCD1V_OCCV), &code)) return 0u;
    AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD1V = code;
    if (!sh309_find_scaled_code(AFE_Parameters_RS485_Struction.u16IdsgOcp_Filter_First.curValue, 10u, AFE_OCD1T, SH309_ARRAY_SIZE(AFE_OCD1T), &code)) return 0u;
    AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD1T = code;
    if (!sh309_find_ocp_current_code(AFE_Parameters_RS485_Struction.u16IdsgOcp_Second.curValue, AFE_OCD2V, SH309_ARRAY_SIZE(AFE_OCD2V), &code)) return 0u;
    AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD2V = code;
    if (!sh309_find_scaled_code(AFE_Parameters_RS485_Struction.u16IdsgOcp_Filter_Second.curValue, 10u, AFE_OCCT_OCD2T, SH309_ARRAY_SIZE(AFE_OCCT_OCD2T), &code)) return 0u;
    AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD2T = code;

    if (!sh309_find_ocp_current_code(AFE_Parameters_RS485_Struction.u16IchgOcp_First.curValue, AFE_OCD1V_OCCV, SH309_ARRAY_SIZE(AFE_OCD1V_OCCV), &code)) return 0u;
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.OCCV = code;
    if (!sh309_find_scaled_code(AFE_Parameters_RS485_Struction.u16IchgOcp_Filter_First.curValue, 10u, AFE_OCCT_OCD2T, SH309_ARRAY_SIZE(AFE_OCCT_OCD2T), &code)) return 0u;
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.OCCT = code;
    if (!sh309_find_scaled_code(AFE_Parameters_RS485_Struction.u16CBC_DelayT.curValue, 1u, AFE_SCT, SH309_ARRAY_SIZE(AFE_SCT), &code)) return 0u;
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.SCT = code;
    if (!sh309_find_sc_current_code(AFE_Parameters_RS485_Struction.u16CBC_Cur_DSG.curValue, &code)) return 0u;
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.SCV = code;

    AFE_TEMPERATURE[0] = AFE_Parameters_RS485_Struction.u16TChgOTp.curValue / 10u;
    AFE_TEMPERATURE[1] = AFE_Parameters_RS485_Struction.u16TChgOTp_Rcv.curValue / 10u;
    AFE_TEMPERATURE[2] = AFE_Parameters_RS485_Struction.u16TchgUTp.curValue / 10u;
    AFE_TEMPERATURE[3] = AFE_Parameters_RS485_Struction.u16TchgUTp_Rcv.curValue / 10u;
    AFE_TEMPERATURE[4] = AFE_Parameters_RS485_Struction.u16TdischgOTp.curValue / 10u;
    AFE_TEMPERATURE[5] = AFE_Parameters_RS485_Struction.u16TdischgOTp_Rcv.curValue / 10u;
    AFE_TEMPERATURE[6] = AFE_Parameters_RS485_Struction.u16TdischgUTp.curValue / 10u;
    AFE_TEMPERATURE[7] = AFE_Parameters_RS485_Struction.u16TdischgUTp_Rcv.curValue / 10u;

    for (i = 0; i < 8; ++i) {
        temp = iSheldTemp_10K_NTC[AFE_TEMPERATURE[i]];
        *(((u8 *)&AFE_ROM_PARAMETERS_Struction.m11H_19H) + i) =
            (u8)(((u32)temp << 9) / ((u32)SH367309_Reg_Store.TR_ResRef + (u32)temp));
    }
    return 1u;
}

/* 濮ｅ繑顐奸弫鐗堝祦閺�鐟板綁闁�燁嚢閸欐潧om閸欏倹鏆熷В鏃囩窛娑擄拷娑撳绱濋柇锝勯嚋閸欏倹鏆熼弨鐟板綁鐏忓崬鍟撻崗銉╁亝=閸濐亙閲� */
static u8 Write_Parameters(void)
{
    u8 temp[SH309_AFE_PARAM_IMAGE_BYTES] = {0};
    const u8 *P = (const u8 *)&AFE_ROM_PARAMETERS_Struction;

    if (sh309_read_param_image(temp))
    {
        for (int i = 0; i < SH309_AFE_PARAM_IMAGE_BYTES; i++)
        { // 閺堬拷閸氬簼绔存稉鐚匯娑撳秴浠涚�佃鐦�
            if (temp[i] != P[i])
            {
                if (!sh309_program_param_byte_verified((u8)i, P[i]))
                {
                    return 0;
                }
            }
        }
    }
    else
    {
        return 0;
    }

    return (sh309_param_image_diff_state(P, temp) == 0);
}

void AFE_Reset(void)
{
    u8 WrBuf[2];

    WrBuf[0] = 0xC0;
    WrBuf[1] = 0xA5;

    /*
    if(!System_ErrFlag.u8ErrFlag_Com_AFE1) {
        if(!MTPWrite(AFE_ID, 0xEA, 1, WrBuf)) {              //0xEA, 0xC0?A CRC
            MTPWrite(AFE_ID, 0xEA, 1, WrBuf);
        }
        //MTPWrite(0xEA, 1, WrBuf);
    }
    */

    // if (!System_ERROR_UserCallback(ERROR_STATUS_AFE1))
    {
        // if (!MTPWrite(0xEA, 1, WrBuf))
        // { // 0xEA, 0xC0?A CRC
        //     MTPWrite(0xEA, 1, WrBuf);
        // }
        if (!MTPWrite(0xEA, 1, WrBuf))
        {
            System_ERROR_UserCallback(ERROR_AFE1);
        }
    }
}

u8 AFE_IsReady(void)
{
    u8 TempCnt = 0, result = 0;
    u8 TempVar;

    while (1)
    {
        Feed_IWatchDog;

        TempVar = 0;
        if (MTPRead(MTP_BFLAG2, 1, &TempVar))
        { 
            if ((TempVar & 0x10) == 0x10)
            {
                break;
            }
        }

        Delay1ms(20);
        if (++TempCnt >= 50)
        {
            System_ERROR_UserCallback(ERROR_AFE1);
            result = 1;
            break;
        }
    }
    return result;
}
void SH367309_Enable_AFE_Wdt_Cadc_Drivers(void)
{
    SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 瀵拷閸氱枌ADC
    SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 0; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
    SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 0; // 閺�鍓ф暩MOS閻㈢泧FE绾兛娆㈤幒褍鍩�
    if (!MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all))
    {
        System_ERROR_UserCallback(ERROR_AFE1);
    }
}

void SH367309_UpdataAfeConfig(void)
{
    if (AFE_PARAM_WRITE_Flag)
    {
        u8 temp[SH309_AFE_PARAM_IMAGE_BYTES] = {0};
        const u8 *P = (const u8 *)&AFE_ROM_PARAMETERS_Struction;
        int diff_state;

        // load_protectParam();
        if (!Refresh_Parameters())
        {
            return;
        }
        diff_state = sh309_param_image_diff_state(P, temp);
        if (diff_state < 0)
        {
            System_ERROR_UserCallback(ERROR_AFE1);
            return;
        }

        if (diff_state != 0)
        {
            u8 update_ok = 0;

            // MCUO_AFE_VPRO = 1; // 鏉╂稑鍙嗛悜褍鍟撳Ο鈥崇础
            gpio_write(GPIO_PD7, 1);
            Delay1ms(20);
            Feed_IWatchDog;

            if (!Write_Parameters())
            {
                System_ERROR_UserCallback(ERROR_AFE1);
            }
            else
            {
                update_ok = 1;
            }

            Feed_IWatchDog;
            // MCUO_AFE_VPRO = 0; // 闁拷閸戣櫣鍎抽崘娆惸佸锟�
            gpio_write(GPIO_PD7, 0);
            Delay1ms(1);

            if (!update_ok)
            {
                return;
            }

            if (!System_ERROR_UserCallback(ERROR_STATUS_AFE1))
            {
                AFE_Reset(); // Reset IC
                Delay1ms(5);
                AFE_IsReady();
                AFE_ResetFlag = 1;
            }
            SH367309_Enable_AFE_Wdt_Cadc_Drivers();
        }
        AFE_PARAM_WRITE_Flag = 0;
    }
}
UINT16 GetEndValue(const UINT16 *ptbl, UINT16 tblsize, UINT16 dat)
{
    UINT16 i, t_linenum;
    UINT32 x1 = 0, y1 = 0, x2 = 1, y2 = 1;
    const UINT16 *p;
    UINT16 t_tmp16a, t_tmp16b;
    p = ptbl;

    t_linenum = tblsize - 1;
    for (i = 0; i < tblsize - 2; i = i + 2)
    {
        t_tmp16a = p[i];
        t_tmp16b = p[i + 2];

        if (((dat >= t_tmp16a) && (dat <= t_tmp16b)) || ((dat <= t_tmp16a) && (dat >= t_tmp16b)))
        {
            x1 = t_tmp16a;
            x2 = t_tmp16b;
            y1 = p[i + 1];
            y2 = p[i + 3];
            break;
        }
    }

    if (i >= t_linenum - 1)
    {
        t_tmp16a = p[0];
        t_tmp16b = p[tblsize - 2];

        if (t_tmp16a <= t_tmp16b)
        {
            if (dat >= t_tmp16b)
            {
                t_tmp16a = p[tblsize - 1];
            }
            else
            {
                t_tmp16a = p[1];
            }
        }
        else
        {
            if (dat >= t_tmp16a)
            {
                t_tmp16a = p[1];
            }
            else
            {
                t_tmp16a = p[tblsize - 1];
            }
        }
        return t_tmp16a;
    }
    else
    {
        INT32 t_tmp32a;
        INT32 t_tmp32b;
        UINT32 k;
        UINT32 b;
        INT32 ret;

        if (x2 < x1)
        {
            ret = x2;
            x2 = x1;
            x1 = ret;
            ret = y2;
            y2 = y1;
            y1 = ret;
        }

        if (y2 >= y1)
        {
            t_tmp32a = y1 * x2;
            t_tmp32b = y2 * x1;
            ret = dat;
            k = y2 - y1;
            ret = ret * k;
            if (t_tmp32a >= t_tmp32b)
            {
                b = t_tmp32a - t_tmp32b;
                ret = ret + b;
            }
            else
            {
                b = t_tmp32b - t_tmp32a;
                ret = ret - b;
            }
            ret = ret / (x2 - x1);
        }
        else
        {
            t_tmp32a = y1 * x2;
            t_tmp32b = y2 * x1;
            ret = dat;
            k = y1 - y2;
            ret = ret * k;
            b = t_tmp32a - t_tmp32b;
            ret = b - ret;
            ret = ret / (x2 - x1);
        }
        return (ret & 0xffff);
    }
}

UINT16 U16_SwapEndian(UINT16 target)
{
    return (((uint16_t)target & 0xFF00) >> 8) | (((uint16_t)target & 0x00FF) << 8);
}
void UpdateVoltageFromBqMaximo(void)
{
    UINT8 i;

    for (i = 0; i < SeriesNum; i++)
    {
        UINT32 u32temp;

        {
            SH367309_Read_AFE1.u16VCell[i] = ((UINT32)U16_SwapEndian(ram_reg_309.Cell[i]) * 5 >> 5); ////Vcell*5/32
        }
        u32temp = ((UINT32)SH367309_Reg_Store.TR_ResRef * U16_SwapEndian(ram_reg_309.Temp1)) / (32769 - U16_SwapEndian(ram_reg_309.Temp1));
        if (u32temp >= 65535u) { u32temp = 65535u; }
        SH367309_Read_AFE1.u16TempBat[0] = GetEndValue(iSheldTemp_10K_AFE, (UINT16)LENGTH_TBLTEMP_AFE_10K, u32temp);
        u32temp = ((UINT32)SH367309_Reg_Store.TR_ResRef * U16_SwapEndian(ram_reg_309.Temp2)) / (32769 - U16_SwapEndian(ram_reg_309.Temp2));
        if (u32temp >= 65535u) { u32temp = 65535u; }
        SH367309_Read_AFE1.u16TempBat[1] = GetEndValue(iSheldTemp_10K_AFE, (UINT16)LENGTH_TBLTEMP_AFE_10K, u32temp);
        u32temp = ((UINT32)SH367309_Reg_Store.TR_ResRef * U16_SwapEndian(ram_reg_309.Temp3)) / (32769 - U16_SwapEndian(ram_reg_309.Temp3));
        if (u32temp >= 65535u) { u32temp = 65535u; }
        SH367309_Read_AFE1.u16TempBat[2] = GetEndValue(iSheldTemp_10K_AFE, (UINT16)LENGTH_TBLTEMP_AFE_10K, u32temp);
        // 鐢垫祦瑕佷笉瑕佸姞婊ゆ尝1s闄や互4锛宒emo鏄繖鏍风殑锛岀幇鍦ㄥ厛瑙傚療涓�涓�
        // SH367309_Read_AFE1.i16Current = (UINT16)((UINT32)U16_SwapEndian(Registers_AFE1.Cadc)*200/(21470*RSENSE));		//TODO
        SH367309_Read_AFE1.u16Current = U16_SwapEndian(ram_reg_309.Cadc);
    }
}

void DataLoad_CellVolt(void)
{
    UINT8 i;

    for (i = 0; i < SeriesNum; ++i)
    {
        INT32 t_i32temp = (UINT32)SH367309_Read_AFE1.u16VCell[SeriesSelect_AFE1[SeriesNum - 1][i]];
        // if (g_tParam.CalibCoefK[VOLT_AFE1] != 1024 || g_tParam.CalibCoefB[VOLT_AFE1] != 0)
        // {
        // 	t_i32temp = ((t_i32temp * g_tParam.CalibCoefK[VOLT_AFE1]) >> 10) + g_tParam.CalibCoefB[VOLT_AFE1];
        // }
        t_i32temp = ((t_i32temp * SYSKDEFAULT) >> 10) + SYSBDEFAULT;
        t_i32temp = t_i32temp > 0 ? t_i32temp : 0;
        g_stCellInfoReport.u16VCell[i] = (UINT16)t_i32temp;
    }

    if (SeriesNum < 32)
    {
        for (i = SeriesNum; i < 32; ++i)
        {
            g_stCellInfoReport.u16VCell[i] = 61001;
        }
    }
}

void DataLoad_CellVoltMaxMinFind(void)
{
    UINT8 i;
    UINT16 t_u16VcellMaxTemp;
    UINT16 t_u16VcellMinTemp;
    UINT8 t_u8VcellMaxPosition;
    UINT8 t_u8VcellMinPosition;
    UINT32 u32VCellTotle;

    t_u16VcellMaxTemp = 0;
    t_u16VcellMinTemp = 0x7FFF;
    t_u8VcellMaxPosition = 0;
    t_u8VcellMinPosition = 0;
    u32VCellTotle = 0;

    for (i = 0; i < SeriesNum; i++)
    {
        UINT16 t_u16VcellTemp = g_stCellInfoReport.u16VCell[i];
        u32VCellTotle += g_stCellInfoReport.u16VCell[i];
        if (t_u16VcellMaxTemp < t_u16VcellTemp)
        {
            t_u16VcellMaxTemp = t_u16VcellTemp;
            t_u8VcellMaxPosition = i;
        }
        if (t_u16VcellMinTemp > t_u16VcellTemp)
        {
            t_u16VcellMinTemp = t_u16VcellTemp;
            t_u8VcellMinPosition = i;
        }
    }

    // 鍗曠墖鏈鸿鎬诲帇
    // u32VCellTotle = ((g_i32ADCResult[ADC_VBC]*g_tParam.CalibCoefK[VOLT_VBUS])>>10) + (UINT32)g_tParam.CalibCoefB[VOLT_VBUS]*1000;
    // AFE璇绘�诲帇
    // u32VCellTotle = ((g_stBq769x0_Read_AFE1.u32VBat*g_tParam.CalibCoefK[VOLT_VBUS])>>10) + (UINT32)g_tParam.CalibCoefB[VOLT_VBUS]*1000;
    // 鎵�鏈夊崟鑺傜數姹犵數鍘嬪姞璧锋潵
    u32VCellTotle = ((u32VCellTotle * SYSKDEFAULT) >> 10) + (UINT32)SYSBDEFAULT * 1000;

    g_stCellInfoReport.u16VCellTotle = (UINT16)((u32VCellTotle * 1638 >> 14) & 0xFFFF); // 闄や互10
    g_stCellInfoReport.u16VCellMax = t_u16VcellMaxTemp;                                 // max cell voltage
    g_stCellInfoReport.u16VCellMin = t_u16VcellMinTemp;                                 // min cell voltage
    g_stCellInfoReport.u16VCellDelta = t_u16VcellMaxTemp - t_u16VcellMinTemp;           // delta cell voltage
    g_stCellInfoReport.u16VCellMaxPosition = t_u8VcellMaxPosition + 1;                  // max cell voltage
    g_stCellInfoReport.u16VCellMinPosition = t_u8VcellMinPosition + 1;                  // min cell voltage
}

/*杩欎釜鏄暟鎹孩鍑虹殑闂锛屽叾娆℃槸>>杩欎釜鐨勪紭鍏堢骇鍜屽埆鐨勭鍙蜂紭鍏堢骇鐨勯棶棰�
  杩愮畻绗︿紭鍏堢骇澶贩涔卞鑷存暟鎹孩鍑虹殑闂
   (UINT16)(t_i32temp/100) 鍜�
    (UINT16)(t_i32temp)/100涓嶄竴鏍�
*/
void DataLoad_Temperature(void)
{
    UINT8 i;
    INT32 t_i32temp;
    UINT8 Select;

    Select = 2;

    for (i = 0; i < Select; i++)
    {
        t_i32temp = (INT32)SH367309_Read_AFE1.u16TempBat[i] / 10 - 40;
        t_i32temp = ((t_i32temp * SYSKDEFAULT) + SYSBDEFAULT) >> 10;
        g_stCellInfoReport.u16Temperature[i] = (UINT16)(t_i32temp * 10 + 400);
        Monitor_TempBreak(&g_stCellInfoReport.u16Temperature[i]);
    }

#if 0
	// 鐜娓╁害1
	t_i32temp = g_i32ADCResult[ADC_TEMP_EV1] / 10 - 40; // 鏀惧ぇ1000鍊嶅拰B鍊煎搴旂殑鎰忔��
	t_i32temp = ((t_i32temp * SYSKDEFAULT) + SYSBDEFAULT) >> 10;
	g_stCellInfoReport.u16Temperature[ENV_TEMP1] = (UINT16)(t_i32temp * 10 + 400);
	Monitor_TempBreak(&g_stCellInfoReport.u16Temperature[ENV_TEMP1]);

	// 鐜娓╁害2
	t_i32temp = g_i32ADCResult[ADC_TEMP_EV2] / 10 - 40; // 鏀惧ぇ1000鍊嶅拰B鍊煎搴旂殑鎰忔��
	t_i32temp = -40;
	t_i32temp = ((t_i32temp * SYSKDEFAULT) + SYSBDEFAULT) >> 10;
	g_stCellInfoReport.u16Temperature[ENV_TEMP2] = (UINT16)(t_i32temp * 10 + 400);

	// 鐜娓╁害3
	t_i32temp = -40;
	t_i32temp = ((t_i32temp * SYSKDEFAULT) + SYSBDEFAULT) >> 10;
	g_stCellInfoReport.u16Temperature[ENV_TEMP3] = (UINT16)(t_i32temp * 10 + 400);
#endif

    // MOS娓╁害涓烘暎鐑墖娓╁害
    // 鍙栦袱鑰呮渶澶у��
    // t_i32temp = g_i32ADCResult[ADC_TEMP_MOS1];
    // t_i32temp = t_i32temp / 10 - 40;
    // t_i32temp = ((t_i32temp * SYSKDEFAULT) + SYSBDEFAULT) >> 10;
    // g_stCellInfoReport.u16Temperature[MOS_TEMP1] = (UINT16)(t_i32temp * 10 + 400);
    // Monitor_TempBreak(&g_stCellInfoReport.u16Temperature[MOS_TEMP1]);
}

void DataLoad_TemperatureMaxMinFind(void)
{
    UINT8 i;
    UINT16 t_u16VcellTemp;
    UINT16 t_u16VcellMaxTemp;
    UINT16 t_u16VcellMinTemp;
    t_u16VcellMaxTemp = 0;
    t_u16VcellMinTemp = 0x7FFF;

    // 濡傛灉鏄袱涓幆澧冩俯搴︼紝鍒欐敼涓�8渚垮彲
    for (i = 0; i < 7; i++)
    { // 榛樿鍙湁涓�涓幆澧冩俯搴︼紝绾冲叆璁＄畻
        if (g_stCellInfoReport.u16Temperature[i] == 0)
        {             // 杩欐浠ｇ爜浠�涔堟剰鎬濓紝鏂簡灏变笉鍒ゆ柇鍚楋紵
            continue; // 鏈夌殑锛屽垯蹇呭畾浼氳璧嬪�硷紝瑕佷箞-29鎽勬皬搴︺��
        } // 绌虹殑锛屽垯灏辨槸榛樿鍒氫笂鐢电殑鍊�0
        t_u16VcellTemp = g_stCellInfoReport.u16Temperature[i];
        if (t_u16VcellMaxTemp < t_u16VcellTemp)
        {
            t_u16VcellMaxTemp = t_u16VcellTemp;
        }
        if (t_u16VcellMinTemp > t_u16VcellTemp)
        {
            t_u16VcellMinTemp = t_u16VcellTemp;
        }
    }

    g_stCellInfoReport.u16TempMax = t_u16VcellMaxTemp; // max temp
    g_stCellInfoReport.u16TempMin = t_u16VcellMinTemp; // min temp
}

#ifdef __TEST_SOC__
static uint8_t step = 0;
#if 0
static uint16_t CHG_current = 1000;
static uint16_t DSG_current = 1000;
#else
static uint16_t CHG_current = 0;
static uint16_t DSG_current = 0;
#endif

void test_Autocurrent_cycle(void)
{

    switch (step)
    {
    case 0:
        if (g_stCellInfoReport.SocElement.u16Soc < 99)
        {
            step = 1;
            sys_time.CHG = CapacityFactory * 5;
            sys_time.DSG = 0;
            g_stCellInfoReport.u16Ichg = sys_time.CHG;
            g_stCellInfoReport.u16IDischg = 0;
        }
        else
        {
            step = 1;
        }
        break;
    case 1:
    {
        if (g_stCellInfoReport.SocElement.u16Soc >= 99)
        {
            step = 2;
            sys_time.CHG = 0;
            sys_time.DSG = CapacityFactory * 5;
            g_stCellInfoReport.u16Ichg = 0;
            g_stCellInfoReport.u16IDischg = sys_time.DSG;
        }
        break;
    }
    case 2:
        if (g_stCellInfoReport.SocElement.u16Soc <= 1)
        {
            step = 0;
        }
        break;
    default:
        break;
    }
}
#endif /* __TEST_SOC__ */
void DataLoad_Current(void)
{
    // if ((SH367309_Read_AFE1.u16Current & 0x1000) == 0)
    if ((SH367309_Read_AFE1.u16Current & 0x8000) == 0)
    {
        // u32_ChgCur_mA = (UINT32)SH367309_Read_AFE1.u16Current * 1000 * g_u32CS_Res_AFE / gu32_CurCoefficient; // 榛樿浣跨敤200mV鐨勮绠楁柟寮�
        u32_ChgCur_mA = (UINT32)SH367309_Read_AFE1.u16Current * 200 * g_u32CS_Res_AFE / (21470);
        // t_i32temp = (UINT32)(0xFFFF - SH367309_Read_AFE1.u16Current + 1) * g_u32CS_Res_AFE / (21470) * 200; // mA

        log_i("******************************************\n");
        log_i("AFE value->%d\n", u32_ChgCur_mA);

        u32_DsgCur_mA = 0;
    }
    else
    {
        // u32_DsgCur_mA = (UINT32)(0xFFFF - (SH367309_Read_AFE1.u16Current | 0xE000) + 1) * 1000 * g_u32CS_Res_AFE / gu32_CurCoefficient; // mA
        // u32_DsgCur_mA = (UINT32)(0xFFFF - SH367309_Read_AFE1.u16Current + 1) * 200 * g_u32CS_Res_AFE / (21470); // mA
        u32_DsgCur_mA = (UINT32)(0xFFFF - SH367309_Read_AFE1.u16Current + 1) * g_u32CS_Res_AFE / (21470) * 200; // mA

        log_i("******************************************\n");
        log_i("AFE value->%d\n", u32_DsgCur_mA);

        u32_ChgCur_mA = 0;
    }
    // DataLoad_CurrentCali();
    if (u32_DsgCur_mA > 2000)
    {
        u32_DsgCur_mA = ((u32_DsgCur_mA * SYSKDEFAULT)) + (INT32)SYSBDEFAULT * 1000; // B鍊兼槸鍩轰簬A涓哄崟浣嶈绠楀嚭鏉ョ殑
    }
    else
    {
        u32_DsgCur_mA = ((u32_DsgCur_mA * 1024));
    }

    if (u32_ChgCur_mA > 2000)
    {
        u32_ChgCur_mA = ((u32_ChgCur_mA * SYSKDEFAULT)) + (INT32)SYSBDEFAULT * 1000;
    }
    else
    {
        u32_ChgCur_mA = ((u32_ChgCur_mA * 1024));
    }

    // 鏀逛负INT32
    u32_ChgCur_mA = u32_ChgCur_mA > 0 ? u32_ChgCur_mA : 0;
    u32_DsgCur_mA = u32_DsgCur_mA > 0 ? u32_DsgCur_mA : 0;

#if (FD_BMS_TYPE == C11_AND_C11pro)
    g_stCellInfoReport.u16Ichg = (UINT16)((u32_ChgCur_mA >> 10) / 100);
    g_stCellInfoReport.u16IDischg = (UINT16)((u32_DsgCur_mA >> 10) / 10 / 12);
#else
    g_stCellInfoReport.u16Ichg = (UINT16)((u32_ChgCur_mA >> 10) / 100);
    g_stCellInfoReport.u16IDischg = (UINT16)((u32_DsgCur_mA >> 10) / 100);
#endif

    // g_stCellInfoReport.u16Ichg = 0;
    // g_stCellInfoReport.u16IDischg = 5 * CapacityFactory;
    if (g_stCellInfoReport.u16Ichg <= 2)
    {
        g_stCellInfoReport.u16Ichg = 0;
    }
    if (g_stCellInfoReport.u16IDischg <= 2)
    {
        g_stCellInfoReport.u16IDischg = 0;
    }
    
#ifdef __VIRTURE_CURRENT__
    if (sys_time.isdebugenable == 1)
    {
        g_stCellInfoReport.u16Ichg = sys_time.CHG;
        g_stCellInfoReport.u16IDischg = sys_time.DSG;
    }
#endif
}

void FaultWarnRecord2(enum FaultFlag num)
{
    if (num >= 1 && num <= 13)
    {
        if (FaultPoint_First2 >= Record_len)
        {
            FaultPoint_First2 = 0;
        }
        Fault_record_First2[FaultPoint_First2++] = num;
    }
    else if (num >= 14 && num <= 26)
    {
        if (FaultPoint_Second2 >= Record_len)
        {
            FaultPoint_Second2 = 0;
        }
        Fault_record_Second2[FaultPoint_Second2++] = num;
    }
    else
    {
        if (FaultPoint_Third2 >= Record_len)
        {
            FaultPoint_Third2 = 0;
        }
        Fault_record_Third2[FaultPoint_Third2++] = num;
    }
}

void Fault_ChangeToMCU(void)
{
    static UINT8 su8_CellOvp_Flag = 0;
    static UINT8 su8_CellUvp_Flag = 0;
    static UINT8 su8_IdischgOcp1_Flag = 0;
    static UINT8 su8_IchgOcp_Flag = 0;
    static UINT8 su8_CellChgUtp_Flag = 0;
    static UINT8 su8_CellChgOtp_Flag = 0;
    static UINT8 su8_CellDsgUtp_Flag = 0;
    static UINT8 su8_CellDsgOtp_Flag = 0;

    g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp = ram_reg_309.REG_BSTATUS1.bits.OV;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp = ram_reg_309.REG_BSTATUS1.bits.UV;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp = ram_reg_309.REG_BSTATUS1.bits.OCD1 || ram_reg_309.REG_BSTATUS1.bits.OCD2;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp = ram_reg_309.REG_BSTATUS1.bits.OCC;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp = ram_reg_309.REG_BSTATUS2.bits.UTC;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp = ram_reg_309.REG_BSTATUS2.bits.OTC;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp = ram_reg_309.REG_BSTATUS2.bits.UTD;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp = ram_reg_309.REG_BSTATUS2.bits.OTD;
    if (ram_reg_309.REG_BSTATUS1.bits.SC)
        System_ErrFlag.u8ErrFlag_CBC_DSG = 1;
    else
        System_ErrFlag.u8ErrFlag_CBC_DSG = 0;

    switch (su8_CellOvp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS1.bits.OV)
        {
            FaultWarnRecord2(CellOvp_Third);
            su8_CellOvp_Flag = 1;
        }
        break;
    case 1:
        if (!ram_reg_309.REG_BSTATUS1.bits.OV)
        {
            su8_CellOvp_Flag = 0;
        }
        break;
    default:
        break;
    }
    switch (su8_CellUvp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS1.bits.UV)
        {
            FaultWarnRecord2(CellUvp_Third);
            su8_CellUvp_Flag = 1;
        }
        break;
    case 1:
        if (!ram_reg_309.REG_BSTATUS1.bits.UV)
        {
            su8_CellUvp_Flag = 0;
        }
        break;
    default:
        break;
    }

#if 1
    switch (su8_IdischgOcp1_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS1.bits.OCD1)
        {
            // FaultWarnRecord2(IdischgOcp_Second);
            FaultWarnRecord2(IdischgOcp_Third);
            su8_IdischgOcp1_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS1.bits.OCD1)
        {
            su8_IdischgOcp1_Flag = 0;
        }
        break;

    default:
        break;
    }
#endif

    // switch (su8_IdischgOcp2_Flag)
    // {
    // case 0:
    // 	if (g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp)
    // 	{
    // 		FaultWarnRecord2(IdischgOcp_Third);
    // 		su8_IdischgOcp2_Flag = 1;
    // 	}
    // 	break;
    // case 1:
    // 	if (!g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp)
    // 	{
    // 		su8_IdischgOcp2_Flag = 0;
    // 	}
    // 	break;
    // default:
    // 	break;
    // }
#if 1
    switch (su8_IchgOcp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS1.bits.OCC)
        {
            FaultWarnRecord2(IchgOcp_Third);
            su8_IchgOcp_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS1.bits.OCC)
        {
            su8_IchgOcp_Flag = 0;
        }
        break;

    default:
        break;
    }

#else
    // switch (su8_IchgOcp_Flag)
    // {
    // case 0:
    // 	if (g_stCellInfoReport.unMdlFault_Second.bits.b1IchgOcp)
    // 	{
    // 		FaultWarnRecord2(IchgOcp_Second);
    // 		su8_IchgOcp_Flag = 1;
    // 	}
    // 	break;

    // case 1:
    // 	if (!g_stCellInfoReport.unMdlFault_Second.bits.b1IchgOcp)
    // 	{
    // 		su8_IchgOcp_Flag = 0;
    // 	}
    // 	break;

    // default:
    // 	break;
    // }
#endif

    switch (su8_CellChgUtp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS2.bits.UTC)
        {
            FaultWarnRecord2(CellChgUTp_Third);
            su8_CellChgUtp_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS2.bits.UTC)
        {
            su8_CellChgUtp_Flag = 0;
        }
        break;

    default:
        break;
    }

    switch (su8_CellChgOtp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS2.bits.OTC)
        {
            FaultWarnRecord2(CellChgOTp_Third);
            su8_CellChgOtp_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS2.bits.OTC)
        {
            su8_CellChgOtp_Flag = 0;
        }
        break;

    default:
        break;
    }

    switch (su8_CellDsgUtp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS2.bits.UTD)
        {
            FaultWarnRecord2(CellDsgUTp_Third);
            su8_CellDsgUtp_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS2.bits.UTD)
        {
            su8_CellDsgUtp_Flag = 0;
        }
        break;

    default:
        break;
    }

    switch (su8_CellDsgOtp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS2.bits.OTD)
        {
            FaultWarnRecord2(CellDsgOTp_Third);
            su8_CellDsgOtp_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS2.bits.OTD)
        {
            su8_CellDsgOtp_Flag = 0;
        }
        break;

    default:
        break;
    }
}


void AFE_Sleep(void)
{
    SH367309_Reg_Store.REG_MTP_CONF.bits.SLEEP = 1;
    if (!MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all))
    {
        System_ERROR_UserCallback(ERROR_AFE1);
    }
}

u32 System_ERROR_UserCallback(enum SYSTEM_ERROR_COMMAND errorCode)
{
    u32 result = 0;

    switch (errorCode)
    {
    case ERROR_AFE1:
        System_ErrFlag.u8ErrFlag_Com_AFE1++;
        break;
    case ERROR_AFE2:
        System_ErrFlag.u8ErrFlag_Com_AFE2++;
        break;
    case ERROR_CAN:
        System_ErrFlag.u8ErrFlag_Com_Can++;
        break;
    case ERROR_EEPROM_COM:
        System_ErrFlag.u8ErrFlag_Com_EEPROM++;
        break;
    case ERROR_SPI:
        System_ErrFlag.u8ErrFlag_Com_SPI++;
        break;
    case ERROR_UPPER:
        System_ErrFlag.u8ErrFlag_Com_Upper++;
        break;
    case ERROR_CLIENT:
        System_ErrFlag.u8ErrFlag_Com_Client++;
        break;
    case ERROR_SCREEN:
        System_ErrFlag.u8ErrFlag_Com_Screen++;
        break;
    case ERROR_WIFI:
        System_ErrFlag.u8ErrFlag_Com_Wifi++;
        break;
    case ERROR_BLUETOOTH:
        System_ErrFlag.u8ErrFlag_Com_BlueTooth++;
        break;
    case ERROR_APP:
        System_ErrFlag.u8ErrFlag_Com_App++;
        break;
    case ERROR_CBC_CHG:
        System_ErrFlag.u8ErrFlag_CBC_CHG++;
        break;
    case ERROR_CBC_DSG:
        System_ErrFlag.u8ErrFlag_CBC_DSG++;
        break;
    case ERROR_EEPROM_STORE:
        System_ErrFlag.u8ErrFlag_Store_EEPROM++;
        break;
    case ERROR_HSE:
        System_ErrFlag.u8ErrFlag_HSE++;
        break;
    case ERROR_LSE:
        System_ErrFlag.u8ErrFlag_LSE++;
        break;
    case ERROR_VDEATLE_OVER:
        System_ErrFlag.u8ErrFlag_Vdelta_OVER++;
        break;
    case ERROR_BALANCED:
        System_ErrFlag.u8ErrFlag_Balanced++;
        break;
    case ERROR_ADC:
        System_ErrFlag.u8ErrFlag_ADC++;
        break;
    case ERROR_SOC_CAIL:
        System_ErrFlag.u8ErrFlag_SOC_Cail++;
        break;
    case ERROR_HEAT:
        System_ErrFlag.u8ErrFlag_Heat++;
        break;
    case ERROR_COOL:
        System_ErrFlag.u8ErrFlag_Cool++;
        break;
    case ERROR_TEMP_BREAK:
        System_ErrFlag.u8ErrFlag_TempBreak = 1;
        break;

    case ERROR_REMOVE_AFE1:
        System_ErrFlag.u8ErrFlag_Com_AFE1 = 0;
        break;
    case ERROR_REMOVE_AFE2:
        System_ErrFlag.u8ErrFlag_Com_AFE2 = 0;
        break;
    case ERROR_REMOVE_CAN:
        System_ErrFlag.u8ErrFlag_Com_Can = 0;
        break;
    case ERROR_REMOVE_EEPROM_COM:
        System_ErrFlag.u8ErrFlag_Com_EEPROM = 0;
        break;
    case ERROR_REMOVE_SPI:
        System_ErrFlag.u8ErrFlag_Com_SPI = 0;
        break;
    case ERROR_REMOVE_UPPER:
        System_ErrFlag.u8ErrFlag_Com_Upper = 0;
        break;
    case ERROR_REMOVE_CLIENT:
        System_ErrFlag.u8ErrFlag_Com_Client = 0;
        break;
    case ERROR_REMOVE_SCREEN:
        System_ErrFlag.u8ErrFlag_Com_Screen = 0;
        break;
    case ERROR_REMOVE_WIFI:
        System_ErrFlag.u8ErrFlag_Com_Wifi = 0;
        break;
    case ERROR_REMOVE_BLUETOOTH:
        System_ErrFlag.u8ErrFlag_Com_BlueTooth = 0;
        break;
    case ERROR_REMOVE_APP:
        System_ErrFlag.u8ErrFlag_Com_App = 0;
        break;
    case ERROR_REMOVE_CBC_CHG:
        System_ErrFlag.u8ErrFlag_CBC_CHG = 0;
        break;
    case ERROR_REMOVE_CBC_DSG:
        System_ErrFlag.u8ErrFlag_CBC_DSG = 0;
        break;
    case ERROR_REMOVE_EEPROM_STORE:
        System_ErrFlag.u8ErrFlag_Store_EEPROM = 0;
        break;
    case ERROR_REMOVE_HSE:
        System_ErrFlag.u8ErrFlag_HSE = 0;
        break;
    case ERROR_REMOVE_LSE:
        System_ErrFlag.u8ErrFlag_LSE = 0;
        break;
    case ERROR_REMOVE_VDEATLE_OVER:
        System_ErrFlag.u8ErrFlag_Vdelta_OVER = 0;
        break;
    case ERROR_REMOVE_BALANCED:
        System_ErrFlag.u8ErrFlag_Balanced = 0;
        break;
    case ERROR_REMOVE_ADC:
        System_ErrFlag.u8ErrFlag_ADC = 0;
        break;
    case ERROR_REMOVE_HEAT:
        System_ErrFlag.u8ErrFlag_Heat = 0;
        break;
    case ERROR_REMOVE_COOL:
        System_ErrFlag.u8ErrFlag_Cool = 0;
        break;
    case ERROR_REMOVE_SOC_CAIL:
        System_ErrFlag.u8ErrFlag_SOC_Cail = 0;
        break;
    case ERROR_REMOVE_TEMP_BREAK:
        System_ErrFlag.u8ErrFlag_TempBreak = 0;
        break;

    case ERROR_STATUS_AFE1:
        result = System_ErrFlag.u8ErrFlag_Com_AFE1;
        break;
    case ERROR_STATUS_AFE2:
        result = System_ErrFlag.u8ErrFlag_Com_AFE2;
        break;
    case ERROR_STATUS_CAN:
        result = System_ErrFlag.u8ErrFlag_Com_Can;
        break;
    case ERROR_STATUS_EEPROM_COM:
        result = System_ErrFlag.u8ErrFlag_Com_EEPROM;
        break;
    case ERROR_STATUS_SPI:
        result = System_ErrFlag.u8ErrFlag_Com_SPI;
        break;
    case ERROR_STATUS_UPPER:
        result = System_ErrFlag.u8ErrFlag_Com_Upper;
        break;
    case ERROR_STATUS_CLIENT:
        result = System_ErrFlag.u8ErrFlag_Com_Client;
        break;
    case ERROR_STATUS_SCREEN:
        result = System_ErrFlag.u8ErrFlag_Com_Screen;
        break;
    case ERROR_STATUS_WIFI:
        result = System_ErrFlag.u8ErrFlag_Com_Wifi;
        break;
    case ERROR_STATUS_BLUETOOTH:
        result = System_ErrFlag.u8ErrFlag_Com_BlueTooth;
        break;
    case ERROR_STATUS_APP:
        result = System_ErrFlag.u8ErrFlag_Com_App;
        break;
    case ERROR_STATUS_CBC_CHG:
        result = System_ErrFlag.u8ErrFlag_CBC_CHG;
        break;
    case ERROR_STATUS_CBC_DSG:
        result = System_ErrFlag.u8ErrFlag_CBC_DSG;
        break;
    case ERROR_STATUS_EEPROM_STORE:
        result = System_ErrFlag.u8ErrFlag_Store_EEPROM;
        break;
    case ERROR_STATUS_HSE:
        result = System_ErrFlag.u8ErrFlag_HSE;
        break;
    case ERROR_STATUS_LSE:
        result = System_ErrFlag.u8ErrFlag_LSE;
        break;
    case ERROR_STATUS_VDEATLE_OVER:
        result = System_ErrFlag.u8ErrFlag_Vdelta_OVER;
        break;
    case ERROR_STATUS_BALANCED:
        result = System_ErrFlag.u8ErrFlag_Balanced;
        break;
    case ERROR_STATUS_ADC:
        result = System_ErrFlag.u8ErrFlag_ADC;
        break;
    case ERROR_STATUS_SOC_CAIL:
        result = System_ErrFlag.u8ErrFlag_SOC_Cail;
        break;
    case ERROR_STATUS_HEAT:
        result = System_ErrFlag.u8ErrFlag_Heat;
        break;
    case ERROR_STATUS_COOL:
        result = System_ErrFlag.u8ErrFlag_Cool;
        break;
    case ERROR_STATUS_TEMP_BREAK:
        result = System_ErrFlag.u8ErrFlag_TempBreak;
        break;

    default:
        break;
    }

    return result;
}
//todo 参数回读校验，crc，
void App_AFEGet(void)
{
    static uint16_t cnt = 0;
    sh367309_ram_t ram_snapshot;

    if (sh309_i2c_read_with_crc(AFE_ID, SH309_RAM_START_ADDR, SH309_RAM_LEN, (u8 *)&ram_snapshot))
    {
        memcpy(&ram_reg_309, &ram_snapshot, sizeof(ram_reg_309));
        cnt = 0;
        UpdateVoltageFromBqMaximo();

        DataLoad_CellVolt();
        DataLoad_CellVoltMaxMinFind();
        DataLoad_Temperature();
        DataLoad_TemperatureMaxMinFind();
    #ifndef __TEST_SOC__
        DataLoad_Current();
    #else
        test_Autocurrent_cycle();
    #endif // !__TEST_SOC__

        SystemStatus.bits.b1Status_MOS_CHG = ram_reg_309.REG_BSTATUS3.bits.CHG_FET;
        SystemStatus.bits.b1Status_MOS_DSG = ram_reg_309.REG_BSTATUS3.bits.DSG_FET;
        Fault_ChangeToMCU();
        System_ErrFlag.u8ErrFlag_Com_AFE1 = 0;
    }
    else
    {
        DataLoad_ClearCurrent();
        if(++cnt >= (10))
        {
            System_ErrFlag.u8ErrFlag_Com_AFE1 = 1;
            DataLoad_ClearAfeReportPreserveSoc();
            DataLoad_ClearCurrent();
        }
    }
}
