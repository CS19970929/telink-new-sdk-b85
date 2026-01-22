#include "SocEnhance.h"
// #include "DataDeal.h"
// #include "EEPROM.h"
// #include "Sci_Upper.h"
// #include "main.h"
#include "conf.h"
#include "Sci_Upper.h"
#include "soc_kv_store.h"

extern struct stCell_Info g_stCellInfoReport;
// #include "soc_module_test.h"

UINT32 ModulusSub(uint32_t Data1, uint32_t Data2)
{
	return (UINT32)(Data1 > Data2 ? Data1 - Data2 : Data2 - Data1);
}

#if 1

#define SOC_FAC_VALUE 60
#define E2P_ADDR_SOC_RECORD_backup (1024 * 3)
#define E2P_ADDR_SOC_RECORD (E2P_ADDR_SOC_RECORD_backup - 2)

#if 1
// #define SOC_100_VAL g_tParam.other.u16Soc_V_100
// #define SOC_0_VAL g_tParam.other.u16Soc_V_0
#define SOC_100_VAL (4150)
#define SOC_0_VAL (3200)

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
	SOC_Calculate_Element.u32CapFactory = (UINT32)CapacityFactory * 3600; // ???*10;???��??????????��????????��????????
	// SOC_Calculate_Element.u32Cycle_times = (UINT32)1 * 100;
	SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFactory;
	SOC_Calculate_Element.u8DSG_SOC_Int = 0;
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
	set_calsoc(_soc->soc);

	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
	SOC_Calculate_Element.u8DSG_SOC_Int = _soc->dsg;
	SOC_Calculate_Element.u32CapFactory = SOC_Calculate_Element.u32CapFull;
	SOC_Calculate_Element.u32Cycle_times = _soc->cycle;

	back_SOC_Calculate_Element = SOC_Calculate_Element;

	SOC_Result_Pass();
}

