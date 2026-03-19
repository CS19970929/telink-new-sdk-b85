#include "SocEnhance.h"
// #include "DataDeal.h"
// #include "EEPROM.h"
// #include "Sci_Upper.h"
// #include "main.h"
#include "conf.h"
#include "Sci_Upper.h"
#include "soc_kv_store.h"

extern struct stCell_Info g_stCellInfoReport;

// Forward declarations
extern UINT16 GetEndValue(const UINT16 *ptbl, UINT16 tblsize, UINT16 dat);
uint8_t get_soc_real(void);
uint8_t get_dispsoc(void);
void SOC_Result_Pass(void);

UINT32 ModulusSub(uint32_t Data1, uint32_t Data2)
{
	return (UINT32)(Data1 > Data2 ? Data1 - Data2 : Data2 - Data1);
}

uint8_t bms_soh_from_cycle(uint16_t cycle)
{
    // 0~80 次：SOH 不变
    if (cycle <= 80) {
        return 100;
    }

    // >=800 次：夹紧到 80%
    if (cycle >= 800) {
        return 80;
    }

    // 81~500 次：100 -> 90
    if (cycle <= 500) {
        // cycle=81 => 100
        // cycle=500 => 90
        uint16_t x = (uint16_t)(cycle - 80);      // 1..420
        // drop = x * 10 / 420
        uint16_t drop = (uint16_t)((uint32_t)x * 10u / 420u);
        return (uint8_t)(100u - drop);
    }

    // 501~799 次：90 -> 80
    {
        uint16_t x = (uint16_t)(cycle - 500);     // 1..299
        // drop = x * 10 / 300
        uint16_t drop = (uint16_t)((uint32_t)x * 10u / 300u);
        return (uint8_t)(90u - drop);
    }
}

#if 1

#define SOC_FAC_VALUE 60
#define E2P_ADDR_SOC_RECORD_backup (1024 * 3)
#define E2P_ADDR_SOC_RECORD (E2P_ADDR_SOC_RECORD_backup - 2)

#if 1
// #define SOC_100_VAL g_tParam.other.u16Soc_V_100
// #define SOC_0_VAL g_tParam.other.u16Soc_V_0
#define SOC_100_VAL (4180)
#define SOC_0_VAL (3100)

#define VCELLMAX g_stCellInfoReport.u16VCellMax
#define VCELLMIN g_stCellInfoReport.u16VCellMin

#define ICHG g_stCellInfoReport.u16Ichg
#define IDSG g_stCellInfoReport.u16IDischg

// #define COV_VAL g_tParam.protect.u16VcellOvp_Third
// #define CUV_VAL g_tParam.protect.u16VcellUvp_Third
// #define BOV_VAL g_tParam.protect.u16VbusOvp_Third
// #define BUV_VAL g_tParam.protect.u16VbusUvp_Third

#define COV_VAL (4200)
#define CUV_VAL (3000)
#define BOV_VAL (4200 * SNum)
#define BUV_VAL (2900 * SNum)

#else
#define SOC_100_VAL g_tParam.other.u16Soc_V_100
#define SOC_0_VAL g_tParam.other.u16Soc_V_0

#define VCELLMAX g_stCellInfoReport.u16VCellMax
#define VCELLMIN g_stCellInfoReport.u16VCellMin

#define ICHG g_stCellInfoReport.u16Ichg
#define IDSG g_stCellInfoReport.u16IDischg

#define COV_VAL PRT_E2ROMParas.u16VcellOvp_Third
#define CUV_VAL PRT_E2ROMParas.u16VcellUvp_Third
#define BOV_VAL PRT_E2ROMParas.u16VbusOvp_Third
#define BUV_VAL PRT_E2ROMParas.u16VbusUvp_Third
#endif

#define SOC_VIRTUAL_CURRENT_CHG (uint16_t)2 // A*10��1��2����Ϊ��0����=�ţ�0.2�Ϳ�ʼ����
#define SOC_VIRTUAL_CURRENT_DSG (uint16_t)2 // A*10��1��2����Ϊ��0���������Ϊ0��ͬʱ����=���ж���ȥ����Ȼ�ͻῨ��DSG��������������

#define _CAL_SLOW_DOWN_CHG

