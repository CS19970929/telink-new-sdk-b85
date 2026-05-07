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

#define SOC_VIRTUAL_CURRENT_CHG (uint16_t)2 // A*10闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁?闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁?闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺岋繝宕掑Ο琛″亾閺嶎偀鍋撳鎰佹綈缂佸顦叅妞ゅ繐鎳忓▍銏ゆ⒑閸濆嫬鈧爼宕愰弽顐熷亾?闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺岋繝宕掑Ο琛″亾閺嶎偀鍋?闂傚倷娴囧▔鏇㈠窗閺囥垹绀堝┑鍌氭啞閺呮煡鐓崶銊︾叆妞?.2闂傚倷娴囧▔鏇㈠窗閺嶎厼鐭楁繛宸簻缁€宀勬煃閳轰礁鏆欐い蹇氭硾閳规垿顢欑喊鍗炲壎闂佽桨鐒﹂幑鍥ь嚕椤掑嫬围闁糕剝顨忔导鎾绘⒒娴ｈ姤纭堕柛鐘冲姍瀵憡绻濆顒傤唵?
#define SOC_VIRTUAL_CURRENT_DSG (uint16_t)2 // A*10闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁?闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁?闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺岋繝宕掑Ο琛″亾閺嶎偀鍋撳鎰佹綈缂佸顦叅妞ゅ繐鎳忓▍銏ゆ⒑閸濆嫬鈧爼宕愰弽顐熷亾?闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺岋繝宕掑Ο琛″亾閺嶎偀鍋撳顒傜Ш闁哄被鍔戦幃銏ゅ川婵犲嫪绱曢梻浣哥秺椤ユ捇宕楀鈧顐﹀箻鐠囧弶顥濋梺闈涚墕濡顢旈崼鏇熲拺閻犳亽鍔岄弸娆忊攽?闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垳鈧懓瀚晶妤呭磹闁秵鐓涢柛鎰╁妿閸╋綁鏌℃担瑙勫磳鐎殿噮鍓熸俊鍫曞幢濡ゅ﹣绱﹂梻鍌欐祰濞夋洟宕伴幇鏉垮嚑濠电姵鑹剧粻?闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺屾稑鈻庡Ο杞板嚱濡炪倖姊归崝娆撶嵁韫囨稒鍊婚柤鎭掑劜濞呫垽姊洪崫鍕偓鍫曞磹閺嶎偀鍋撳顒傜Ш鐎规洏鍨虹换婵嗩潩椤撶喐顏熼梻浣告惈閸婂爼宕愰弽顐熷亾濮橆剛绉洪柡灞诲姂閹垽宕ㄦ繝鍕磿闂備礁婀遍崗姗€藟閹捐绀夌憸鏃堝蓟閵娾晜鍋勯柣鎾抽缁堆勭箾閺夋垵鎮戦柤瑙勫劤閻ｇ兘鏌嗗鍡欏弳闂佺粯鏌ㄩ幉锟犳倶椤曗偓閺岀喐顦伴獮鐔哄殲G闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺岋繝宕掑Ο琛″亾閺嶎偀鍋撳顒傜Ш闁哄被鍔戦幃銏ゅ川婵犲嫪绱曢梻浣哥秺椤ユ捇宕楀鈧顐﹀箻鐠囧弶顥濋梺闈涚墕濡顢旈崼鏇熲拺閻犳亽鍔岄弸鎴︽煛鐎ｎ亶鐓兼鐐茬箻閹粓鎳為妷锔筋仧闂備礁鎼崐鍫曞磹閺嶎偀鍋撳顒傜Ш闁哄被鍔戦幃銏ゅ川婵犲嫪绱曢梻?

#define SOC_PERCENT_MAX                 100u
#define SOC_DSG_INT_MAX                 79u
#define SOC_CYCLE_MAX                   65535u
#define SOC_OCV_VALID_MIN_MV            2500u
#define SOC_OCV_VALID_MAX_MV            4000u
#define SOC_OCV_CELL_DELTA_MAX_MV       120u
#define SOC_OCV_RUNTIME_DIFF_THRESHOLD  5u
// #define SOC_OCV_IDLE_STABLE_TICKS       (5 * 60 * 30)
// #define SOC_OCV_IDLE_ADJUST_TICKS       (5 * 60 * 2)
#define SOC_OCV_IDLE_STABLE_TICKS       (5 * 60)
#define SOC_OCV_IDLE_ADJUST_TICKS       (5 * 60)
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

struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;		 // 闂傚倷娴囧▔鏇㈠窗閺囩喍绻嗘い鎾跺У鐎氭氨鎲告惔锝傚亾濮橆剛绉洪柡灞诲姂閹垽宕ㄦ繝鍕磿闂備礁缍婇ˉ鎾诲礂濮椻偓瀵偊骞樼拠鍙夘棟闂侀潧鐗嗗Λ妤咁敂閸撲讲鍋撻悷鏉款仹闁煎疇娉涢埢宥夊閵堝懐顔嗛梺缁樺灱婵倝寮?

