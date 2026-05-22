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
    // 0~80 濠电姷鏁搁崑娑樜涚仦杞匡綁宕熼鐕佹綗闂佽皫鍕＜H 婵犵數鍋為崹鍫曞箰閸濄儳鐭撶痪鎯ь儍娴?
    if (cycle <= 80) {
        return 100;
    }

    // >=800 濠电姷鏁搁崑娑樜涚仦杞匡綁宕熼鐕佹綗闂佺粯顨呴悧鍡欐閻愮儤鐓曢柨鏃囶嚙楠炴鎲搁悧鍫㈡创闁?80%
    if (cycle >= 800) {
        return 80;
    }

    // 81~500 濠电姷鏁搁崑娑樜涚仦杞匡綁宕熼鐕佹綗?00 -> 90
    if (cycle <= 500) {
        // cycle=81 => 100
        // cycle=500 => 90
        uint16_t x = (uint16_t)(cycle - 80);      // 1..420
        // drop = x * 10 / 420
        uint16_t drop = (uint16_t)((uint32_t)x * 10u / 420u);
        return (uint8_t)(100u - drop);
    }

    // 501~799 濠电姷鏁搁崑娑樜涚仦杞匡綁宕熼鐕佹綗?0 -> 80
    {
        uint16_t x = (uint16_t)(cycle - 500);     // 1..299
        // drop = x * 10 / 300
        uint16_t drop = (uint16_t)((uint32_t)x * 10u / 300u);
        return (uint8_t)(90u - drop);
    }
}

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

#define SOC_VIRTUAL_CURRENT_CHG         (uint16_t)0
#define SOC_VIRTUAL_CURRENT_DSG         (uint16_t)0
#define SOC_INTEGRAL_PERIOD_MS          200u
#define SOC_INTEGRAL_MS_PER_SEC         1000u
#define SOC_CAPACITY_UNITS_PER_AH       (3600u * 10u)
#define SOC_CAPACITY_FACTORY_UNITS_PER_AH 10u
#define SOC_CAPACITY_UNITS_PER_FACTORY  (SOC_CAPACITY_UNITS_PER_AH / SOC_CAPACITY_FACTORY_UNITS_PER_AH)
#define SOC_REPORT_CAPACITY_DIVISOR     360u

#define SOC_PERCENT_MAX                 100u
#define SOC_EQUIV_CYCLE_PERCENT         100u
#define SOC_DSG_INT_MAX                 (SOC_EQUIV_CYCLE_PERCENT - 1u)
#define SOC_CYCLE_MAX                   65535u
#define SOC_OCV_VALID_MIN_MV            2000u
#define SOC_OCV_VALID_MAX_MV            4000u
#define SOC_OCV_CELL_DELTA_MAX_MV       200u
#define SOC_OCV_RUNTIME_DIFF_THRESHOLD  3u
// 放电过程 OCV 比较修正分档：电流越大，允许的修正越保守
#define SOC_DSG_OCV_LOW_CURR_MAX        20u
#define SOC_DSG_OCV_MID_CURR_MAX        50u
#define SOC_DSG_OCV_HIGH_CURR_MAX       100u
#define SOC_DSG_OCV_LOW_DIFF_THRESHOLD  1u
#define SOC_DSG_OCV_MID_DIFF_THRESHOLD  5u
#define SOC_DSG_OCV_HIGH_DIFF_THRESHOLD 8u
#define SOC_DSG_OCV_VHIGH_DIFF_THRESHOLD 12u
#define SOC_DSG_OCV_LOW_STABLE_TICKS    8u
#define SOC_DSG_OCV_MID_STABLE_TICKS    12u
#define SOC_DSG_OCV_HIGH_STABLE_TICKS   20u
#define SOC_DSG_OCV_VHIGH_STABLE_TICKS  30u
#define SOC_DSG_OCV_LOW_ADJUST_TICKS    (5 * 20)
#define SOC_DSG_OCV_MID_ADJUST_TICKS    (5 * 20)
#define SOC_DSG_OCV_HIGH_ADJUST_TICKS   (5 * 20)
#define SOC_DSG_OCV_VHIGH_ADJUST_TICKS  (5 * 20)
// #define SOC_OCV_IDLE_STABLE_TICKS       (5 * 60 * 30)
// #define SOC_OCV_IDLE_ADJUST_TICKS       (5 * 60 * 2)
#define SOC_OCV_IDLE_STABLE_TICKS       (5 * 60)
#define SOC_OCV_IDLE_ADJUST_TICKS       (5 * 60)
#define SOC_FULL_SYNC_MIN_MV            ((uint16_t)(SOC_100_VAL - 200u))
#define SOC_EMPTY_SYNC_MAX_MV           ((uint16_t)(SOC_0_VAL + 200u))
#define SOC_FULL_LOCK_TICKS             20u
#define SOC_EMPTY_LOCK_TICKS            25u
#define SOC_DSG_TERMINAL_START_MV       ((uint16_t)(SOC_0_VAL + 300u))
#define SOC_DSG_TERMINAL_L1_MV          ((uint16_t)(SOC_0_VAL + 200u))
#define SOC_DSG_TERMINAL_L2_MV          ((uint16_t)(SOC_0_VAL + 150u))
#define SOC_DSG_TERMINAL_L3_MV          ((uint16_t)(SOC_0_VAL + 50u))
#define SOC_DSG_TERMINAL_VALID_MIN_MV   2000u
#define SOC_DSG_TERMINAL_LOW_STEP_TICKS 5u
#define SOC_DSG_TERMINAL_MID_STEP_TICKS 10u
#define SOC_DSG_TERMINAL_HIGH_STEP_TICKS 20u
#define SOC_DSG_TERMINAL_VHIGH_STEP_TICKS 30u
#define SOC_DSG_EMPTY_LOCK_TICKS        10u
#define SOC_TERMINAL_SYNC_STEP_TICKS    5u