// ==================== OCV-SOC Table (Ternary Li 25C) ====================
// Format: {voltage_mV, SOC%, ...} for GetEndValue interpolation
// 21 pairs = 42 elements
#define SOC_TABLE_TERNARY_SIZE 42
static const UINT16 OCV_SOC_Table_TernaryLi[SOC_TABLE_TERNARY_SIZE] = {
	3000,    0,
	3200,    5,
	3350,   10,
	3420,   15,
	3480,   20,
	3530,   25,
	3570,   30,
	3610,   35,
	3650,   40,
	3690,   45,
	3730,   50,
	3770,   55,
	3810,   60,
	3860,   65,
	3910,   70,
	3970,   75,
	4030,   80,
	4080,   85,
	4120,   90,
	4160,   95,
	4200,  100,
};

// ==================== Temp-Capacity Factor Table (Ternary Li) ====================
// Format: {(T+40)*10, factor_permil}
#define TEMP_CAP_TABLE_SIZE 18
static const UINT16 Temp_Capacity_Table[TEMP_CAP_TABLE_SIZE] = {
	200,    600,   // -20C -> 60%
	250,    660,   // -15C
	300,    720,   // -10C
	350,    780,   //  -5C
	400,    840,   //   0C
	420,    870,   //   2C
	450,    910,   //   5C
	500,    950,   //  10C
	550,    980,   //  15C
	600,   1000,   //  20C
	650,   1000,   //  25C
	700,   1010,   //  30C
	750,   1020,   //  35C
	800,   1020,   //  40C
	850,   1010,   //  45C
	900,   1000,   //  50C
	950,    980,   //  55C
	1000,   960,   //  60C
};

// ==================== Temp-Voltage Correction Tables ====================
#define TEMP_V100_TABLE_SIZE 10
static const UINT16 Temp_V100_Table[TEMP_V100_TABLE_SIZE] = {
	200,   4000,   // -20C
	300,   4100,   // -10C
	400,   4150,   //   0C
	500,   4170,   //  10C
	600,   4180,   //  20C
	650,   4180,   //  25C
	750,   4180,   //  35C
	850,   4180,   //  45C
	950,   4180,   //  55C
	1000,  4180,   //  60C
};

#define TEMP_V0_TABLE_SIZE 10
static const UINT16 Temp_V0_Table[TEMP_V0_TABLE_SIZE] = {
	200,   3300,   // -20C
	300,   3200,   // -10C
	400,   3150,   //   0C
	500,   3120,   //  10C
	600,   3100,   //  20C
	650,   3100,   //  25C
	750,   3100,   //  35C
	850,   3100,   //  45C
	950,   3100,   //  55C
	1000,  3100,   //  60C
};

// ==================== Rest Detection ====================
#define REST_CURRENT_THRESHOLD   5
#define REST_PREPARE_CNT_SMALL   150
#define REST_STABLE_CNT          1500
#define REST_PREPARE_CNT_LARGE   9000
#define LARGE_CURRENT_THRESHOLD  50
#define OCV_CALI_DOWN_THRESHOLD  5
#define OCV_CALI_UP_THRESHOLD    3
#define OCV_CALI_UP_SOC_LIMIT    50

enum REST_STATE { REST_IDLE = 0, REST_PREPARE, REST_READY };

static struct SOC_REST_DETECT {
	enum REST_STATE state;
	uint16_t prepare_cnt;
	uint16_t stable_cnt;
	uint8_t  large_curr_flag;
} s_rest = {REST_IDLE, 0, 0, 0};

static uint16_t soc_temp_get_battery_temp_raw(void)
{
	return g_stCellInfoReport.u16Temperature[8];
}
static uint16_t soc_get_temp_capacity_factor(uint16_t temp_raw)
{
	return GetEndValue(Temp_Capacity_Table, TEMP_CAP_TABLE_SIZE, temp_raw);
}
static uint16_t soc_get_temp_v100(uint16_t temp_raw)
{
	return GetEndValue(Temp_V100_Table, TEMP_V100_TABLE_SIZE, temp_raw);
}
static uint16_t soc_get_temp_v0(uint16_t temp_raw)
{
	return GetEndValue(Temp_V0_Table, TEMP_V0_TABLE_SIZE, temp_raw);
}
static uint8_t soc_ocv_lookup(uint16_t vcell_mv)
{
	if (vcell_mv <= 3000) return 0;
	if (vcell_mv >= 4200) return 100;
	return (uint8_t)GetEndValue(OCV_SOC_Table_TernaryLi, SOC_TABLE_TERNARY_SIZE, vcell_mv);
}

