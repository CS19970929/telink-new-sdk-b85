#ifndef SOCENHANCE_H
#define SOCENHANCE_H

#include "conf.h"
#include "soc_kv_store.h"

struct SOC_CALCULATE_ELEMENT
{
	UINT32 u32CapFactory; // 锟斤拷爻锟绞硷拷锟斤拷锟斤拷锟?(锟斤拷锟斤拷锟斤拷锟斤拷)As*10 =        Ah*3600*10
	UINT32 u32CapChange; // 锟斤拷锟斤拷锟斤拷锟斤拷浠?	   As*10锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷
	uint8_t u8CHG_AHCalcu_Flag; // 锟斤拷绨彩憋拷锟斤拷挚锟绞癸拷帽锟街?
	uint8_t u8DSG_AHCalcu_Flag; // 锟脚电安时锟斤拷锟街匡拷使锟矫憋拷志

	uint8_t u8SOC_Now;	   // 锟斤拷前锟斤拷锟絊OC     0锟斤拷100 为锟斤拷锟斤拷锟斤拷锟斤拷俜直锟?
	UINT32 u32CapNow;	   // 锟斤拷锟绞ｏ拷锟斤拷锟斤拷锟斤拷锟紸s*10
	uint8_t u8DSG_SOC_Int; // 循锟斤拷锟斤拷锟斤拷只锟斤拷诺锟斤拷锟斤拷锟斤拷逊诺锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷俜直龋锟?90%锟斤拷一锟斤拷循锟斤拷
	UINT32 u32Cycle_times; // 循锟斤拷锟斤拷锟斤拷*100锟斤拷锟斤拷锟斤拷只锟斤拷锟斤拷锟斤拷锟斤拷一锟斤拷锟斤拷锟斤拷直锟接碉拷锟斤拷去锟斤拷锟斤拷锟斤拷锟斤拷太锟斤拷锟紼EPROM锟斤拷锟街诧拷锟斤拷
	UINT32 u32CapFull;	   // 锟斤拷锟剿ワ拷锟斤拷锟斤拷锟斤拷锟斤拷锟紸s*10(SOH)锟斤拷锟揭碉拷锟斤拷示SOH要锟斤拷一锟侥ｏ拷锟斤拷锟斤拷锟?

	uint8_t u8SOC_Old; // 锟斤拷始SOC    0-100 为锟斤拷锟斤拷锟斤拷锟斤拷俜直锟?
	UINT32 u32CapFull_Cal_As; // 锟斤拷锟斤拷锟斤拷锟叫ｏ拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷As*10
	uint8_t soh;
};

extern struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;		 // 锟节诧拷锟斤拷锟斤拷峁癸拷锟?

void APP_SOC_IntEnhance_Ctrl();

void soc_factory_param_init_first(void);

void set_soc_param(uint8_t _soc_val, uint16_t _cap_factory, uint8_t disp_sync_updatae);
void soc_param_lib_init(soc_kv_data_t* _soc);

#endif	/* SOCENHANCE_H */

