/*
*********************************************************************************************************
*
*	妯″潡鍚嶇О : 搴旂敤绋嬪簭鍙傛暟妯″潡
*	鏂囦欢鍚嶇О : param.h
*	鐗�    鏈� : V1.0
*	璇�    鏄� : 澶存枃浠�
*
*	Copyright (C), 2012-2013, 瀹夊瘜鑾辩數瀛� www.armfly.com
*
*********************************************************************************************************
*/

#ifndef __PARAM_H
#define __PARAM_H

#include "flash_store_cfg.h"
// #include "types.h"
#include "conf.h"

//typedef u16 u16;
#define SNum (4)

/* 涓嬮潰2琛屽畯鍙兘閫夋嫨鍏朵竴 */
// #define PARAM_SAVE_TO_EEPROM			/* 鍙傛暟瀛樺偍鍒板閮ㄧ殑EEPROM (AT24C128) */
#define PARAM_SAVE_TO_FLASH		/* 鍙傛暟瀛樺偍鍒癈PU鍐呴儴Flash */

#ifdef PARAM_SAVE_TO_EEPROM
	#define PARAM_ADDR		0			/* 鍙傛暟鍖哄湴鍧� */
#endif

#ifdef PARAM_SAVE_TO_FLASH
	#define PARAM_ADDR		 FLASH_ADDR_SOFT_PROTECT_BASE			/* 0x0800C000 涓棿鐨�16KB鎵囧尯鐢ㄦ潵瀛樻斁鍙傛暟 */
#endif

#define PARAM_VER			0xfff0					/* 鍙傛暟鐗堟湰 */

struct PRT_E2ROM_PARAS {
//--------------parameters store sequence and its address allocation-----------
	u16	u16VcellOvp_First;
	u16	u16VcellOvp_Second;
	u16	u16VcellOvp_Third;
	u16	u16VcellOvp_Rcv;
	u16	u16VcellOvp_Filter;

	u16	u16VcellUvp_First;
	u16	u16VcellUvp_Second;
	u16	u16VcellUvp_Third;
	u16	u16VcellUvp_Rcv;
	u16	u16VcellUvp_Filter;
	
	u16	u16VbusOvp_First;
	u16	u16VbusOvp_Second;
	u16	u16VbusOvp_Third;
	u16	u16VbusOvp_Rcv;
	u16	u16VbusOvp_Filter;

	u16	u16VbusUvp_First;
	u16	u16VbusUvp_Second;
	u16	u16VbusUvp_Third;
	u16	u16VbusUvp_Rcv;	
	u16	u16VbusUvp_Filter;

	u16	u16IchgOcp_First;
	u16	u16IchgOcp_Second;
	u16	u16IchgOcp_Third;
	u16	u16IchgOcp_Rcv;
	u16	u16IchgOcp_Filter;

	u16	u16IdsgOcp_First;
	u16	u16IdsgOcp_Second;
	u16	u16IdsgOcp_Third;
	u16	u16IdsgOcp_Rcv;
	u16	u16IdsgOcp_Filter;
	
	u16	u16TChgOTp_First;
	u16	u16TChgOTp_Second;
	u16	u16TChgOTp_Third;
	u16	u16TChgOTp_Rcv;
	u16	u16TChgOTp_Filter;

	u16	u16TchgUTp_First;
	u16	u16TchgUTp_Second;
	u16	u16TchgUTp_Third;
	u16	u16TchgUTp_Rcv;
	u16	u16TchgUTp_Filter;

	u16	u16TdischgOTp_First;
	u16	u16TdischgOTp_Second;
	u16	u16TdischgOTp_Third;
	u16	u16TdischgOTp_Rcv;
	u16	u16TdischgOTp_Filter;

	u16	u16TdischgUTp_First;
	u16	u16TdischgUTp_Second;
	u16	u16TdischgUTp_Third;
	u16	u16TdischgUTp_Rcv;
	u16  u16TdischgUTp_Filter;

	u16	u16TmosOTp_First;
	u16	u16TmosOTp_Second;
	u16	u16TmosOTp_Third;
	u16	u16TmosOTp_Rcv;
	u16	u16TmosOTp_Filter;

	u16	u16VdeltaOvp_First;
	u16	u16VdeltaOvp_Second;
	u16	u16VdeltaOvp_Third;
	u16	u16VdeltaOvp_Rcv;
	u16	u16VdeltaOvp_Filter;

	u16	u16SocUp_First;
	u16	u16SocUp_Second;
	u16	u16SocUp_Third;
	u16	u16SocUp_Rcv;
	u16	u16SocUp_Filter;
};

#define COV_1           3750
#define COV_2           3750
#define COV_3           3750
#define COV_recover     3500
#define COV_filter1      100
#define COV_filter2     100
#define COV_filter3     100

#define CUV_1           3000
#define CUV_2           3000
#define CUV_3           3000
#define CUV_recover     3100
#define CUV_filter1      100
#define CUV_filter2     100
#define CUV_filter3     1000

#define BOV_1           (350 * SNum)
#define BOV_2           (360 * SNum)
#define BOV_3           (365 * SNum)
#define BOV_recover     (350 * SNum)
// #define BOV_1           (2800)
// #define BOV_2           (2920)
// #define BOV_3           (3000)
// #define BOV_recover     (2800)
#define BOV_filter1      100 
#define BOV_filter2     100 
#define BOV_filter3     100 