static void soc_rest_detect(void)
{
	uint8_t curr_is_small = (ICHG <= REST_CURRENT_THRESHOLD && IDSG <= REST_CURRENT_THRESHOLD) ? 1 : 0;
	uint8_t curr_is_large = (ICHG >= LARGE_CURRENT_THRESHOLD || IDSG >= LARGE_CURRENT_THRESHOLD) ? 1 : 0;

	if (curr_is_large) {
		s_rest.large_curr_flag = 1;
		s_rest.state = REST_IDLE;
		s_rest.prepare_cnt = 0;
		s_rest.stable_cnt = 0;
		return;
	}
	if (!curr_is_small) {
		s_rest.state = REST_IDLE;
		s_rest.prepare_cnt = 0;
		s_rest.stable_cnt = 0;
		return;
	}
	switch (s_rest.state) {
	case REST_IDLE:
		s_rest.prepare_cnt++;
		if (s_rest.large_curr_flag) {
			if (s_rest.prepare_cnt >= REST_PREPARE_CNT_LARGE) {
				s_rest.large_curr_flag = 0;
				s_rest.prepare_cnt = 0;
				s_rest.state = REST_PREPARE;
			}
		} else {
			if (s_rest.prepare_cnt >= REST_PREPARE_CNT_SMALL) {
				s_rest.prepare_cnt = 0;
				s_rest.state = REST_PREPARE;
			}
		}
		break;
	case REST_PREPARE:
		s_rest.stable_cnt++;
		if (s_rest.stable_cnt >= REST_STABLE_CNT) {
			s_rest.stable_cnt = 0;
			s_rest.state = REST_READY;
		}
		break;
	case REST_READY:
		break;
	default:
		s_rest.state = REST_IDLE;
		break;
	}
}

static void soc_ocv_calibration(void)
{
	if (s_rest.state != REST_READY) return;

	uint8_t soc_ocv = soc_ocv_lookup(VCELLMIN);
	uint8_t soc_now = get_soc_real();

	if (soc_ocv < soc_now) {
		if ((soc_now - soc_ocv) >= OCV_CALI_DOWN_THRESHOLD) {
			SOC_Calculate_Element.u8SOC_Now = soc_ocv;
			SOC_Calculate_Element.u32CapNow = (UINT32)soc_ocv * SOC_Calculate_Element.u32CapFull / 100;
			SOC_Calculate_Element.u32CapChange = 0;
		}
	} else if (soc_ocv > soc_now) {
		if (soc_now < OCV_CALI_UP_SOC_LIMIT && (soc_ocv - soc_now) >= OCV_CALI_UP_THRESHOLD) {
			SOC_Calculate_Element.u8SOC_Now = soc_ocv;
			SOC_Calculate_Element.u32CapNow = (UINT32)soc_ocv * SOC_Calculate_Element.u32CapFull / 100;
			SOC_Calculate_Element.u32CapChange = 0;
		}
	}
	s_rest.state = REST_IDLE;
	s_rest.prepare_cnt = 0;
	s_rest.stable_cnt = 0;
}

enum SOC_CALI_STATE
{
	SOC_CALI_STATE_TRANSFER,
	SOC_CALI_CONT_CHG,
	SOC_CALI_CONT_DSG,
};

enum CAP_FULL_STATE
{
	CAP_FULL_INIT = 0,
	CAP_FULL_STARTUP,
	CAP_FULL_CALCU,
	CAP_FULL_SUCCESS,
	CAP_FULL_FAIL,
};

struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;		 // �ڲ�����ṹ��
struct SOC_CALCULATE_ELEMENT back_SOC_Calculate_Element; // �ڲ�����ṹ��

enum SOC_CALI_STATE SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER; // ��ģ����������		SOC����״̬�����ǵó�ʼ��
enum CAP_FULL_STATE CapFull_Cali_Flag = CAP_FULL_INIT;		 // �������¼���״̬����

struct SOC_PARA
{
	uint8_t soc;
	uint8_t soh;
};
struct BMS_PARAM
{
	struct SOC_PARA soc_para;
};