uint8_t Get_OpenCircuit_Value_new(uint16_t VCell)
{
#if 0
	uint8_t result = 0;

	// temp_compensatation();

	switch (g_tParam.other.u16Soc_TableSelect)
	{
	case SOC_TABLE_LIFEPO:
		result = GetEndValue(SOC_Table_LiFePO, (uint16_t)SOC_Size_LiFePO, VCell);
		if (VCell <= SOC_0_VAL)
		{
			result = 0;
		}
		break;
	case SOC_TABLE_TERNARYLI:
		result = GetEndValue(SocTable_TernaryLi, (uint16_t)SOC_Size_TernaryLi, (uint16_t)VCELLMIN);
		break;
	case SOC_TABLE_LIFEPO2:
		result = GetEndValue(SocTable_LiFePO2, (uint16_t)SOC_Size_LiFePO2, (uint16_t)VCELLMIN);
		break;
	default:
		break;
	}
	return result;
#endif
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

void Correction_CapacityFull(void)
{
	static uint16_t su16_ChgCur_Tcnt = 0;
	static uint16_t su16_DsgCur_Tcnt = 0;
	static uint16_t su16_CalErr_Tcnt = 0;

	switch (CapFull_Cali_Flag)
	{
	case CAP_FULL_INIT:
		SOC_Calculate_Element.u32CapFull_Cal_As = 0;
		CapFull_Cali_Flag = CAP_FULL_STARTUP;
		break;

	case CAP_FULL_STARTUP:
		if (VCELLMIN <= SOC_0_VAL)
		{
			if (g_stCellInfoReport.u16IDischg <= SOC_VIRTUAL_CURRENT_DSG)
			{
				if (++su16_DsgCur_Tcnt > 5)
				{
					su16_DsgCur_Tcnt = 0;
					CapFull_Cali_Flag = CAP_FULL_CALCU;
					SOC_Calculate_Element.u32CapFull_Cal_As = 0; // ������ʼ������ʼ����
				}
			}
			else
			{
				su16_DsgCur_Tcnt = 0;
			}
		}
		break;

	case CAP_FULL_CALCU:
		if (VCELLMAX >= SOC_100_VAL)
		{
			if (g_stCellInfoReport.u16Ichg <= SOC_VIRTUAL_CURRENT_CHG && g_stCellInfoReport.u16IDischg <= SOC_VIRTUAL_CURRENT_DSG)
			{
				if (++su16_ChgCur_Tcnt > 5)
				{
					su16_ChgCur_Tcnt = 0;
					CapFull_Cali_Flag = CAP_FULL_SUCCESS;
				}
			}
			else
			{
				su16_ChgCur_Tcnt = 0;
			}
		}

		if (g_stCellInfoReport.u16Ichg < SOC_VIRTUAL_CURRENT_CHG)
		{
			if (++su16_CalErr_Tcnt >= 5 * 60 * 10)
			{
				su16_CalErr_Tcnt = 0;
				CapFull_Cali_Flag = CAP_FULL_FAIL;
			}
		}
		else
		{
			su16_CalErr_Tcnt = 0;
		}

		if (g_stCellInfoReport.u16IDischg > SOC_VIRTUAL_CURRENT_DSG)
		{
			if (++su16_DsgCur_Tcnt > 5)
			{
				su16_DsgCur_Tcnt = 0;
				CapFull_Cali_Flag = CAP_FULL_FAIL;
			}
		}
		else
		{
			su16_DsgCur_Tcnt = 0;
		}
		break;

	case CAP_FULL_SUCCESS:
		if (SOC_Calculate_Element.u32CapFull_Cal_As == 0)
		{
			SOC_Calculate_Element.u32CapFull_Cal_As = SOC_Calculate_Element.u32CapFull;
		}
		SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFull_Cal_As; // ������������
		SOC_Calculate_Element.u32CapFull_Cal_As = 0;
		SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
		CapFull_Cali_Flag = CAP_FULL_STARTUP;
		break;

	case CAP_FULL_FAIL:
		SOC_Calculate_Element.u32CapFull_Cal_As = 0;
		CapFull_Cali_Flag = CAP_FULL_STARTUP;
		break;

	default:
		break;
	}

	if (SOC_Calculate_Element.u32CapFull == 0)
	{
		SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFactory;
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

void SOC_Update_param(void)
{
#if 0
	SOC_Calculate_Element.u8DSG_SOC_Int = 0;
	SOC_Calculate_Element.u32CapFactory = (UINT32)g_tParam.other.u16Soc_Ah * 3600;
	SOC_Calculate_Element.u32Cycle_times = (UINT32)g_tParam.other.u16Soc_Cycle_times * 100;
	// ����SOC_Calculate_Element.u32CapFactory�Ѿ���ʼ��
	SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFactory;

	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
#endif
	InitData_SOC();
}

void SOC_Result_Pass(void)
{
#ifndef _DOUBLE_SOC_FUNC_
	g_stCellInfoReport.SocElement.u16Soc = get_soc_real();
	g_stCellInfoReport.SocElement.u16CapacityNow = SOC_Calculate_Element.u32CapNow * 1 / 360;
#else
	g_stCellInfoReport.real_now_Capacity = SOC_Calculate_Element.u32CapNow * 1 / 360;
#endif

	// if (SOC_Calculate_Element.u32CapFull >= SOC_Calculate_Element.u32CapFactory)
	// {
	// 	g_stCellInfoReport.SocElement.u16Soh = 100;
	// }
	// else
	// {
	// 	g_stCellInfoReport.SocElement.u16Soh = (uint8_t)((100 * SOC_Calculate_Element.u32CapFull / SOC_Calculate_Element.u32CapFactory) & 0xFF);
	// }

	// todo soh algo
	g_stCellInfoReport.SocElement.u16Soh = 100;

	g_stCellInfoReport.SocElement.u16CapacityFull = SOC_Calculate_Element.u32CapFull * 1 / 360;
	g_stCellInfoReport.SocElement.u16CapacityFactory = SOC_Calculate_Element.u32CapFactory * 1 / 360;
	g_stCellInfoReport.SocElement.u16Cycle_times = SOC_Calculate_Element.u32Cycle_times;
}

void bmsParam_save(void)
{
	uint8_t soc_disp = get_dispsoc();

	if (g_bms_param_default.soc_para.soc != soc_disp)
	{
		g_bms_param_default.soc_para.soc = soc_disp;
		WriteEEPROM_Word_NoZone(E2P_ADDR_SOC_RECORD_backup, g_stCellInfoReport.SocElement.u16Soc);
	}
}

#if 0
#define LARGE_CURR 500
#define LARGE_CURR2 100

#define N 7

static uint8_t ocv_state = 0;
static uint8_t ocv_cnt = 0;
static uint8_t arr_soc[N] = {0, 0, 0, 0, 0};

static uint8_t large_curr_flag = 0;

static uint32_t ocv200mscnt = 0;
static uint32_t ocv200mscnt_large_curr = 0;

void PRE_OCV(void)
{
#define OCV_CURRENT_THRESHOLD (10)
	// static uint8_t state_pre_ocv = 0;

	if (g_stCellInfoReport.u16Ichg >= LARGE_CURR || g_stCellInfoReport.u16IDischg >= LARGE_CURR)
	{
		large_curr_flag = 1;

		ocv200mscnt = 0;
		ocv200mscnt_large_curr = 0;

		return;
	}
	else if (g_stCellInfoReport.u16Ichg >= LARGE_CURR2 || g_stCellInfoReport.u16IDischg >= LARGE_CURR2)
	{
		large_curr_flag = 2;

		ocv200mscnt = 0;
		ocv200mscnt_large_curr = 0;

		return;
	}

	if (!large_curr_flag)
	{
		//!!! С����ֵ�ĵ�����ʵʱУ׼����soc�ϲ�ȥ,��ȷ�ϣ��϶��䲻��ȥ �������ǰ�
		if (g_stCellInfoReport.u16Ichg <= OCV_CURRENT_THRESHOLD && g_stCellInfoReport.u16IDischg <= OCV_CURRENT_THRESHOLD)
		{
			if (++ocv200mscnt >= g_debug.real_ocv_start_delay_time)
			{
				log_a("start real ocv cali");
				ocv200mscnt = 0;
				ocv_state = 1;
			}
		}
		else
		{
			ocv200mscnt = 0;
		}
	}
	else if (large_curr_flag == 1)
	{
		if (g_stCellInfoReport.u16Ichg <= OCV_CURRENT_THRESHOLD && g_stCellInfoReport.u16IDischg <= OCV_CURRENT_THRESHOLD)
		{
			// 3��Сʱ������
			if (++ocv200mscnt_large_curr >= 5 * 60 * 180)
			{
				ocv200mscnt_large_curr = 0;

				large_curr_flag = 0;

				// ocv_state = 1;
			}
		}
		//!!!!!!!!!!!!???!!!!!!!!!!!!
		// else
		// {
		// 	ocv200mscnt = 0;
		// }
	}
	else if (large_curr_flag == 2)
	{
		if (g_stCellInfoReport.u16Ichg <= OCV_CURRENT_THRESHOLD && g_stCellInfoReport.u16IDischg <= OCV_CURRENT_THRESHOLD && VCELLMIN <= OCV_VOL_ENABLE)
		{
			if (++ocv200mscnt_large_curr >= 5 * 60 * 60)
			{
				ocv200mscnt_large_curr = 0;

				large_curr_flag = 0;

				// ocv_state = 1;
				// log_w("large curr ocv cali real soc-> %d\n", soc_calculate.u8SOC_Now);
			}
		}
	}
}
uint8_t get_ocv_cali(uint8_t *arr_soc)
{
	uint16_t sum = 0;
	uint8_t temp = 0;
	uint8_t ocv_soc = 0;

	char count, i, j;
	for (j = 0; j < (N - 1); j++)
	{
		for (i = 0; i < (N - j - 1); i++)
		{
			if (arr_soc[i] > arr_soc[i + 1])
			{
				temp = arr_soc[i];
				arr_soc[i] = arr_soc[i + 1];
				arr_soc[i + 1] = temp;
			}
		}
	}
// #ifdef __test__
#if 1
	uint8_t k = 0;

	log_e("arr_soc[]: ");
	for (k = 0; k < N; k++)
	{
		log_w("%d ", arr_soc[k]);
	}
#endif
	if (ModulusSub(arr_soc[N - 1], arr_soc[0]) > 10)
	{
		log_e("maxsoc %d minsoc %d", arr_soc[N - 1], arr_soc[0]);
		goto _err;
	}
	for (count = 1; count < N - 1; count++)
	{
		sum += arr_soc[count];
	}
	ocv_soc = (uint8_t)(sum / (N - 2));
	log_e("ocv cali soc->%d", ocv_soc);

	return ocv_soc;

_err:
	return get_dispsoc();
}

void SOC_OCV_Fix2(void)
{
	static uint8_t ocv_soc = 0;

	// static uint8_t ocv_soc_record[10];
	// static bool is_firstOCV = true;

	if (VCELLMIN > OCV_VOL_ENABLE)
	{
		ocv_state = 0;
		ocv_cnt = 0;
		ocv200mscnt = 0;
		ocv200mscnt_large_curr = 0;
		return;
	}
	if (g_stCellInfoReport.u16Ichg > 10 || g_stCellInfoReport.u16IDischg > 10 || g_debug.people_set)
	{
		if (g_debug.people_set)
			g_debug.people_set = false;

		ocv_state = 0;
		ocv_cnt = 0;

		// return;
	}
	switch (ocv_state)
	{
	case 0:
	{
		PRE_OCV();
		break;
	}
	case 1:
	{
		arr_soc[ocv_cnt] = get_soc_from_openVol_onlyDec_new(VCELLMIN);

		if (++ocv_cnt >= N)
		{
			ocv_cnt = 0;
			// ocv_state = 2;
			ocv_state = 0;

			ocv_soc = get_ocv_cali(arr_soc);

			// todo ���Ŷ�confidense �б�
			set_soc_param(ocv_soc, 1, 0);
		}
		break;
	}
#if 0
	case 2:
	{
		// todo ??????????????????????????????��?????soc?��??????��eeprom????
		// todo ???��????????��soc eeprom

		if (ModulusSub(ocv_soc, SOC_Calculate_Element.u8SOC_Now) > 3)
		{
			set_calsoc(ocv_soc);
			log_e("ocv success");
		}
		else
		{
			log_e("ocv soc, soc_now err < 3, not update ocv_soc:%d, soc_now:%d", ocv_soc, SOC_Calculate_Element.u8SOC_Now);
		}

		break;
	}
#endif
	default:
		break;
	}
}
#endif

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
#define TERNARYLI

#ifdef TERNARYLI
#define Totle_soc100 (4000)
#elif (defined(LIFEPO))
#define Totle_soc100 (3300)
#endif

	if (isCHG())
	{
		if ((g_stCellInfoReport.u16VCellMax >= SOC_100_VAL) && g_stCellInfoReport.u16VCellMin >= Totle_soc100)
		{
			SOC_Calculate_Element.u8SOC_Now = 100;
			SOC_Calculate_Element.u32CapNow = SOC_Calculate_Element.u32CapFull;
		}
	}
	else
	{
		if ((g_stCellInfoReport.u16VCellMin <= SOC_0_VAL) && (g_stCellInfoReport.u16VCellMin >= 2000))
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

	soc_cali();

	// SOC_EEPROM_Deal_Monitor();
	// Correction_CapacityFull();

	SOC_Result_Pass();
}

#endif
