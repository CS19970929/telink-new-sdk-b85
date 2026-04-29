#include "SocEnhance.h"
// #include "DataDeal.h"
// #include "EEPROM.h"
// #include "Sci_Upper.h"
// #include "main.h"
#include "conf.h"
#include "Sci_Upper.h"
#include "soc_kv_store.h"

extern struct stCell_Info g_stCellInfoReport;
void SOC_Result_Pass(void);
// #include "soc_module_test.h"

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
#define SOC_0_VAL (3000)

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

#define SOC_PERCENT_MAX                 100u
#define SOC_DSG_INT_MAX                 79u
#define SOC_CYCLE_MAX                   65535u
#define SOC_OCV_VALID_MIN_MV            2500u
#define SOC_OCV_VALID_MAX_MV            4000u
#define SOC_OCV_CELL_DELTA_MAX_MV       120u
#define SOC_OCV_RUNTIME_DIFF_THRESHOLD  5u
#define SOC_OCV_RUNTIME_STEP            1u
#define SOC_OCV_IDLE_STABLE_TICKS       (5 * 60 * 30)
#define SOC_OCV_IDLE_ADJUST_TICKS       (5 * 60 * 2)
#define SOC_FULL_SYNC_MIN_MV            ((uint16_t)(SOC_100_VAL - 120u))
#define SOC_EMPTY_SYNC_MAX_MV           ((uint16_t)(SOC_0_VAL + 120u))
#define SOC_FULL_LOCK_TICKS             20u
#define SOC_EMPTY_LOCK_TICKS            25u
#define SOC_DSG_TERMINAL_START_MV       ((uint16_t)(SOC_0_VAL + 300u))
#define SOC_DSG_TERMINAL_L1_MV          ((uint16_t)(SOC_0_VAL + 200u))
#define SOC_DSG_TERMINAL_L2_MV          ((uint16_t)(SOC_0_VAL + 150u))
#define SOC_DSG_TERMINAL_L3_MV          ((uint16_t)(SOC_0_VAL + 50u))
#define SOC_DSG_TERMINAL_VALID_MIN_MV   2000u
#define SOC_DSG_TERMINAL_STEP_TICKS     5u
#define SOC_DSG_EMPTY_LOCK_TICKS        10u
#define SOC_TERMINAL_SYNC_STEP_TICKS    5u

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
typedef struct
{
	uint16_t idle_stable_ticks;
	uint16_t idle_adjust_ticks;
	uint8_t startup_checked;
	uint8_t full_lock_ticks;
	uint8_t empty_lock_ticks;
	uint8_t full_terminal_adjust_ticks;
	uint8_t empty_terminal_adjust_ticks;
	uint8_t dsg_terminal_adjust_ticks;
	uint8_t dsg_empty_lock_ticks;
} soc_strategy_state_t;

static soc_strategy_state_t g_soc_strategy_state;


uint16_t get_idle_stable_ticks(void)
{
		return g_soc_strategy_state.idle_stable_ticks ;
}
uint16_t get_idle_adjust_ticks(void)
{
		return g_soc_strategy_state.idle_adjust_ticks ;
}

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

static uint8_t soc_limit_percent_u32(uint32_t value)
{
	if (value > SOC_PERCENT_MAX)
	{
		return SOC_PERCENT_MAX;
	}

	return (uint8_t)value;
}

static uint8_t soc_limit_dsg_u32(uint32_t value)
{
	if (value > SOC_DSG_INT_MAX)
	{
		return SOC_DSG_INT_MAX;
	}

	return (uint8_t)value;
}

static uint32_t soc_limit_cycle_u32(uint32_t value)
{
	if (value > SOC_CYCLE_MAX)
	{
		return SOC_CYCLE_MAX;
	}

	return value;
}

static uint16_t soc_cycle_to_u16(uint32_t value)
{
	return (uint16_t)soc_limit_cycle_u32(value);
}