struct BMS_PARAM g_bms_param_default;

void set_dispsoc(uint8_t soc)
{
	g_stCellInfoReport.SocElement.u16Soc = soc;
}

uint8_t isCHG(void)
{
	return g_stCellInfoReport.u16Ichg > SOC_VIRTUAL_CURRENT_CHG ? 1 : 0;
}
uint8_t isDSG(void)
{
	return g_stCellInfoReport.u16IDischg > SOC_VIRTUAL_CURRENT_DSG ? 1 : 0;
}
uint8_t get_soc_real(void)
{
	return SOC_Calculate_Element.u8SOC_Now;
}

void set_calsoc(uint8_t _soc)
{
	SOC_Calculate_Element.u8SOC_Now = _soc;
	SOC_Calculate_Element.u32CapFactory = (UINT32)CapacityFactory * 3600;
	SOC_Calculate_Element.soh = bms_soh_from_cycle(SOC_Calculate_Element.u32Cycle_times);
	SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFactory * SOC_Calculate_Element.soh / 100;
	{
		uint16_t temp_raw = soc_temp_get_battery_temp_raw();
		uint16_t temp_factor = soc_get_temp_capacity_factor(temp_raw);
		SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFull * temp_factor / 1000;
	}
	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
}

static void Inc_real_soc(void)
{
	SOC_Calculate_Element.u8SOC_Now += 1;
	SOC_Calculate_Element.u32CapNow += SOC_Calculate_Element.u32CapFull / 100;
}
static void Dec_real_soc(void)
{
	SOC_Calculate_Element.u8SOC_Now -= 1;
	SOC_Calculate_Element.u32CapNow -= SOC_Calculate_Element.u32CapFull / 100;
}

void set_soc_param(uint8_t _soc_val, uint16_t _cap_factory, uint8_t disp_sync_updatae)
{
	{
		// soc_calculate.u32CapFactory = (UINT32)g_tParam.other.u16Soc_Ah * 3600;
		// soc_calculate.u32Cycle_times = (UINT32)g_tParam.other.u16Soc_Cycle_times * 100;
	}

	set_calsoc(_soc_val);
	// todo
	// if (disp_sync_updatae)

	{
		set_dispsoc(_soc_val);
	}
	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
}

void soc_factory_param_init_first(void)
{
#if 1
	SOC_Calculate_Element.u8SOC_Now = FAC_INIT_soc;
	SOC_Calculate_Element.u32CapFactory = (UINT32)CapacityFactory * 3600; // ???*10;???��??????????��????????��????????
	SOC_Calculate_Element.u32Cycle_times = (UINT32)1 * 100;
	SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFactory;
	SOC_Calculate_Element.u8DSG_SOC_Int = 0;

	// {
	// 	nvm_param_set(NVM_KEY_SOC, SOC_Calculate_Element.u8SOC_Now);
	// 	nvm_param_set(NVM_KEY_DSGSOC_INT, 0);
	// 	nvm_param_set(NVM_KEY_CYCLES, SOC_Calculate_Element.u32Cycle_times);
	// 	nvm_param_set(NVM_KEY_CAPACITY, SOC_Calculate_Element.u32CapFactory);
	// }

	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
	back_SOC_Calculate_Element = SOC_Calculate_Element;

#endif
}

void soc_param_lib_init(soc_kv_data_t *_soc)
{
	SOC_Calculate_Element.u8DSG_SOC_Int = _soc->dsg;
	SOC_Calculate_Element.u32Cycle_times = _soc->cycle;
	set_calsoc(_soc->soc);
	
	SOC_Calculate_Element.soh = bms_soh_from_cycle(SOC_Calculate_Element.u32Cycle_times);
	SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFactory * SOC_Calculate_Element.soh / 100;
	{
		uint16_t temp_raw = soc_temp_get_battery_temp_raw();
		uint16_t temp_factor = soc_get_temp_capacity_factor(temp_raw);
		SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFull * temp_factor / 1000;
	}
	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
	// SOC_Calculate_Element.u32CapFactory = SOC_Calculate_Element.u32CapFull;

	back_SOC_Calculate_Element = SOC_Calculate_Element;

	SOC_Result_Pass();
}