#define _CAL_SLOW_DOWN_CHG

enum SOC_CALI_STATE
{
	SOC_CALI_STATE_TRANSFER,
	SOC_CALI_CONT_CHG,
	SOC_CALI_CONT_DSG,
};

enum SOC_INTEGRAL_DIR
{
	SOC_INTEGRAL_DIR_NONE,
	SOC_INTEGRAL_DIR_CHG,
	SOC_INTEGRAL_DIR_DSG,
};

struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;		 // 闂傚倷娴囧▔鏇㈠窗閺囩喍绻嗘い鎾跺У鐎氭氨鎲告惔锝傚亾濮橆剛绉洪柡灞诲姂閹垽宕ㄦ繝鍕磿闂備礁缍婇ˉ鎾诲礂濮椻偓瀵偊骞樼拠鍙夘棟闂侀潧鐗嗗Λ妤咁敂閸撲讲鍋撻悷鏉款仹闁煎疇娉涢埢宥夊閵堝懐顔嗛梺缁樺灱婵倝寮?

enum SOC_CALI_STATE SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER; // 闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶姊洪崹顔炬啹濞撴埃鍋撻柡灞诲姂閹垽宕ㄦ繝鍕磿闂備礁缍婇ˉ鎾诲礂濮椻偓瀵偊骞樼拠鍙夘棟闂侀潧鐗嗗Λ妤咁敂閸洘鈷戦悹鎭掑妼閺嬫垿鏌＄€ｎ亶鐓兼鐐茬箻閹粓鎳為妷锔筋仧闂備礁鎼崐鍫曞磹閺嶎偀鍋撳顒傜Ш闁哄被鍔戦幃銏ゅ川婵犲嫪绱曢梻?	SOC闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺岋繝宕掑Ο琛″亾閺嶎偀鍋撳顒傜Ш闁诡喒鏅犻幊婊呭枈濡桨澹曟繛鎾村焹閸嬫捇鏌℃担瑙勫磳鐎殿噮鍓熸俊鍫曞幢濡ゅ﹣绱﹂梻鍌欐祰濞夋洟宕伴幇鏉垮嚑濠电姵鑹剧粻顖炴煟閹达絽袚闁哄懏鎮傞幃宄扳枎閹邦剛鐟ㄧ紓浣瑰劶娴滎剛鍒掗悽鍨枂闁告洦鍋掓导鎾寸節濞堝灝鏋涙い鎴濐樀瀵偊骞樼拠鍙夘棟闂侀潧鐗嗗Λ妤咁敂?

typedef struct
{
	uint16_t mv;
	uint8_t soc;
} soc_ocv_point_t;

static const soc_ocv_point_t g_soc_ocv_points[] = {
	{SOC_0_VAL, 0u},
	{3200u, 5u},
	{3300u, 10u},
	{3400u, 15u},
	{3500u, 25u},
	{3600u, 35u},
	{3650u, 45u},
	{3700u, 55u},
	{3750u, 65u},
	{3800u, 75u},
	{3900u, 85u},
	{4000u, 92u},
	{4100u, 97u},
	{SOC_100_VAL, 100u},
};