static void soc_recalc_full_capacity(void)
{
	SOC_Calculate_Element.u32CapFactory = (UINT32)CapacityFactory * 3600u;
	SOC_Calculate_Element.soh = bms_soh_from_cycle(soc_cycle_to_u16(SOC_Calculate_Element.u32Cycle_times));
	SOC_Calculate_Element.u32CapFull = (SOC_Calculate_Element.u32CapFactory * SOC_Calculate_Element.soh) / 100u;
	if (SOC_Calculate_Element.u32CapFull == 0u)
	{
		SOC_Calculate_Element.u32CapFull = 1u;
	}
}

static void soc_recalc_now_capacity(void)
{
	SOC_Calculate_Element.u32CapNow = ((UINT32)get_soc_real() * SOC_Calculate_Element.u32CapFull) / 100u;
}

static void soc_reset_integral_accumulator(void)
{
	SOC_Calculate_Element.u32CapChange = 0u;
	SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 0u;
	SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 0u;
}

static void soc_apply_real_value(uint8_t soc, uint8_t sync_display)
{
	SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(soc);
	soc_recalc_now_capacity();
	if (sync_display)
	{
		set_dispsoc(SOC_Calculate_Element.u8SOC_Now);
	}
}

static uint8_t soc_apply_step_towards_value(uint8_t target_soc, uint8_t sync_display)
{
	uint8_t current_soc;

	target_soc = soc_limit_percent_u32(target_soc);
	current_soc = get_soc_real();
	if (current_soc == target_soc)
	{
		return 0u;
	}

	if (current_soc < target_soc)
	{
		current_soc++;
	}
	else
	{
		current_soc--;
	}

	soc_apply_real_value(current_soc, sync_display);
	return 1u;
}

static uint8_t soc_apply_step_towards_when_due(uint8_t target_soc, uint8_t *ticks, uint8_t step_ticks)
{
	if (get_soc_real() == soc_limit_percent_u32(target_soc))
	{
		*ticks = 0u;
		return 0u;
	}

	if (*ticks < step_ticks)
	{
		(*ticks)++;
	}

	if (*ticks < step_ticks)
	{
		return 0u;
	}

	*ticks = 0u;
	if (soc_apply_step_towards_value(target_soc, 1u))
	{
		soc_reset_integral_accumulator();
		return 1u;
	}

	return 0u;
}

static void soc_sanitize_state(void)
{
	SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(SOC_Calculate_Element.u8SOC_Now);
	SOC_Calculate_Element.u8DSG_SOC_Int = soc_limit_dsg_u32(SOC_Calculate_Element.u8DSG_SOC_Int);
	SOC_Calculate_Element.u32Cycle_times = soc_limit_cycle_u32(SOC_Calculate_Element.u32Cycle_times);
	soc_recalc_full_capacity();
	soc_recalc_now_capacity();
	if (SOC_Calculate_Element.u32CapNow > SOC_Calculate_Element.u32CapFull)
	{
		SOC_Calculate_Element.u32CapNow = SOC_Calculate_Element.u32CapFull;
	}
}

static uint8_t soc_ocv_sample_valid(void)
{
	if ((VCELLMIN < SOC_OCV_VALID_MIN_MV) || (VCELLMIN > SOC_OCV_VALID_MAX_MV))
	{
		return 0u;
	}

	if ((VCELLMAX < VCELLMIN) || (VCELLMAX > SOC_OCV_VALID_MAX_MV))
	{
		return 0u;
	}

	if (g_stCellInfoReport.u16VCellDelta > SOC_OCV_CELL_DELTA_MAX_MV)
	{
		return 0u;
	}

	return 1u;
}

static uint8_t soc_idle_for_ocv(void)
{
	return (g_stCellInfoReport.u16Ichg == 0u) && (g_stCellInfoReport.u16IDischg == 0u);
}

static uint8_t soc_estimate_percent_from_cell_mv(uint16_t cell_mv)
{
	uint32_t numerator;
	uint16_t denominator;

	if (cell_mv <= SOC_0_VAL)
	{
		return 0u;
	}

	if (cell_mv >= SOC_100_VAL)
	{
		return SOC_PERCENT_MAX;
	}

	denominator = (uint16_t)(SOC_100_VAL - SOC_0_VAL);
	numerator = ((uint32_t)(cell_mv - SOC_0_VAL) * SOC_PERCENT_MAX) + (denominator / 2u);
	return (uint8_t)(numerator / denominator);
}

