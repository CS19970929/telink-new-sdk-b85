#ifndef SCI_H
#define SCI_H

#include "stdint.h"

struct SOC_CAL_ELEMENT_UPPER {
	uint16_t u16Soc;                 	//��ǰ���SOC     0��100 Ϊ��������ٷֱ�
	uint16_t u16Soh;                 	//Ϊ���������ٷֱ�0����100
	uint16_t u16CapacityNow;        	//��ǰ����	Ah*100
	uint16_t u16CapacityFull;        	//��ǰ��������	Ah*100		//Ϊʲô*100Ϊ��λ�أ���Ϊ��λ����mAh�������������ʾ����
	uint16_t u16CapacityFactory;     	//������������	Ah*100		//�����Ľ����650Ah���
	uint16_t u16Cycle_times;     		//ѭ������
};

struct MDLCHGFAULT_BITS     {    	// bits  description
	uint8_t b1CellOvp			:1;   	//
	uint8_t b1CellUvp			:1;   	//
	uint8_t b1BatOvp			:1;   	//
	uint8_t b1BatUvp			:1;   	//
	
	uint8_t b1IchgOcp			:1;   	//
	uint8_t b1IdischgOcp		:1;   	//
	uint8_t b1CellChgOtp		:1;   	//
	uint8_t b1CellDischgOtp 	:1;   	//

	uint8_t b1CellChgUtp		:1;   	//
	uint8_t b1CellDischgUtp 	:1;   	//
	uint8_t b1VcellDeltaBig	:1;   	//
	uint8_t b1TempDeltaBig 	:1;   	//���û�У�Res����

	uint8_t b1SocLow			:1;   	//
	uint8_t b1TmosOtp			:1;   	//
	uint8_t b1Rcved1	 		:1;   	//
	uint8_t b1Rcved2  		:1;   	//
};

union  MDLCHGFAULT_REG
{
	uint16_t   all;
	struct MDLCHGFAULT_BITS	bits;
};


struct stCell_Info {
	uint16_t	u16VCell[32];
	uint16_t	u16VCellMax;                    // mv
	uint16_t	u16VCellMin;                    // mv
    uint16_t	u16VCellMaxPosition;
    uint16_t	u16VCellMinPosition;
	uint16_t	u16VCellDelta;                  // mv
	uint16_t	u16VCellTotle;                  // v *100
    uint16_t	u16Temperature[10];       // +40��C *10
    uint16_t	u16TempMax;                     // +40��C *10
	uint16_t	u16TempMin;                     // +40��C *10
	uint16_t	u16Ichg;                        // A *10
    uint16_t	u16IDischg;                     // A *10
    //uint16_t	u16Soc;							// %
    struct SOC_CAL_ELEMENT_UPPER SocElement;
    union MDLCHGFAULT_REG unMdlFault_First;
    union MDLCHGFAULT_REG unMdlFault_Second;
	union MDLCHGFAULT_REG unMdlFault_Third;
	uint16_t	u16BalanceFlag1;                 //��ؾ����־λ1
	uint16_t	u16BalanceFlag2;                 //��ؾ����־λ2
};

#endif