// OCV lookup: uses soc_ocv_lookup internally
uint8_t Get_OpenCircuit_Value_new(uint16_t VCell)
{
	return soc_ocv_lookup(VCell);
}

int8_t get_soc_from_openVol_onlyDec_new(uint16_t VCell)
{
	uint8_t result;
	uint8_t old_soc = get_dispsoc();

	result = Get_OpenCircuit_Value_new(VCell);
	if (result < old_soc)
		return result;

	return old_soc;
}

int8_t get_soc_from_openVol_new(uint16_t VCell)
{
	return Get_OpenCircuit_Value_new(VCell);
}

void CorrectionTerminal_CV(enum _CUR CurrentType)
{
	static uint16_t su16_SocChgCal_L1_Tcnt = 0;
	static uint16_t su16_SocChgCal_L2_Tcnt = 0;
	static uint16_t su16_SocChgCal_L3_Tcnt = 0;
	static uint16_t su16_SocChgCal_L4_Tcnt = 0;

	static uint16_t su16_SocDsgCal_L1_Tcnt = 0;
	static uint16_t su16_SocDsgCal_L2_Tcnt = 0;
	static uint16_t su16_SocDsgCal_L3_Tcnt = 0;
	static uint16_t su16_SocDsgCal_L4_Tcnt = 0;
	switch (CurrentType)
	{
	case CurCHG:
		if (VCELLMAX >= SOC_100_VAL - 100 && VCELLMAX < SOC_100_VAL && get_soc_real() < 95)
		{ // �ͷŵ������Ӧ����һ�Σ���������95%����
			if (++su16_SocChgCal_L1_Tcnt >= 10)
			{
				su16_SocChgCal_L1_Tcnt = 0;
				Inc_real_soc();
			}
		}
		else if (VCELLMAX >= SOC_100_VAL && get_soc_real() < 100)
		{
			if (get_soc_real() > 95)
			{
				if (++su16_SocChgCal_L2_Tcnt >= 8)
				{
					su16_SocChgCal_L2_Tcnt = 0;
					Inc_real_soc();
				}
			}
			else
			{
				if (++su16_SocChgCal_L3_Tcnt >= 4)
				{
					su16_SocChgCal_L3_Tcnt = 0;
					Inc_real_soc();
				}
			}
		}

		// ���ǻ��ڳ������ܴﵽ100%���ռ�������2S + 1%
		if (VCELLMAX >= SOC_100_VAL + 50 && get_soc_real() < 100)
		{
			if (++su16_SocChgCal_L4_Tcnt >= 2)
			{
				su16_SocChgCal_L4_Tcnt = 0;
				Inc_real_soc();
			}
		}

#ifdef _CAL_SLOW_DOWN_CHG
		if (get_soc_real() >= 99 && VCELLMAX < SOC_100_VAL)
		{
			// SOC_Calculate_Element.u8SOC_Now = 98;
			SOC_Calculate_Element.u8SOC_Now = get_soc_real(); // SOC���ֲ���
			SOC_Calculate_Element.u32CapChange = 0;			  // ������ۼ��������ɣ��������©���������1
			SOC_Calculate_Element.u32CapNow = (UINT32)get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
		}
#endif

		su16_SocDsgCal_L1_Tcnt = 0;
		su16_SocDsgCal_L2_Tcnt = 0;
		su16_SocDsgCal_L3_Tcnt = 0;
		su16_SocDsgCal_L4_Tcnt = 0;
		break;

	case CurDSG:
		if (VCELLMIN <= SOC_0_VAL + 100 && VCELLMIN > SOC_0_VAL && get_soc_real() > 5)
		{
			if (++su16_SocDsgCal_L1_Tcnt >= 10)
			{ // ��һ��У׼
				su16_SocDsgCal_L1_Tcnt = 0;
				Dec_real_soc();
			}
		}
		else if (VCELLMIN <= SOC_0_VAL && get_soc_real() > 0)
		{ // ��Ҳ��֪��ΪʲôҪ5%�����룬ֱ��0%�������������гɱ�ѭ��
			if (get_soc_real() < 5)
			{ // �ڶ���У׼
				if (++su16_SocDsgCal_L2_Tcnt >= 8)
				{								// ��ƴ����������һ���ĸ�������1%����10��Ϊ8�ɡ�
					su16_SocDsgCal_L2_Tcnt = 0; // ���Ǽ��С�����ܷž�һЩ�����ܸ�Ϊ6
					Dec_real_soc();
				}
			}
			else
			{ // ��û���ˣ����кܴ��SOC
				if (++su16_SocDsgCal_L3_Tcnt >= 4)
				{ // ������У׼
					su16_SocDsgCal_L3_Tcnt = 0;
					Dec_real_soc();
				}
			}
		}

		if (VCELLMIN <= SOC_0_VAL - 50 && get_soc_real() > 0)
		{
			if (++su16_SocDsgCal_L4_Tcnt >= 2)
			{
				su16_SocDsgCal_L4_Tcnt = 0;
				Dec_real_soc();
			}
		}

		if (get_soc_real() <= 1 && VCELLMIN > SOC_0_VAL)
		{
			// SOC_Calculate_Element.u8SOC_Now = 2;
			SOC_Calculate_Element.u8SOC_Now = get_soc_real(); // SOC���ֲ���
			SOC_Calculate_Element.u32CapChange = 0;			  // ������ۼ��������ɣ��������©���������1
			SOC_Calculate_Element.u32CapNow = (UINT32)get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
		}

		su16_SocChgCal_L1_Tcnt = 0;
		su16_SocChgCal_L2_Tcnt = 0;
		su16_SocChgCal_L3_Tcnt = 0;
		su16_SocChgCal_L4_Tcnt = 0;
		break;

	default:
		break;
	}
}