static uint8_t soc_estimate_ocv_percent(void)
{
	uint16_t weighted_mv;

	weighted_mv = (uint16_t)((((uint32_t)VCELLMIN * 3u) + (uint32_t)VCELLMAX) / 4u);
	return soc_estimate_percent_from_cell_mv(weighted_mv);
}

static void soc_apply_startup_ocv_correction(void)
{
	uint8_t current_soc;
	uint8_t ocv_soc;

	if (g_soc_strategy_state.startup_checked)
	{
		return;
	}

	if (!soc_ocv_sample_valid() || !soc_idle_for_ocv())
	{
		return;
	}

	g_soc_strategy_state.startup_checked = 1u;
	current_soc = get_soc_real();
	ocv_soc = soc_estimate_ocv_percent();

	/* E-bike 仪表更看重开机稳定性。
	 * 启动阶段只在明显满电/空电端点做硬校正，避免中段 SOC 开机跳变。 */
	if (((ocv_soc == 0u) && (VCELLMIN <= SOC_0_VAL) && (current_soc > 3u)) ||
		((ocv_soc == SOC_PERCENT_MAX) && (VCELLMAX >= SOC_100_VAL) && (current_soc < 97u)))
	{
		soc_apply_real_value(ocv_soc, 1u);
		soc_reset_integral_accumulator();
	}
}

static void soc_apply_idle_ocv_tracking(void)
{
	uint8_t current_soc;
	uint8_t ocv_soc;
	uint8_t diff;

	if (!soc_idle_for_ocv() || !soc_ocv_sample_valid())
	{
		g_soc_strategy_state.idle_stable_ticks = 0u;
		g_soc_strategy_state.idle_adjust_ticks = 0u;
		return;
	}

	if (g_soc_strategy_state.idle_stable_ticks < SOC_OCV_IDLE_STABLE_TICKS)
	{
		g_soc_strategy_state.idle_stable_ticks++;
		g_soc_strategy_state.idle_adjust_ticks = 0u;
		return;
	}

	if (++g_soc_strategy_state.idle_adjust_ticks < SOC_OCV_IDLE_ADJUST_TICKS)
	{
		return;
	}
	g_soc_strategy_state.idle_adjust_ticks = 0u;

	current_soc = get_soc_real();
	ocv_soc = soc_estimate_ocv_percent();

	/* Idle OCV tracking is bidirectional, but is limited to 1% per adjust window. */
	if (ocv_soc >= current_soc)
	{
		diff = (uint8_t)(ocv_soc - current_soc);
	}
	else
	{
		diff = (uint8_t)(current_soc - ocv_soc);
	}

	if (diff < SOC_OCV_RUNTIME_DIFF_THRESHOLD)
	{
		return;
	}

	soc_apply_step_towards_value(ocv_soc, 1u);
	soc_reset_integral_accumulator();
}

static void soc_apply_terminal_sync(void)
{
	if (isCHG() && (VCELLMAX >= SOC_100_VAL) && (VCELLMIN >= SOC_FULL_SYNC_MIN_MV))
	{
		if (g_soc_strategy_state.full_lock_ticks < SOC_FULL_LOCK_TICKS)
		{
			g_soc_strategy_state.full_lock_ticks++;
		}
		if (g_soc_strategy_state.full_lock_ticks >= SOC_FULL_LOCK_TICKS)
		{
			soc_apply_step_towards_when_due(SOC_PERCENT_MAX,
											&g_soc_strategy_state.full_terminal_adjust_ticks,
											SOC_TERMINAL_SYNC_STEP_TICKS);
		}
	}
	else
	{
		g_soc_strategy_state.full_lock_ticks = 0u;
		g_soc_strategy_state.full_terminal_adjust_ticks = 0u;
	}

	if (soc_idle_for_ocv() && (VCELLMIN <= SOC_0_VAL) && (VCELLMAX <= SOC_EMPTY_SYNC_MAX_MV))
	{
		if (g_soc_strategy_state.empty_lock_ticks < SOC_EMPTY_LOCK_TICKS)
		{
			g_soc_strategy_state.empty_lock_ticks++;
		}
		if (g_soc_strategy_state.empty_lock_ticks >= SOC_EMPTY_LOCK_TICKS)
		{
			soc_apply_step_towards_when_due(0u,
											&g_soc_strategy_state.empty_terminal_adjust_ticks,
											SOC_TERMINAL_SYNC_STEP_TICKS);
		}
	}
	else
	{
		g_soc_strategy_state.empty_lock_ticks = 0u;
		g_soc_strategy_state.empty_terminal_adjust_ticks = 0u;
	}
}