typedef struct
{
	uint16_t idle_stable_ticks;
	uint16_t idle_adjust_ticks;
	uint16_t dsg_ocv_stable_ticks;
	uint16_t dsg_ocv_adjust_ticks;
	uint8_t startup_checked;
	uint8_t full_lock_ticks;
	uint8_t empty_lock_ticks;
	uint8_t full_terminal_adjust_ticks;
	uint8_t empty_terminal_adjust_ticks;
	uint8_t dsg_terminal_adjust_ticks;
	uint8_t dsg_empty_lock_ticks;
	uint8_t dsg_ocv_band;
} soc_strategy_state_t;

static soc_strategy_state_t g_soc_strategy_state;
static enum SOC_INTEGRAL_DIR g_soc_integral_dir = SOC_INTEGRAL_DIR_NONE;
static uint16_t g_soc_integral_ms_remainder = 0u;



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
	SOC_Calculate_Element.u32CapFactory = (UINT32)CapacityFactory * SOC_CAPACITY_UNITS_PER_FACTORY;
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
	g_soc_integral_dir = SOC_INTEGRAL_DIR_NONE;
	g_soc_integral_ms_remainder = 0u;
}

static void soc_integral_select_dir(enum SOC_INTEGRAL_DIR dir)
{
	if (g_soc_integral_dir != dir)
	{
		g_soc_integral_dir = dir;
		g_soc_integral_ms_remainder = 0u;
		SOC_Calculate_Element.u32CapChange = 0u;
	}
}

static uint32_t soc_integral_delta_from_current(uint16_t current, enum SOC_INTEGRAL_DIR dir)
{
	uint32_t sum;

	if (current == 0u)
	{
		return 0u;
	}

	soc_integral_select_dir(dir);
	sum = ((uint32_t)current * SOC_INTEGRAL_PERIOD_MS) + (uint32_t)g_soc_integral_ms_remainder;
	g_soc_integral_ms_remainder = (uint16_t)(sum % SOC_INTEGRAL_MS_PER_SEC);
	return sum / SOC_INTEGRAL_MS_PER_SEC;
}

static uint8_t soc_percent_from_capacity_charge(uint32_t cap)
{
	if (cap >= SOC_Calculate_Element.u32CapFull)
	{
		return SOC_PERCENT_MAX;
	}
	return soc_limit_percent_u32((cap * SOC_PERCENT_MAX) / SOC_Calculate_Element.u32CapFull);
}

static uint8_t soc_percent_from_capacity_discharge(uint32_t cap)
{
	uint32_t percent;

	if (cap == 0u)
	{
		return 0u;
	}
	if (cap >= SOC_Calculate_Element.u32CapFull)
	{
		return SOC_PERCENT_MAX;
	}

	percent = ((cap * SOC_PERCENT_MAX) + SOC_Calculate_Element.u32CapFull - 1u) / SOC_Calculate_Element.u32CapFull;
	return soc_limit_percent_u32(percent);
}

static void soc_note_discharge_soc_drop(uint8_t old_soc, uint8_t new_soc)
{
	uint16_t dsg_acc;
	uint8_t cycle_changed = 0u;

	if (old_soc <= new_soc)
	{
		return;
	}

	dsg_acc = (uint16_t)SOC_Calculate_Element.u8DSG_SOC_Int + (uint16_t)(old_soc - new_soc);
	while (dsg_acc >= SOC_EQUIV_CYCLE_PERCENT)
	{
		dsg_acc -= SOC_EQUIV_CYCLE_PERCENT;
		if (SOC_Calculate_Element.u32Cycle_times < SOC_CYCLE_MAX)
		{
			SOC_Calculate_Element.u32Cycle_times += 1u;
			cycle_changed = 1u;
		}
	}
	SOC_Calculate_Element.u8DSG_SOC_Int = (uint8_t)dsg_acc;
	if (cycle_changed)
	{
		soc_recalc_full_capacity();
		soc_recalc_now_capacity();
	}
}