#define BUV_1           (300 * SNum)
#define BUV_2           (300 * SNum)
#define BUV_3           (290 * SNum)
#define BUV_recover     (300 * SNum)
#define BUV_filter1      100 
#define BUV_filter2     100 
#define BUV_filter3     100 


#define OTC_1           ((40 + 40) * 10)
#define OTC_2           ((50 + 40) * 10)
#define OTC_3           ((55 + 40) * 10)
#define OTC_recover     ((50 + 40) * 10)
#define OTC_filter1       100
#define OTC_filter2      100
#define OTC_filter3      100

#define UTC_1           ((5 + 40) * 10)
#define UTC_2           ((3 + 40) * 10)
#ifdef __FUNC__HEAT__
#if (AFE_TYPE == sh36xx)
#define UTC_3           ((-20 + 40) * 10)
#elif (AFE_TYPE == bq76xx_afe)
#define UTC_3           ((-28 + 40) * 10)
#endif
#else
#define UTC_3           ((0 + 40) * 10)
#endif // DEBUG
#define UTC_recover     ((3 + 40) * 10)
#define UTC_filter1      100
#define UTC_filter2      100
#define UTC_filter3      100

#define OTD_1           ((50 + 40) * 10)
#define OTD_2           ((50 + 40) * 10)
#define OTD_3           ((60 + 40) * 10)
#define OTD_recover     ((50 + 40) * 10)
#define OTD_filter1      100
#define OTD_filter2      100
#define OTD_filter3      100

#define UTD_1           ((-10 + 40) * 10)
#define UTD_2           ((-15 + 40) * 10)
#define UTD_3           ((-20 + 40) * 10)
#define UTD_recover     ((-10 + 40) * 10)
#define UTD_filter1      100
#define UTD_filter2      100
#define UTD_filter3      100

#define mos_1           ((75 + 40) * 10)
#define mos_2           ((85 + 40) * 10)
#define mos_3           ((95 + 40) * 10)
#define mos_recover     ((80 + 40) * 10)
#define mos_filter1      100
#define mos_filter2      100
#define mos_filter3      100

#define VDELTER_1       600
#define VDELTER_2       800
#define VDELTER_3       1000
#define VDELTER_recover 800
#define VDELTER_filter1  100
#define VDELTER_filter2  100
#define VDELTER_filter3  100

#define socLow_1        20
#define socLow_2        10
#define socLow_3        5
#define socLow_recover  11
#define socLow_filter1   100
#define socLow_filter2   100
#define socLow_filter3   100

#define OCC_1       (100) 
#define OCC_2       (150) 
#define OCC_3       (200) 
#define OCC_recover (100) 
#define OCC_filter1  (100 * 5) 
#define OCC_filter2  (100 * 5) 
#define OCC_filter3  10 

#define ODC_1       (100) 
#define ODC_2       (150) 
#define ODC_3       (200) 
#define ODC_recover (100) 
#define ODC_filter1  (100 * 5) 
#define ODC_filter2  (100 * 5) 
#define ODC_filter3  10

#define E2P_PROTECT_DEFAULT_PRT	{/*鍗曡妭杩囧帇*/COV_1,	COV_2,	COV_3,	COV_recover,	COV_filter3,\
								 /*鍗曡妭浣庡帇*/CUV_1,	CUV_2,	CUV_3,	CUV_recover,	CUV_filter3,\
								 /*鎬诲帇杩囧帇*/BOV_1, BOV_2,	BOV_3,  BOV_recover, 	BOV_filter3,\
								 /*鎬诲帇浣庡帇*/BUV_1, BUV_2,	BUV_3,  BUV_recover, 	BUV_filter3,\
		        				 /*鍏呯數杩囨祦*/OCC_1,	OCC_2,	OCC_3,	OCC_recover,	OCC_filter3,\
		        				 /*鏀剧數杩囨祦*/ODC_1,	ODC_2,	ODC_3,	ODC_recover,	ODC_filter3,\
								 /*鍏呯數楂樻俯*/OTC_1,	OTC_2,	OTC_3,	OTC_recover,	OTC_filter3,\
								 /*鍏呯數浣庢俯*/UTC_1,	UTC_2,	UTC_3,	UTC_recover,	UTC_filter3,\
								 /*鏀剧數楂樻俯*/OTD_1,	OTD_2,	OTD_3,	OTD_recover,	OTD_filter3,\
								 /*鏀剧數浣庢俯*/UTD_1,	UTD_2,	UTD_3,	UTD_recover,	UTD_filter3,\
		        				 /*椹卞姩楂樻俯*/mos_1,	mos_2,	mos_3,	mos_recover,	mos_filter3,\
		        				 /*鍘嬪樊杩囧ぇ*/VDELTER_1,	VDELTER_2,	VDELTER_3,	VDELTER_recover,	VDELTER_filter3,\
		        				 /*鐢甸噺杩囦綆*/socLow_1,	socLow_2,	socLow_3,	socLow_recover,		socLow_filter3}

/* 鍏ㄥ眬鍙傛暟 */
typedef struct
{
	u16 ParamVer;			/* 鍙傛暟鍖虹増鏈帶鍒讹紙鍙敤浜庣▼搴忓崌绾ф椂锛屽喅瀹氭槸鍚﹀鍙傛暟鍖鸿繘琛屽崌绾э級 */
	struct PRT_E2ROM_PARAS protect;
}
PARAM_T;

extern PARAM_T g_tParam;

void LoadParam(void);
void SaveParam(void);

#endif