static uint8_t soc_discharge_terminal_soc_ceiling(void)
{
	if (VCELLMIN <= SOC_0_VAL)
	{
		return 0u;
	}

	if (VCELLMIN <= SOC_DSG_TERMINAL_L3_MV)
	{
		return 1u;
	}

	if (VCELLMIN <= SOC_DSG_TERMINAL_L2_MV)
	{
		return 3u;
	}

	if (VCELLMIN <= SOC_DSG_TERMINAL_L1_MV)
	{
		return 6u;
	}

	if (VCELLMIN <= SOC_DSG_TERMINAL_START_MV)
	{
		return 12u;
	}

	return SOC_PERCENT_MAX;
}

static void soc_apply_discharge_terminal_tracking(void)
{
	uint8_t current_soc;
	uint8_t target_soc;

	if (!isDSG() || (VCELLMAX < VCELLMIN) || (VCELLMIN < SOC_DSG_TERMINAL_VALID_MIN_MV))
	{
		g_soc_strategy_state.dsg_terminal_adjust_ticks = 0u;
		g_soc_strategy_state.dsg_empty_lock_ticks = 0u;
		return;
	}

	target_soc = soc_discharge_terminal_soc_ceiling();
	if (target_soc >= SOC_PERCENT_MAX)
	{
		g_soc_strategy_state.dsg_terminal_adjust_ticks = 0u;
		g_soc_strategy_state.dsg_empty_lock_ticks = 0u;
		return;
	}

	current_soc = get_soc_real();

	if ((current_soc == 0u) && (target_soc > 0u))
	{
		soc_apply_real_value(1u, 1u);
		soc_reset_integral_accumulator();
		g_soc_strategy_state.dsg_terminal_adjust_ticks = 0u;
		g_soc_strategy_state.dsg_empty_lock_ticks = 0u;
		return;
	}

	if (target_soc == 0u)
	{
		if (g_soc_strategy_state.dsg_empty_lock_ticks < SOC_DSG_EMPTY_LOCK_TICKS)
		{
			g_soc_strategy_state.dsg_empty_lock_ticks++;
		}
		if (g_soc_strategy_state.dsg_empty_lock_ticks >= SOC_DSG_EMPTY_LOCK_TICKS)
		{
			soc_apply_step_towards_when_due(0u,
											&g_soc_strategy_state.dsg_terminal_adjust_ticks,
											SOC_DSG_TERMINAL_STEP_TICKS);
		}
		return;
	}

	g_soc_strategy_state.dsg_empty_lock_ticks = 0u;
	if (current_soc <= target_soc)
	{
		g_soc_strategy_state.dsg_terminal_adjust_ticks = 0u;
		return;
	}

	soc_apply_step_towards_when_due(target_soc,
									&g_soc_strategy_state.dsg_terminal_adjust_ticks,
									SOC_DSG_TERMINAL_STEP_TICKS);
}

static void soc_strategy_update(void)
{
	soc_apply_startup_ocv_correction();
	soc_apply_terminal_sync();
	soc_apply_discharge_terminal_tracking();
	soc_apply_idle_ocv_tracking();
}

void set_calsoc(uint8_t _soc)
{
	SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(_soc);
	soc_recalc_full_capacity();
	soc_recalc_now_capacity();
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
	(void)_cap_factory;

	{
		// soc_calculate.u32CapFactory = (UINT32)g_tParam.other.u16Soc_Ah * 3600;
		// soc_calculate.u32Cycle_times = (UINT32)g_tParam.other.u16Soc_Cycle_times * 100;
	}

	set_calsoc(_soc_val);
	soc_reset_integral_accumulator();
	if (disp_sync_updatae)
	{
		set_dispsoc(get_soc_real());
	}
	soc_recalc_now_capacity();
}