void Correction_Terminal(enum _CUR CurrentType)
{
	switch (CurrentType)
	{
	case CurCHG:
		CorrectionTerminal_CV(CurrentType);
		break;

	case CurDSG:
		CorrectionTerminal_CV(CurrentType);
		break;
	default:
		break;
	}
}

void SOC_Cont_AH_Int_CHG(void)
{
	UINT32 C_change_per;
	static uint8_t s_u8_CHG200msCnt = 0;
	static uint8_t s_u8_Transfer200msCnt = 0;
#if 1
	if (g_stCellInfoReport.u16Ichg >= SOC_VIRTUAL_CURRENT_CHG)
	{
		// if(g_stCellInfoReport.u16Ichg > 0) {
		if (++s_u8_CHG200msCnt >= 5)
		{
			s_u8_CHG200msCnt = 0;
			SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 1;
		}
		if (s_u8_Transfer200msCnt)
			s_u8_Transfer200msCnt = 0;
	}
	else
	{
		if (++s_u8_Transfer200msCnt >= 2)
		{ // ��ֹ˲����������
			s_u8_Transfer200msCnt = 0;
			s_u8_CHG200msCnt = 0;
			SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER;
			return;
		}
		--s_u8_CHG200msCnt;
	}
#endif

	if (SOC_Calculate_Element.u8CHG_AHCalcu_Flag)
	{
		Correction_Terminal(CurCHG);
		SOC_Calculate_Element.u8SOC_Old = get_soc_real();
		SOC_Calculate_Element.u32CapChange += (UINT32)g_stCellInfoReport.u16Ichg * 1; // As*10*100(����Ч��100)
		SOC_Calculate_Element.u32CapNow += (UINT32)g_stCellInfoReport.u16Ichg * 1;	  // ʣ������ʵʱ����

		if (SOC_Calculate_Element.u32CapNow > SOC_Calculate_Element.u32CapFull)
			SOC_Calculate_Element.u32CapNow = SOC_Calculate_Element.u32CapFull;
		C_change_per = SOC_Calculate_Element.u32CapChange * 100 / SOC_Calculate_Element.u32CapFull;
		SOC_Calculate_Element.u8SOC_Now = SOC_Calculate_Element.u8SOC_Old + C_change_per;
		if (get_soc_real() > 100)
			SOC_Calculate_Element.u8SOC_Now = 100;
		SOC_Calculate_Element.u32CapChange = (((SOC_Calculate_Element.u32CapChange * 100) % SOC_Calculate_Element.u32CapFull) + 50) / 100;
		SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 0;

		SOC_Calculate_Element.u32CapFull_Cal_As += (UINT32)g_stCellInfoReport.u16Ichg * 1;
	}
}

