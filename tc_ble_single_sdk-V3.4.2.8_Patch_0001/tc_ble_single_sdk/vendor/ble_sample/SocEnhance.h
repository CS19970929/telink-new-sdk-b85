#ifndef SOCENHANCE_H
#define SOCENHANCE_H

#include "conf.h"
#include "soc_kv_store.h"

struct SOC_CALCULATE_ELEMENT
{
	UINT32 u32CapFactory; // ��س�ʼ������?(��������)As*10 =        Ah*3600*10
	UINT32 u32CapChange; // ���������?	   As*10����������
	uint8_t u8CHG_AHCalcu_Flag; // ��簲ʱ���ֿ�ʹ�ñ��?
	uint8_t u8DSG_AHCalcu_Flag; // �ŵ簲ʱ���ֿ�ʹ�ñ�־

	uint8_t u8SOC_Now;	   // ��ǰ���SOC     0��100 Ϊ��������ٷֱ�?
	UINT32 u32CapNow;	   // ���ʣ��������As*10
	uint8_t u8DSG_SOC_Int; // ѭ������ֻ��ŵ������ѷŵ����������ٷֱȣ�?90%��һ��ѭ��
	UINT32 u32Cycle_times; // ѭ������*100������ֻ��������һ������ֱ�ӵ���ȥ��������̫���EEPROM���ֲ���
	UINT32 u32CapFull;	   // ���˥����������As*10(SOH)���ҵ���ʾSOHҪ��һ�ģ������?

	uint8_t u8SOC_Old; // ��ʼSOC    0-100 Ϊ��������ٷֱ�?
	UINT32 u32CapFull_Cal_As; // �������У�����������As*10
	uint8_t soh;
};

extern struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;		 // �ڲ�����ṹ��?

void APP_SOC_IntEnhance_Ctrl();

void soc_factory_param_init_first(void);

void set_soc_param(uint8_t _soc_val, uint16_t _cap_factory, uint8_t disp_sync_updatae);
void soc_param_lib_init(soc_kv_data_t* _soc);

#endif	/* SOCENHANCE_H */