void soc_factory_param_init_first(void)
{
#if 1
	SOC_Calculate_Element.u8SOC_Now = (uint8_t)SOC_PARAM_DEFAULT_SOC;
	SOC_Calculate_Element.u32Cycle_times = soc_limit_cycle_u32(SOC_PARAM_DEFAULT_CYCLE);
	SOC_Calculate_Element.u8DSG_SOC_Int = soc_limit_dsg_u32(SOC_PARAM_DEFAULT_DSG);

	// {
	// 	nvm_param_set(NVM_KEY_SOC, SOC_Calculate_Element.u8SOC_Now);
	// 	nvm_param_set(NVM_KEY_DSGSOC_INT, 0);
	// 	nvm_param_set(NVM_KEY_CYCLES, SOC_Calculate_Element.u32Cycle_times);
	// 	nvm_param_set(NVM_KEY_CAPACITY, SOC_Calculate_Element.u32CapFactory);
	// }

	memset(&g_soc_strategy_state, 0, sizeof(g_soc_strategy_state));
	soc_sanitize_state();
	back_SOC_Calculate_Element = SOC_Calculate_Element;

#endif
}

void soc_param_lib_init(soc_kv_data_t *_soc)
{
	memset(&g_soc_strategy_state, 0, sizeof(g_soc_strategy_state));
	SOC_Calculate_Element.u8DSG_SOC_Int = soc_limit_dsg_u32(_soc->dsg);
	SOC_Calculate_Element.u32Cycle_times = soc_limit_cycle_u32(_soc->cycle);
	set_calsoc(_soc->soc);
	soc_sanitize_state();

	back_SOC_Calculate_Element = SOC_Calculate_Element;

	SOC_Result_Pass();
}

uint8_t Get_OpenCircuit_Value_new(uint16_t VCell)
{
	return soc_estimate_percent_from_cell_mv(VCell);
}

int8_t get_soc_from_openVol_onlyDec_new(uint16_t VCell)
{
	uint8_t result;
	uint8_t old_soc = get_soc_real();

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
#if 1
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
#endif
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
		if (s_u8_CHG200msCnt)
		{
			--s_u8_CHG200msCnt;
		}
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
	uint16_t dsg_acc;
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
		if (s_u8_DSG200msCnt)
		{
			--s_u8_DSG200msCnt;
		}
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
			dsg_acc = (uint16_t)SOC_Calculate_Element.u8DSG_SOC_Int + (uint16_t)C_change_per;
			SOC_Calculate_Element.u8DSG_SOC_Int = (uint8_t)dsg_acc;
			// SOC_Calculate_Element.u8DSG_SOC_Int += 1;
			if (SOC_Calculate_Element.u8DSG_SOC_Int >= 80)
			{
				SOC_Calculate_Element.u8DSG_SOC_Int = 0;
				if (SOC_Calculate_Element.u32Cycle_times < SOC_CYCLE_MAX)
				{
					SOC_Calculate_Element.u32Cycle_times += 1;
				}
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
	g_stCellInfoReport.SocElement.u16Soh = bms_soh_from_cycle(soc_cycle_to_u16(SOC_Calculate_Element.u32Cycle_times));

	g_stCellInfoReport.SocElement.u16CapacityFull = SOC_Calculate_Element.u32CapFull * 1 / 360;
	g_stCellInfoReport.SocElement.u16CapacityFactory = SOC_Calculate_Element.u32CapFactory * 1 / 360;
	g_stCellInfoReport.SocElement.u16Cycle_times = soc_cycle_to_u16(SOC_Calculate_Element.u32Cycle_times);
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
		if ((g_stCellInfoReport.u16VCellMin <= 2900) && (g_stCellInfoReport.u16VCellMin >= 2000))
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

	soc_strategy_update();
	// SOC_EEPROM_Deal_Monitor();
	SOC_Result_Pass();
}

#endif