enum SOC_CALI_STATE SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER; // 闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶姊洪崹顔炬啹濞撴埃鍋撻柡灞诲姂閹垽宕ㄦ繝鍕磿闂備礁缍婇ˉ鎾诲礂濮椻偓瀵偊骞樼拠鍙夘棟闂侀潧鐗嗗Λ妤咁敂閸洘鈷戦悹鎭掑妼閺嬫垿鏌＄€ｎ亶鐓兼鐐茬箻閹粓鎳為妷锔筋仧闂備礁鎼崐鍫曞磹閺嶎偀鍋撳顒傜Ш闁哄被鍔戦幃銏ゅ川婵犲嫪绱曢梻?	SOC闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺岋繝宕掑Ο琛″亾閺嶎偀鍋撳顒傜Ш闁诡喒鏅犻幊婊呭枈濡桨澹曟繛鎾村焹閸嬫捇鏌℃担瑙勫磳鐎殿噮鍓熸俊鍫曞幢濡ゅ﹣绱﹂梻鍌欐祰濞夋洟宕伴幇鏉垮嚑濠电姵鑹剧粻顖炴煟閹达絽袚闁哄懏鎮傞幃宄扳枎閹邦剛鐟ㄧ紓浣瑰劶娴滎剛鍒掗悽鍨枂闁告洦鍋掓导鎾寸節濞堝灝鏋涙い鎴濐樀瀵偊骞樼拠鍙夘棟闂侀潧鐗嗗Λ妤咁敂?

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

static uint8_t soc_apply_discharge_terminal_tracking(void)
{
	uint8_t current_soc;
	uint8_t target_soc;

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
														SOC_DSG_TERMINAL_STEP_TICKS);
			}
		return 0u;
	}

	g_soc_strategy_state.dsg_empty_lock_ticks = 0u;
	return soc_apply_step_towards_when_due(target_soc,
											0u,
											&g_soc_strategy_state.dsg_terminal_adjust_ticks,
											SOC_DSG_TERMINAL_STEP_TICKS);
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
	UINT32 C_change_per;
	static uint8_t s_u8_CHG200msCnt = 0;
	static uint8_t s_u8_Transfer200msCnt = 0;
#if 1
	if (g_stCellInfoReport.u16Ichg >= SOC_VIRTUAL_CURRENT_CHG)
	{
		// if(g_stCellInfoReport.u16Ichg > 0) {
		if (++s_u8_CHG200msCnt >= 1)
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
		{ // 闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶姊洪崹顕呭剾闁告捇绠栭弻锝夘敆婢跺﹤鈷嬮梺杞扮劍閹瑰洤顕ｉ鍕ч柛鈩冾殢娴兼捇姊绘担鑺ョ《闁哥姵鍔欏鍛婄節濮橆剛顔嗛梺缁樺灱婵倝寮查幖浣圭厸闁稿本锚閳ь剚鐗滈埀顒佽壘缂嶅﹪寮婚妸鈺傚亜闁告稑锕︽导鍕⒑瑜版帩妫戦柛蹇旓耿瀵偊骞樼拠鍙夘棟闂侀潧鐗嗗Λ妤咁敂?
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
		SOC_Calculate_Element.u8SOC_Old = get_soc_real();
		SOC_Calculate_Element.u32CapChange += (UINT32)g_stCellInfoReport.u16Ichg * 1; // As*10*100(闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺岋繝宕掑Ο琛″亾閺嶎偀鍋撳顒傜Ш鐎殿噮鍋婂畷濂稿Ψ閿旇姤顏熼梻浣告惈閸婂爼宕愰弽顐熷亾?00)
		SOC_Calculate_Element.u32CapNow += (UINT32)g_stCellInfoReport.u16Ichg * 1;	  // 闂備礁鎲￠幐鎾疾閻樿鏋侀柟鎹愵嚙濡﹢鏌曢崼婵囶棞妞ゅ繐鐖煎铏规崉閵娿儲鐎鹃梺鍝勵儏椤兘鐛箛娑欏€婚柤鎭掑劜濞呫垽姊洪崫鍕偓鍫曞磹閺嶎偀鍋撳顒佽础闁逞屽墮濠€閬嶅磻閻愬樊娓婚柛宀€鍋為悡銉╂煟閺傛寧鍟為柣蹇ｅ櫍閺岀喐顦版惔鈥冲箣闂佽桨鐒﹂幑鍥ь嚕椤掑嫬围闁糕剝顨忔导?

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
		if (++s_u8_DSG200msCnt >= 1)
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

		SOC_Calculate_Element.u8SOC_Old = get_soc_real();
		SOC_Calculate_Element.u32CapChange += (UINT32)g_stCellInfoReport.u16IDischg * 1;
		SOC_Calculate_Element.u32CapNow -= (UINT32)g_stCellInfoReport.u16IDischg * 1;

		if (SOC_Calculate_Element.u32CapNow > SOC_Calculate_Element.u32CapFull)
			SOC_Calculate_Element.u32CapNow = 0;
		C_change_per = SOC_Calculate_Element.u32CapChange * 100 / SOC_Calculate_Element.u32CapFull;
		SOC_Calculate_Element.u8SOC_Now = SOC_Calculate_Element.u8SOC_Old - C_change_per;
		if (get_soc_real() > 100)
			SOC_Calculate_Element.u8SOC_Now = 0;
		SOC_Calculate_Element.u32CapChange = (((SOC_Calculate_Element.u32CapChange * 100) % SOC_Calculate_Element.u32CapFull) + 50) / 100; // 闂傚倷娴囧▔鏇㈠窗閹版澘鍑犲┑鐘宠壘缁狀垶鏌ｉ幋锝呅撻柡鍛倐閺岋繝宕掑Ο琛″亾閺嶎偀鍋撳顒傜Ш闁哄被鍔戦幃銏ゅ川婵犲嫪绱曢梻浣哥秺椤ユ捇宕楀鈧顐﹀箻閼搁潧鐝板銈嗗姦閸撴稓绮堟径鎰拺閻犳亽鍔岄弸鎴︽煕閳轰焦鍣芥繛鎴犳暬閺屻劎鈧綆浜舵导?
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

	SOC_Result_Pass();
}

#endif