void SOC_Cont_AH_Int_DSG(void)
{
	UINT32 C_change_per;
	static uint8_t s_u8_DSG200msCnt = 0;
	static uint8_t s_u8_Transfer200msCnt = 0;
#if 1
	if (g_stCellInfoReport.u16IDischg >= SOC_VIRTUAL_CURRENT_DSG)
	{
		if (++s_u8_DSG200msCnt >= 5)
		{
			s_u8_DSG200msCnt = 0;
			SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 1;
		}
		if (s_u8_Transfer200msCnt)
			s_u8_Transfer200msCnt = 0;
	}
	else
	{
		if (++s_u8_Transfer200msCnt >= 2)
		{
			s_u8_Transfer200msCnt = 0;
			s_u8_DSG200msCnt = 0;
			SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER;
			return;
		}
		--s_u8_DSG200msCnt;
	}
#endif

	if (SOC_Calculate_Element.u8DSG_AHCalcu_Flag)
	{
		Correction_Terminal(CurDSG);

		SOC_Calculate_Element.u8SOC_Old = get_soc_real();
		SOC_Calculate_Element.u32CapChange += (UINT32)g_stCellInfoReport.u16IDischg * 1;
		SOC_Calculate_Element.u32CapNow -= (UINT32)g_stCellInfoReport.u16IDischg * 1;

		if (SOC_Calculate_Element.u32CapNow > SOC_Calculate_Element.u32CapFull)
			SOC_Calculate_Element.u32CapNow = 0;
		C_change_per = SOC_Calculate_Element.u32CapChange * 100 / SOC_Calculate_Element.u32CapFull;
		SOC_Calculate_Element.u8SOC_Now = SOC_Calculate_Element.u8SOC_Old - C_change_per;
		if (get_soc_real() > 100)
			SOC_Calculate_Element.u8SOC_Now = 0;
		SOC_Calculate_Element.u32CapChange = (((SOC_Calculate_Element.u32CapChange * 100) % SOC_Calculate_Element.u32CapFull) + 50) / 100; // �������룬�ؼ�
		SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 0;

		if (get_soc_real() != 0)
		{
			SOC_Calculate_Element.u8DSG_SOC_Int += C_change_per;
			// SOC_Calculate_Element.u8DSG_SOC_Int += 1;
			if (SOC_Calculate_Element.u8DSG_SOC_Int >= 80)
			{
				SOC_Calculate_Element.u8DSG_SOC_Int = 0;
				SOC_Calculate_Element.u32Cycle_times += 1;
			}
		}
	}
}

void SOC_State_Transfer(void)
{
	static uint8_t s_u8SOC_State_CHG = 0;
	static uint8_t s_u8SOC_State_DSG = 0;
	static uint8_t s_u8SOC_State_OCV = 0;
	if (g_stCellInfoReport.u16Ichg >= SOC_VIRTUAL_CURRENT_CHG)
	{
		if (++s_u8SOC_State_CHG >= 3)
		{
			s_u8SOC_State_CHG = 0;
			SOC_Cali_Flag = SOC_CALI_CONT_CHG;
		}
		if (s_u8SOC_State_DSG)
			s_u8SOC_State_DSG = 0;
		if (s_u8SOC_State_OCV)
			s_u8SOC_State_OCV = 0;
	}
	else if (g_stCellInfoReport.u16IDischg >= SOC_VIRTUAL_CURRENT_DSG)
	{
		if (++s_u8SOC_State_DSG >= 3)
		{
			s_u8SOC_State_DSG = 0;
			SOC_Cali_Flag = SOC_CALI_CONT_DSG;
		}
		if (s_u8SOC_State_CHG)
			s_u8SOC_State_CHG = 0;
		if (s_u8SOC_State_OCV)
			s_u8SOC_State_OCV = 0;
	}
	else
	{
		if (++s_u8SOC_State_OCV >= 3)
		{
			s_u8SOC_State_OCV = 0;
		}
		if (s_u8SOC_State_CHG)
			s_u8SOC_State_CHG = 0;
		if (s_u8SOC_State_DSG)
			s_u8SOC_State_DSG = 0;
	}
}