static void soc_apply_integral_delta(enum SOC_INTEGRAL_DIR dir, uint32_t delta)
{
	uint8_t old_soc;
	uint8_t new_soc;

	if (delta == 0u)
	{
		return;
	}

	old_soc = get_soc_real();
	SOC_Calculate_Element.u32CapChange += delta;
	if (dir == SOC_INTEGRAL_DIR_CHG)
	{
		if ((SOC_Calculate_Element.u32CapNow >= SOC_Calculate_Element.u32CapFull) ||
			((SOC_Calculate_Element.u32CapFull - SOC_Calculate_Element.u32CapNow) <= delta))
		{
			SOC_Calculate_Element.u32CapNow = SOC_Calculate_Element.u32CapFull;
		}
		else
		{
			SOC_Calculate_Element.u32CapNow += delta;
		}

		new_soc = soc_percent_from_capacity_charge(SOC_Calculate_Element.u32CapNow);
		if (new_soc > old_soc)
		{
			SOC_Calculate_Element.u8SOC_Now = new_soc;
			SOC_Calculate_Element.u32CapChange = 0u;
		}
		else if (SOC_Calculate_Element.u32CapNow >= SOC_Calculate_Element.u32CapFull)
		{
			SOC_Calculate_Element.u32CapChange = 0u;
		}
	}
	else if (dir == SOC_INTEGRAL_DIR_DSG)
	{
		if (SOC_Calculate_Element.u32CapNow <= delta)
		{
			SOC_Calculate_Element.u32CapNow = 0u;
		}
		else
		{
			SOC_Calculate_Element.u32CapNow -= delta;
		}

		new_soc = soc_percent_from_capacity_discharge(SOC_Calculate_Element.u32CapNow);
		if ((new_soc == 0u) && (old_soc > 0u) && (VCELLMIN > SOC_0_VAL))
		{
			SOC_Calculate_Element.u32CapNow = (SOC_Calculate_Element.u32CapFull + SOC_PERCENT_MAX - 1u) / SOC_PERCENT_MAX;
			new_soc = 1u;
		}
		if (new_soc < old_soc)
		{
			SOC_Calculate_Element.u8SOC_Now = new_soc;
			SOC_Calculate_Element.u32CapChange = 0u;
			soc_note_discharge_soc_drop(old_soc, new_soc);
		}
		else if (SOC_Calculate_Element.u32CapNow == 0u)
		{
			SOC_Calculate_Element.u32CapChange = 0u;
		}
	}
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

static uint8_t soc_apply_step_towards_value(uint8_t target_soc, uint8_t allow_raise, uint8_t sync_display)
{
	uint8_t current_soc;

	target_soc = soc_limit_percent_u32(target_soc);
	current_soc = get_soc_real();
	if (current_soc == target_soc)
	{
		return 0u;
	}

	if ((current_soc < target_soc) && !allow_raise)
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

static uint8_t soc_apply_step_towards_when_due(uint8_t target_soc, uint8_t allow_raise, uint8_t *ticks, uint8_t step_ticks)
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
	if (soc_apply_step_towards_value(target_soc, allow_raise, 1u))
	{
		soc_reset_integral_accumulator();
		return 1u;
	}

	return 0u;
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
	return !isCHG() && !isDSG();
}

static uint8_t soc_estimate_percent_from_cell_mv(uint16_t cell_mv)
{
	uint8_t i;
	const soc_ocv_point_t *lo;
	const soc_ocv_point_t *hi;
	uint32_t numerator;

	if (cell_mv <= g_soc_ocv_points[0].mv)
	{
		return g_soc_ocv_points[0].soc;
	}

	if (cell_mv >= g_soc_ocv_points[(sizeof(g_soc_ocv_points) / sizeof(g_soc_ocv_points[0])) - 1u].mv)
	{
		return SOC_PERCENT_MAX;
	}

	for (i = 1u; i < (uint8_t)(sizeof(g_soc_ocv_points) / sizeof(g_soc_ocv_points[0])); ++i)
	{
		if (cell_mv <= g_soc_ocv_points[i].mv)
		{
			lo = &g_soc_ocv_points[i - 1u];
			hi = &g_soc_ocv_points[i];
			numerator = ((uint32_t)(cell_mv - lo->mv) * (uint32_t)(hi->soc - lo->soc)) +
						((uint32_t)(hi->mv - lo->mv) / 2u);
			return (uint8_t)(lo->soc + (numerator / (uint32_t)(hi->mv - lo->mv)));
		}
	}

	return SOC_PERCENT_MAX;
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

static uint8_t soc_estimate_ocv_percent(void)
{
	uint16_t weighted_mv;

	weighted_mv = (uint16_t)((((uint32_t)VCELLMIN * 3u) + (uint32_t)VCELLMAX) / 4u);
	return soc_estimate_percent_from_cell_mv(weighted_mv);
}

static uint8_t soc_discharge_ocv_current_band(uint16_t dsg_current)
{
	if (dsg_current <= SOC_DSG_OCV_LOW_CURR_MAX)
	{
		return 0u;
	}

	if (dsg_current <= SOC_DSG_OCV_MID_CURR_MAX)
	{
		return 1u;
	}

	if (dsg_current <= SOC_DSG_OCV_HIGH_CURR_MAX)
	{
		return 2u;
	}

	return 3u;
}

static uint8_t soc_discharge_ocv_diff_threshold(uint8_t band)
{
	switch (band)
	{
	case 0u:
		return SOC_DSG_OCV_LOW_DIFF_THRESHOLD;
	case 1u:
		return SOC_DSG_OCV_MID_DIFF_THRESHOLD;
	case 2u:
		return SOC_DSG_OCV_HIGH_DIFF_THRESHOLD;
	default:
		return SOC_DSG_OCV_VHIGH_DIFF_THRESHOLD;
	}
}

static uint8_t soc_discharge_ocv_stable_ticks(uint8_t band)
{
	switch (band)
	{
	case 0u:
		return SOC_DSG_OCV_LOW_STABLE_TICKS;
	case 1u:
		return SOC_DSG_OCV_MID_STABLE_TICKS;
	case 2u:
		return SOC_DSG_OCV_HIGH_STABLE_TICKS;
	default:
		return SOC_DSG_OCV_VHIGH_STABLE_TICKS;
	}
}

static uint8_t soc_discharge_ocv_adjust_ticks(uint8_t band)
{
	switch (band)
	{
	case 0u:
		return SOC_DSG_OCV_LOW_ADJUST_TICKS;
	case 1u:
		return SOC_DSG_OCV_MID_ADJUST_TICKS;
	case 2u:
		return SOC_DSG_OCV_HIGH_ADJUST_TICKS;
	default:
		return SOC_DSG_OCV_VHIGH_ADJUST_TICKS;
	}
}

static uint8_t soc_apply_startup_ocv_correction(void)
{
	uint8_t current_soc;
	uint8_t ocv_soc;

	if (g_soc_strategy_state.startup_checked)
	{
		return 0u;
	}

	if (!soc_ocv_sample_valid() || !soc_idle_for_ocv())
	{
		return 0u;
	}

	g_soc_strategy_state.startup_checked = 1u;
	current_soc = get_soc_real();
	ocv_soc = soc_estimate_ocv_percent();

	if ((ocv_soc == 0u) && (VCELLMIN <= SOC_0_VAL) && (current_soc > 0u))
	{
		return soc_apply_step_towards_value(0u, 0u, 1u);
	}

	return 0u;
}

static uint8_t soc_apply_discharge_ocv_tracking(void)
{
	uint8_t current_soc;
	uint8_t ocv_soc;
	uint8_t diff;
	uint8_t band;
	uint8_t diff_threshold;
	uint8_t stable_limit;
	uint8_t adjust_limit;

	if (!isDSG())
	{
		g_soc_strategy_state.dsg_ocv_stable_ticks = 0u;
		g_soc_strategy_state.dsg_ocv_adjust_ticks = 0u;
		g_soc_strategy_state.dsg_ocv_band = 0xFFu;
		return 0u;
	}

	if (!soc_ocv_sample_valid())
	{
		g_soc_strategy_state.dsg_ocv_stable_ticks = 0u;
		g_soc_strategy_state.dsg_ocv_adjust_ticks = 0u;
		g_soc_strategy_state.dsg_ocv_band = 0xFFu;
		return 0u;
	}

	band = soc_discharge_ocv_current_band(IDSG);
	diff_threshold = soc_discharge_ocv_diff_threshold(band);
	stable_limit = soc_discharge_ocv_stable_ticks(band);
	adjust_limit = soc_discharge_ocv_adjust_ticks(band);

	if (g_soc_strategy_state.dsg_ocv_band != band)
	{
		g_soc_strategy_state.dsg_ocv_band = band;
		g_soc_strategy_state.dsg_ocv_stable_ticks = 0u;
		g_soc_strategy_state.dsg_ocv_adjust_ticks = 0u;
	}

	if (g_soc_strategy_state.dsg_ocv_stable_ticks < stable_limit)
	{
		g_soc_strategy_state.dsg_ocv_stable_ticks++;
		g_soc_strategy_state.dsg_ocv_adjust_ticks = 0u;
		return 0u;
	}

	if (++g_soc_strategy_state.dsg_ocv_adjust_ticks < adjust_limit)
	{
		return 0u;
	}
	g_soc_strategy_state.dsg_ocv_adjust_ticks = 0u;

	current_soc = get_soc_real();
	ocv_soc = soc_estimate_ocv_percent();

	if (ocv_soc >= current_soc)
	{
		g_soc_strategy_state.dsg_ocv_adjust_ticks = 0u;
		return 0u;
	}

	diff = (uint8_t)(current_soc - ocv_soc);
	if (diff < diff_threshold)
	{
		g_soc_strategy_state.dsg_ocv_adjust_ticks = 0u;
		return 0u;
	}

	if (soc_apply_step_towards_value(ocv_soc, 0u, 1u))
	{
		soc_reset_integral_accumulator();
		return 1u;
	}

	return 0u;
}

static uint8_t soc_apply_idle_ocv_tracking(void)
{
	uint8_t current_soc;
	uint8_t ocv_soc;
	uint8_t diff;

	if (!soc_idle_for_ocv() || !soc_ocv_sample_valid())
	{
		g_soc_strategy_state.idle_stable_ticks = 0u;
		g_soc_strategy_state.idle_adjust_ticks = 0u;
		return 0u;
	}

	if (g_soc_strategy_state.idle_stable_ticks < SOC_OCV_IDLE_STABLE_TICKS)
	{
		g_soc_strategy_state.idle_stable_ticks++;
		g_soc_strategy_state.idle_adjust_ticks = 0u;
		return 0u;
	}

	if (++g_soc_strategy_state.idle_adjust_ticks < SOC_OCV_IDLE_ADJUST_TICKS)
	{
		return 0u;
	}
	g_soc_strategy_state.idle_adjust_ticks = 0u;

	current_soc = get_soc_real();
	ocv_soc = soc_estimate_ocv_percent();

	if (ocv_soc >= current_soc)
	{
		diff = (uint8_t)(ocv_soc - current_soc);
	}
	else
	{
		diff = (uint8_t)(current_soc - ocv_soc);
	}

	if ((diff < SOC_OCV_RUNTIME_DIFF_THRESHOLD) || (ocv_soc >= current_soc))
	{
		return 0u;
	}

	if (soc_apply_step_towards_value(ocv_soc, 0u, 1u))
	{
		soc_reset_integral_accumulator();
		return 1u;
	}

	return 0u;
}

static uint8_t soc_apply_terminal_sync(void)
{
	if (isCHG() && (VCELLMAX >= SOC_100_VAL) && (VCELLMIN >= SOC_FULL_SYNC_MIN_MV))
	{
		if (g_soc_strategy_state.full_lock_ticks < SOC_FULL_LOCK_TICKS)
		{
			g_soc_strategy_state.full_lock_ticks++;
		}
		if (g_soc_strategy_state.full_lock_ticks >= SOC_FULL_LOCK_TICKS)
		{
			return soc_apply_step_towards_when_due(SOC_PERCENT_MAX,
													1u,
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
			return soc_apply_step_towards_when_due(0u,
													0u,
													&g_soc_strategy_state.empty_terminal_adjust_ticks,
													SOC_TERMINAL_SYNC_STEP_TICKS);
		}
	}
	else
	{
		g_soc_strategy_state.empty_lock_ticks = 0u;
		g_soc_strategy_state.empty_terminal_adjust_ticks = 0u;
	}

	return 0u;
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

static uint8_t soc_discharge_terminal_step_ticks(void)
{
	switch (soc_discharge_ocv_current_band(IDSG))
	{
	case 0u:
		return SOC_DSG_TERMINAL_LOW_STEP_TICKS;
	case 1u:
		return SOC_DSG_TERMINAL_MID_STEP_TICKS;
	case 2u:
		return SOC_DSG_TERMINAL_HIGH_STEP_TICKS;
	default:
		return SOC_DSG_TERMINAL_VHIGH_STEP_TICKS;
	}
}

static uint8_t soc_apply_discharge_terminal_tracking(void)
{
	uint8_t current_soc;
	uint8_t target_soc;
	uint8_t step_ticks;

	if (!isDSG() || (VCELLMAX < VCELLMIN) || (VCELLMIN < SOC_DSG_TERMINAL_VALID_MIN_MV))
	{
		g_soc_strategy_state.dsg_terminal_adjust_ticks = 0u;
		g_soc_strategy_state.dsg_empty_lock_ticks = 0u;
		return 0u;
	}

	target_soc = soc_discharge_terminal_soc_ceiling();
	if (target_soc >= SOC_PERCENT_MAX)
	{
		g_soc_strategy_state.dsg_terminal_adjust_ticks = 0u;
		g_soc_strategy_state.dsg_empty_lock_ticks = 0u;
		return 0u;
	}

	current_soc = get_soc_real();
	if (current_soc <= target_soc)
	{
		g_soc_strategy_state.dsg_terminal_adjust_ticks = 0u;
		return 0u;
	}

	step_ticks = soc_discharge_terminal_step_ticks();
	if (target_soc == 0u)
	{
		if (g_soc_strategy_state.dsg_empty_lock_ticks < SOC_DSG_EMPTY_LOCK_TICKS)
		{
			g_soc_strategy_state.dsg_empty_lock_ticks++;
		}
		if (g_soc_strategy_state.dsg_empty_lock_ticks >= SOC_DSG_EMPTY_LOCK_TICKS)
		{
			return soc_apply_step_towards_when_due(0u,
													0u,
													&g_soc_strategy_state.dsg_terminal_adjust_ticks,
													step_ticks);
		}
		return 0u;
	}

	g_soc_strategy_state.dsg_empty_lock_ticks = 0u;
	return soc_apply_step_towards_when_due(target_soc,
											0u,
											&g_soc_strategy_state.dsg_terminal_adjust_ticks,
											step_ticks);
}

static void soc_strategy_update(void)
{
	if (soc_apply_startup_ocv_correction())
	{
		return;
	}

	if (soc_apply_terminal_sync())
	{
		return;
	}

	if (soc_apply_discharge_terminal_tracking())
	{
		return;
	}

	if (soc_apply_discharge_ocv_tracking())
	{
		return;
	}

	(void)soc_apply_idle_ocv_tracking();
}

void set_calsoc(uint8_t _soc)
{
	SOC_Calculate_Element.u8SOC_Now = soc_limit_percent_u32(_soc);
	soc_recalc_full_capacity();
	soc_recalc_now_capacity();
}

void set_soc_param(uint8_t _soc_val, uint16_t _cap_factory, uint8_t disp_sync_updatae)
{
	(void)_cap_factory;

	{
		// soc_calculate.u32CapFactory = (UINT32)g_tParam.other.u16Soc_Ah * SOC_CAPACITY_UNITS_PER_AH;
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

#endif
}

void soc_param_lib_init(soc_kv_data_t *_soc)
{
	memset(&g_soc_strategy_state, 0, sizeof(g_soc_strategy_state));
	SOC_Calculate_Element.u8DSG_SOC_Int = soc_limit_dsg_u32(_soc->dsg);
	SOC_Calculate_Element.u32Cycle_times = soc_limit_cycle_u32(_soc->cycle);
	set_calsoc(_soc->soc);
	soc_sanitize_state();


	SOC_Result_Pass();
}

void SOC_Cont_AH_Int_CHG(void)
{
	uint32_t delta;

	if (!isCHG())
	{
		SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 0u;
		return;
	}

	SOC_Cali_Flag = SOC_CALI_CONT_CHG;
	SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 1u;
	SOC_Calculate_Element.u8SOC_Old = get_soc_real();
	delta = soc_integral_delta_from_current(g_stCellInfoReport.u16Ichg, SOC_INTEGRAL_DIR_CHG);
	soc_apply_integral_delta(SOC_INTEGRAL_DIR_CHG, delta);
	SOC_Calculate_Element.u32CapFull_Cal_As += delta;
	SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 0u;
}

void SOC_Cont_AH_Int_DSG(void)
{
	uint32_t delta;

	if (!isDSG())
	{
		SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 0u;
		return;
	}

	SOC_Cali_Flag = SOC_CALI_CONT_DSG;
	SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 1u;
	SOC_Calculate_Element.u8SOC_Old = get_soc_real();
	delta = soc_integral_delta_from_current(g_stCellInfoReport.u16IDischg, SOC_INTEGRAL_DIR_DSG);
	soc_apply_integral_delta(SOC_INTEGRAL_DIR_DSG, delta);
	SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 0u;
}

void SOC_State_Transfer(void)
{
	if (isCHG())
	{
		SOC_Cali_Flag = SOC_CALI_CONT_CHG;
	}
	else if (isDSG())
	{
		SOC_Cali_Flag = SOC_CALI_CONT_DSG;
	}
	else
	{
		SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER;
		soc_reset_integral_accumulator();
	}
}


void SOC_Result_Pass(void)
{
#ifndef _DOUBLE_SOC_FUNC_
	g_stCellInfoReport.SocElement.u16Soc = get_soc_real();
	g_stCellInfoReport.SocElement.u16CapacityNow = SOC_Calculate_Element.u32CapNow / SOC_REPORT_CAPACITY_DIVISOR;
#else
	g_stCellInfoReport.real_now_Capacity = SOC_Calculate_Element.u32CapNow / SOC_REPORT_CAPACITY_DIVISOR;
	#endif
	// g_stCellInfoReport.SocElement.u16Soh = 100;
	g_stCellInfoReport.SocElement.u16Soh = bms_soh_from_cycle(soc_cycle_to_u16(SOC_Calculate_Element.u32Cycle_times));

	g_stCellInfoReport.SocElement.u16CapacityFull = SOC_Calculate_Element.u32CapFull / SOC_REPORT_CAPACITY_DIVISOR;
	g_stCellInfoReport.SocElement.u16CapacityFactory = SOC_Calculate_Element.u32CapFactory / SOC_REPORT_CAPACITY_DIVISOR;
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
		//!!! 闂佽绻愮换鎰崲濮椻偓瀵偊骞樼拠鍙夘棟闂侀潧鐗嗗Λ妤咁敂閸洘鈷戦悹鎭掑妼閺嬫垿鏌＄€ｎ亶鐓兼鐐茬箰閻ｏ繝骞嶉崘韫婵犻潧鍊搁幉锟犲疾缁嬭￥鈧帒顫濋妷銉ュБ濡炪倧缍嗛崳锝夌嵁韫囨稒鍊婚柤鎭掑劜濞呫垽姊洪崫鍕偓鍫曞磹閺嶎偀鍋撳顒傜Ш闁哄被鍔戦幃銏ゅ川婵犲嫪绱曢梻浣烘嚀閸㈡煡宕查弻銉﹀仾闁告洦鍨扮猾宥夋煠閸濄儲鏆╁褝绻濋弻娑㈠箣濠靛浂妫﹂梺杞扮劍閹瑰洤顕ｉ鍕ч柛鈩冾殢娴兼捇姊绘担鑺ョ《闁哥姵鍔欏鍛婄節濮橆剛顔嗙紓浣告湰缁硿闂傚倷娴囧▔鏇㈠窗閺囩姵顐芥繝闈涚墛鐎氭氨鎲告惔锝傚亾濮橆剛绉虹€?闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垰顪冪€ｎ亞宀搁柍褜鍓氶敃銏ゅ蓟閵娾晜鍋勯柤濮愬€楅悰鈺呮⒑閹稿海绠撶紒缁樺灩閳ь剚鑹剧紞濠囧蓟閵娾晜鍋勯柤濮愬€楅悰鈺呮煟閻樿鲸绁版い顐㈩槺閳ь剚鑹剧紞濠囧蓟閵娾晜鍋勭紒瀣硶娴滅増绻涢幋鐐村碍缂佸缍婂顐﹀箻鐠囧弶顥濋梺闈涚墕濡顢旈崼鏇熺厱?闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺岋繝宕掑Ο琛″亾閺嶎偀鍋撳顒傜Ш闁哄被鍔戦幃銏ゅ川婵犲嫪绱曢梻浣哥秺椤ユ捇宕楀鈧顐﹀箻閼搁潧鏋傞梺鍦劋閸ㄧ數鑺辨繝姘厵?
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
			// 3闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垳鈧懓瀚竟鍡椥掗崼銉︾厸闁告劑鍔庨崺锝夋煛娴ｈ宕岀€殿噮鍓熸俊鍫曞幢濡ゅ﹣绱﹂梻鍌欐祰濞夋洟宕伴幇鏉垮嚑濠电姵鑹剧粻顖炴煟閹达絽袚闁哄懏鎮傞弻锟犲磼濡　鍋撻弽顐熷亾?
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
#endif

void APP_SOC_IntEnhance_Ctrl()
{
	if (isCHG())
	{
		SOC_Cont_AH_Int_CHG();
	}
	else if (isDSG())
	{
		SOC_Cont_AH_Int_DSG();
	}
	else
	{
		SOC_State_Transfer();
	}

	soc_strategy_update();

	SOC_Result_Pass();
}