void SOC_Result_Pass(void)
{
#ifndef _DOUBLE_SOC_FUNC_
	g_stCellInfoReport.SocElement.u16Soc = get_soc_real();
	g_stCellInfoReport.SocElement.u16CapacityNow = SOC_Calculate_Element.u32CapNow * 1 / 360;
#else
	g_stCellInfoReport.real_now_Capacity = SOC_Calculate_Element.u32CapNow * 1 / 360;
#endif
	// g_stCellInfoReport.SocElement.u16Soh = 100;
	g_stCellInfoReport.SocElement.u16Soh = bms_soh_from_cycle(SOC_Calculate_Element.u32Cycle_times);

	g_stCellInfoReport.SocElement.u16CapacityFull = SOC_Calculate_Element.u32CapFull * 1 / 360;
	g_stCellInfoReport.SocElement.u16CapacityFactory = SOC_Calculate_Element.u32CapFactory * 1 / 360;
	g_stCellInfoReport.SocElement.u16Cycle_times = SOC_Calculate_Element.u32Cycle_times;
}


void SOC_EEPROM_Deal_Monitor(void)
{
	// static uint8_t back_soc = 0;

	if (back_SOC_Calculate_Element.u8SOC_Now != SOC_Calculate_Element.u8SOC_Now)
	{
		back_SOC_Calculate_Element.u8SOC_Now = SOC_Calculate_Element.u8SOC_Now;
		// nvm_param_set(NVM_KEY_SOC, SOC_Calculate_Element.u8SOC_Now);
		// printf("soc %d", SOC_Calculate_Element.u8SOC_Now);
	}
	if (back_SOC_Calculate_Element.u8DSG_SOC_Int != SOC_Calculate_Element.u8DSG_SOC_Int)
	{
		back_SOC_Calculate_Element.u8DSG_SOC_Int = SOC_Calculate_Element.u8DSG_SOC_Int;
		// nvm_param_set(NVM_KEY_DSGSOC_INT, SOC_Calculate_Element.u8DSG_SOC_Int);
		// printf("dsg soc int %d", SOC_Calculate_Element.u8DSG_SOC_Int);
	}
	if (back_SOC_Calculate_Element.u32Cycle_times != SOC_Calculate_Element.u32Cycle_times)
	{
		back_SOC_Calculate_Element.u32Cycle_times = SOC_Calculate_Element.u32Cycle_times;
		// nvm_param_set(NVM_KEY_CYCLES, SOC_Calculate_Element.u32Cycle_times);
		// nvm_param_set(NVM_KEY_CAPACITY, SOC_Calculate_Element.u32CapFactory);
	}
	// if()
}

void soc_cali(void)
{
	static uint8_t dsg_soc0_delay = 0;

	uint16_t temp_raw = soc_temp_get_battery_temp_raw();
	uint16_t soc100_val = soc_get_temp_v100(temp_raw);
	uint16_t soc0_val  = soc_get_temp_v0(temp_raw);
	uint16_t totle_soc100 = (uint16_t)((uint32_t)soc100_val * 4000 / 4180);

	if (isCHG())
	{
		if ((g_stCellInfoReport.u16VCellMax >= soc100_val) && g_stCellInfoReport.u16VCellMin >= totle_soc100)
		{
			SOC_Calculate_Element.u8SOC_Now = 100;
			SOC_Calculate_Element.u32CapNow = SOC_Calculate_Element.u32CapFull;
		}
	}
	else
	{
		if ((g_stCellInfoReport.u16VCellMin <= soc0_val) && (g_stCellInfoReport.u16VCellMin >= 2000))
		{
			if (++dsg_soc0_delay >= (5 * 10))
			{
				dsg_soc0_delay = 0;
				SOC_Calculate_Element.u8SOC_Now = 0;
				SOC_Calculate_Element.u32CapNow = 0;
			}
		}
		else
		{
			dsg_soc0_delay = 0;
		}
	}
}

void APP_SOC_IntEnhance_Ctrl()
{
	switch (SOC_Cali_Flag)
	{
	case SOC_CALI_STATE_TRANSFER:
		SOC_State_Transfer();
		break;
	case SOC_CALI_CONT_CHG:
		SOC_Cont_AH_Int_CHG();
		break;
	case SOC_CALI_CONT_DSG:
		SOC_Cont_AH_Int_DSG();
		break;
	default:
		break;
	}

	soc_rest_detect();
	soc_ocv_calibration();
	soc_cali();
	SOC_Result_Pass();
}

#endif
