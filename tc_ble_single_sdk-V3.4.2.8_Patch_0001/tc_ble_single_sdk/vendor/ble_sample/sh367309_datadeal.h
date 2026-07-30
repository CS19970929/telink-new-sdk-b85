#ifndef SH367309_DATADEAL_H_
#define SH367309_DATADEAL_H_

#include "conf.h"



#ifdef LIFEPO
#define AFE_COV           (3780)
#define AFE_COV_recover   (3500)
#define AFE_COV_filter     100

#define AFE_CUV           (2500)
#define AFE_CUV_recover     (2600)
#define AFE_CUV_filter     (100)
#else

#ifndef  FAC_TEST
#define AFE_COV           (4250)
#define AFE_COV_recover   (4150)
#define AFE_COV_filter     100

#define AFE_CUV           (2750)
#define AFE_CUV_filter     (100)

#define AFE_CUV_recover     (3000)
#else
#define AFE_COV           (4200)
#define AFE_COV_recover   (4150)
#define AFE_COV_filter     100

#define AFE_CUV           (2800)
#define AFE_CUV_recover     (3000)
#define AFE_CUV_filter     (100)
#endif // ! FAC_TEST

#endif // LIFEPO

#define AFE_OTC           ((55 + 40) * 10)
#define AFE_OTC_recover     ((45 + 40) * 10)
#define AFE_OTC_filter      100

#define AFE_UTC           ((-7 + 40) * 10)
#define AFE_UTC_recover     ((0 + 40) * 10)
#define AFE_UTC_filter      100

#define AFE_OTD           ((75 + 40) * 10)
#define AFE_OTD_recover     ((60 + 40) * 10)
#define AFE_OTD_filter      100

#define AFE_UTD         ((-20 + 40) * 10)
#define AFE_UTD_recover     ((-10 + 40) * 10)
#define AFE_UTD_filter      100


#define AFE_OCC1       		(200) 
#define AFE_OCC1_filter  	(10)
#define AFE_OCC2       		(200) 
#define AFE_OCC2_filter  	(10)

#define AFE_ODC1_filter  	(100)
#define AFE_ODC2_filter  	(50)

/*curValue*/  /*defaultValue*/ /*maxValue*/ /*minValue*/
#define AFE_PARAMETERS_RS485_STRUCTION_DEFAULT  {\
	/*单节过压*/			AFE_COV,			AFE_COV,			5000,	1000,\
	/*单节过压恢�??*/		AFE_COV_recover,	AFE_COV_recover,	5000,	1000,\
	/*单节过压延时*/		AFE_COV_filter,		AFE_COV_filter,		50000,	1,\
	/*单节低压*/			AFE_CUV,			AFE_CUV,			5000,	1000,\
	/*单节低压恢�??*/		AFE_CUV_recover,	AFE_CUV_recover,	5000,	1000,\
	/*单节低压延时*/		AFE_CUV_filter,		AFE_CUV_filter,		50000,	1,\
	/*一级充电过�?*/		AFE_OCC1,			AFE_OCC1,			50000,	10,\
	/*一级充电过流延�?*/	AFE_OCC1_filter,	AFE_OCC1_filter,	50000,	1,\
	/*二级充电过流*/		AFE_OCC2,			AFE_OCC2,			50000,	10,\
	/*二级充电过流延时*/	AFE_OCC2_filter,	AFE_OCC2_filter,	50000,	1,\
	/*一级放电过�?*/		AFE_ODC1,			AFE_ODC1,			50000,	10,\
	/*一级放电过流延�?*/    AFE_ODC1_filter,	AFE_ODC1_filter,	50000,	1,\
	/*二级放电过流*/		AFE_ODC2,	        AFE_ODC2,			50000,	10,\
	/*二级放电过流延时*/    AFE_ODC2_filter,	AFE_ODC2_filter,	50000,	1,\
	/*充电高温*/			AFE_OTC,	       AFE_OTC,				2000,	400,\
	/*充电高温恢�??*/		AFE_OTC_recover,	AFE_OTC_recover,	50000,	1,\
	/*充电低温*/			AFE_UTC,	       AFE_UTC,				800,	0,\
	/*充电低温恢�??*/		AFE_UTC_recover,	AFE_UTC_recover,	50000,	1,\
	/*放电高温*/			AFE_OTD,	       AFE_OTD,				2000,	400,\
	/*放电高温恢�??*/		AFE_OTD_recover,	AFE_OTD_recover,	50000,	1,\
	/*放电低温*/			AFE_UTD,	       AFE_UTD,				800,	0,\
	/*放电低温恢�??*/		AFE_UTD_recover,	AFE_UTD_recover,	50000,	1,\
	/*�?�?电流*/			200,	200,	65000,	0,\
	/*�?�?延时*/			256,	256,		65000,	0,\
}

typedef struct {
	u8 CN 		:4;  		//5-15，�?�应串数，别的为16�?
	u8 BAL 		:1;			//0：平衡功能由内部SH367309控制�?1:由�?�部MCU�?
	u8 OCPM 		:1;			//0:充电过流�?关闭充电MOS，放电过流只关放电MOS�?1则同时关
	u8 ENMOS 	:1;			/*0:禁�?�充电MOS恢�?�控制位�?1:�?动充电MOS恢�?�控制位�?
												(当过充电/温度保护(温度实际�?2�?)关闭充电MOS后，如果检测到放电状态，则开�?充电MOS) */									
	u8 ENPCH 	:1;			//0:禁�?��?�充电功能，1：启动�?�充功能
	
	u8 EUVR 		:1;			//0：过放保护状态释放与负载释放无关，意味着负载释放�?和电流保护有关了
	u8 OCRA 		:1;			/*0：不允�?��?1：允�?---“电流保护定时恢复”功能，
												也即意味着�?能负载释放才能解除电流保护，不能�?动恢�? */
	u8 CTLC 		:2;			//
	u8 DIS_PF 	:1;			//
	u8 UV_OP 	:1;			//
	u8 Reserve 	:1;			//
	u8 E0VB 		:1;			//
	
}BYTE_00H_01H_TypeDef;


typedef struct {
	u8 OVH 		:2;  		//过充保护�?2�?
	u8 LDRT 		:2;			//11，负载释放延�?2000ms，这�?和短�?保护释放延时有关�?
	u8 OVT		:4;			//过充电保护延�?
	u8 OVL;					//过充保护�?8�?
}BYTE_02H_03H_TypeDef;


typedef struct {
	u8 OVRH 		:2;  		//过充保护恢�?�前2�?
	u8 Reserve 	:2;		
	u8 UVT		:4;			//低压保护延时
	u8 OVRL;					//过充保护恢�?�后8�?
}BYTE_04H_05H_TypeDef;

typedef struct {
	u8 UV;					//低压保护
	u8 UVR;					//低压保护恢�??
}BYTE_06H_07H_TypeDef;


typedef struct {
	u8 BALV;					//均衡开�?电压
	u8 PREV;					//预充电压
}BYTE_08H_09H_TypeDef;

typedef struct {
	u8 L0V;				//低电压�?��?�充电电�?
	u8 PFV;				//二�?�过充电保护电压设置寄存�?，放大一些，不用这个
}BYTE_0AH_0BH_TypeDef;


typedef struct {
	u8 OCD1T		:4;			//放电过流1保护延时
	u8 OCD1V		:4;			//放电过流保护1保护电压
	u8 OCD2T		:4;			//放电过流2保护延时
	u8 OCD2V		:4;			//放电过流保护2保护电压
}BYTE_0CH_0DH_TypeDef;

typedef struct {
	u8 SCT		:4;			//�?�?保护延时
	u8 SCV		:4;			//�?�?保护电压
	u8 OCCT		:4;			//充电过流保护延时
	u8 OCCV		:4;			//充电过流保护电压
}BYTE_0EH_0FH_TypeDef;

typedef struct {
	u8 PFT		:2;			//�?�?保护延时
	u8 OCRT		:2;			//�?�?保护电压
	u8 MOST		:2;			//充电过流保护延时
	u8 CHS		:2;			//充电过流保护电压
}BYTE_10H_TypeDef;

typedef struct {
	u8 OTC;					//充电高温保护
	u8 OTCR;					//充电高温保护恢�??
	u8 UTC;					//充电低温保护
	u8 UTCR;					//充电低温保护恢�??
	u8 OTD;					//放电高温保护
	u8 OTDR;					//放电高温保护恢�??
	u8 UTD;					//放电低温保护
	u8 UTDR;					//放电低温保护恢�??
	u8 TR;
}BYTE_11H_19H_TypeDef;


typedef	struct {
	BYTE_00H_01H_TypeDef 	m00H_01H;
	BYTE_02H_03H_TypeDef	m02H_03H;
	BYTE_04H_05H_TypeDef	m04H_05H;
	BYTE_06H_07H_TypeDef 	m06H_07H;
	BYTE_08H_09H_TypeDef 	m08H_09H;
	BYTE_0AH_0BH_TypeDef 	m0AH_0BH;
	BYTE_0CH_0DH_TypeDef	m0CH_0DH;
	BYTE_0EH_0FH_TypeDef 	m0EH_0FH;
	BYTE_10H_TypeDef 			m10H;
	BYTE_11H_19H_TypeDef	m11H_19H;
}AFE_ROM_PARAMETERS_TypeDef;

/******************************* AFE保护参数结构�? *****************************/
typedef struct {
	u16 curValue;			//当前�?
	u16 defaultValue;		//默�?��?
	u16 maxValue;			//最大�?
	u16 minValue;			//最小�?
}AFE_Value_Typedef;

typedef struct{
	AFE_Value_Typedef	u16VcellOvp;  		//单节过压 mv
	AFE_Value_Typedef	u16VcellOvp_Rcv;	//过压恢�?? mv
	AFE_Value_Typedef	u16VcellOvp_Filter;	//过压延时 10ms
	
	AFE_Value_Typedef	u16VcellUvp;		//单节低压
	AFE_Value_Typedef	u16VcellUvp_Rcv;
	AFE_Value_Typedef	u16VcellUvp_Filter;
	
	AFE_Value_Typedef	u16IchgOcp_First;	//一级充电过�? A*10
	AFE_Value_Typedef	u16IchgOcp_Filter_First;
	
	AFE_Value_Typedef	u16IchgOcp_Second;	//二级充电过流
	AFE_Value_Typedef	u16IchgOcp_Filter_Second;
	
	AFE_Value_Typedef	u16IdsgOcp_First;	//一级放电过�?
	AFE_Value_Typedef	u16IdsgOcp_Filter_First;
	
	AFE_Value_Typedef	u16IdsgOcp_Second;	//二级放电过流
	AFE_Value_Typedef	u16IdsgOcp_Filter_Second;
	
	AFE_Value_Typedef	u16TChgOTp;			//充电高温 (�?*10+400)
	AFE_Value_Typedef	u16TChgOTp_Rcv;
	AFE_Value_Typedef	u16TchgUTp;			//充电低温
	AFE_Value_Typedef	u16TchgUTp_Rcv;
	AFE_Value_Typedef	u16TdischgOTp;		//放电高温
	AFE_Value_Typedef	u16TdischgOTp_Rcv;
	AFE_Value_Typedef	u16TdischgUTp;		//放电低温
	AFE_Value_Typedef	u16TdischgUTp_Rcv;
	AFE_Value_Typedef 	u16CBC_Cur_DSG;
	AFE_Value_Typedef 	u16CBC_DelayT;
}AFE_Parameters_RS485_Typedef; 

typedef union __MTP_REG_BSTATUS1 {
    u8 all;
    struct _MTP_REG_BSTATUS1 {
		u8 OV     			:1;		//单节过压
		u8 UV     			:1;		//单节低压
		u8 OCD1      		:1;		//放电过流1保护状�?
		u8 OCD2      		:1;		//放电过流2保护状�?
		
		u8 OCC     			:1;		//充电过流保护状�?
		u8 SC  				:1;		//�?�?保护状�?
		u8 PF  				:1;		//二�?�过充电保护状态位
		u8 WDT  				:1;		//看门狗溢出位
     }bits;
}MTP_REG_BSTATUS1;


typedef union __MTP_REG_BSTATUS2 {
    u8 all;
    struct _MTP_REG_BSTATUS2 {
		u8 UTC  				:1;		//充电低温保护状态位
		u8 OTC  				:1;		//充电高温保护状态位
		u8 UTD      			:1;		//放电低温保护状态位
		u8 OTD   			:1;		//放电高温保护状态位
		
		u8 Rcv				:4;		//保留�?
		//u8 Rcv2				:8;		//保留�?
     }bits;
}MTP_REG_BSTATUS2;


typedef union __MTP_REG_BSTATUS3 {
    u8 all;
    struct _MTP_REG_BSTATUS3 {
		u8 DSG_FET     		:1;		//放电管状�?
		u8 CHG_FET     		:1;		//充电管状�?
		u8 PCHG_FET      	:1;		//预充管状�?
		u8 L0V      			:1;		//低电压�?��?�充电状态位
		
		u8 EEPR_WR     		:1;		//EEPROM写操作状态位
		u8 RCV  				:1;		//保留�?
		u8 DSGING  			:1;		//放电状�?
		u8 CHGING  			:1;		//充电状�?
     }bits;
}MTP_REG_BSTATUS3;


typedef union __MTP_REG_CONF {
    u8 all;
    struct _MTP_REG_CONF {
		u8 IDLE     		:1;		//放电管状�?
		u8 SLEEP     	:1;		//充电管状�?
		u8 ENWDT      	:1;		//预充管状�?
		u8 CADCON      	:1;		//低电压�?��?�充电状态位
		
		u8 CHGMOS     	:1;		//EEPROM写操作状态位
		u8 DSGMOS  		:1;		//保留�?
		u8 PCHMOS  		:1;		//放电状�?
		u8 OCRC  		:1;		//充电状�?
     }bits;
}MTP_REG_CONF;

typedef struct _AFE_REG_STORE {
	u8 u8_MTP_SCONF2;				//01H
	u8 u8_MTP_SCV_SCT;				//0EH
	
	MTP_REG_CONF REG_MTP_CONF;			//40H
	u8 u8_MTP_BALANCEH;				//41H
	u8 u8_MTP_BALANCEL;				//42H
	MTP_REG_BSTATUS1 REG_BSTATUS1;		//43H
	MTP_REG_BSTATUS2 REG_BSTATUS2;		//44H
	MTP_REG_BSTATUS3 REG_BSTATUS3;		//45H
	u8 TR;					//71H
	u16 TR_ResRef;			//680 + 5*TR，单位是kΩ*100
}SH367309_REG_STORE;

#define BIT_ENPCH	(0<<7)		//0:禁�?��?�充电功能，1：启动�?�充功能
#define BIT_ENMOS	(1<<6)		//0:禁�?�充电MOS恢�?�，1:�?动充电MOS恢�?�控制位�?
								//当过充电/温度保护(温度实际�?2�?)关闭充电MOS后，如果检测到放电状态，则开�?充电MOS
								//用这�?吧，MOS应�?�会没那么热�?
#define BIT_OCPM	(0<<5)		//0:充电过流�?关闭充电MOS，放电过流只关放电MOS�?1则同时关
#define BIT_BAL		(0<<4)		//0：平衡功能由内部SH367309控制�?1:由�?�部MCU�?
#define BIT0_3_CN	(0)			//5-15，�?�应串数，别的为16�?
#define BYTE_00H_SCONF1			BIT_ENPCH|BIT_ENMOS|BIT_OCPM|BIT_BAL|BIT0_3_CN

#define BIT_E0VB	(0<<7)		//0：关�?�?1：开�?---“�?��??低压电芯充电”功�?
#define BIT_UV_OP	(0<<5)		//0：过放只关闭放电MOS�?1：过放关�?充放电MOS
#define BIT_DIS_PF	(1<<4)		//0：开�?�?1：�?��??---二�?�过充电保护，同时也�?�?线�?�测功�?(奇�?了，怎么揉在一起了)
								//这个标志位为0则开�?，会出现�?题，重新�?电上电�?�易出现全部电压�?10000mV左右，所以�?��?�掉(出问题PF引脚会输出VSS电平)
// #define BIT2_3_CTLC	(0<<2)		//MOS由内部逻辑控制，CTL引脚无效，具体看规格�?
#define BIT2_3_CTLC	(1<<4) | (1<<3)		//MOS由内部逻辑控制，CTL引脚无效，具体看规格�?
// #define BIT2_3_CTLC	(12<<2)		//MOS由内部逻辑控制，CTL引脚无效，具体看规格�?
								//2，�?�置为控制放电MOS，�?�放功能
#define BIT_OCRA	(0<<1)		//0：不允�?��?1：允�?---“电流保护定时恢复”功能，也即意味着�?能负载释放才能解除电流保护，不能�?动恢�?
#define BIT_EUVR	(0)			//0：过放保护状态释放与负载释放无关，意味着负载释放�?和电流保护有关了
								//过放保护还必须负载释放才能解除�?
								//先改�?0测试，后面改�?1，�?�果不关充电管，P+永远检测到一�?高电平，负载移除没法检测到
								//改回0，测试没�?�?
#define BYTE_01H_SCONF2			BIT_E0VB|BIT_UV_OP|BIT_DIS_PF|BIT2_3_CTLC|BIT_OCRA|BIT_EUVR


//以下电压的大小�?�求
//PFV->OV->OVR->UVR->UV->Vpd->PREV->L0V
#define BIT4_7_OVT				(6<<4)		//0110，过充电保护延时1s
#define BIT2_3_LDRT				(11<<2)		//11，负载释放延�?2000ms，这�?和短�?保护释放延时有关�?
#define BIT0_1_OVH				(u8)((VAL_CELL_OVP/5)>>8)			//过充保护�?2�?
#define BYTE_02H_OVT_LDRT_OVH	BIT4_7_OVT|BIT2_3_LDRT|BIT0_1_OVH

#define BIT0_7_OVL				(u8)((VAL_CELL_OVP/5)&0x00FF)		//过充保护�?8�?
#define BYTE_03H_OVL			BIT0_7_OVL

#define BIT4_7_UVT				(6<<4)									//0110，过放保护延�?1s
#define BIT0_1_OVR				(u8)((VAL_CELL_OVP_REC/5)>>8)		//过充保护恢�?�前2�?
#define BYTE_04H_UVT_OVRH		BIT4_7_UVT|BIT0_1_OVR

#define BIT0_7_OVR				(u8)((VAL_CELL_OVP_REC/5)&0x00FF)	//过充保护恢�?�后8�?
#define BYTE_05H_OVRL			BIT0_7_OVR

#define BIT0_7_UV				(u8)((VAL_CELL_UVP/20)&0x00FF)		//低压保护
#define BYTE_06H_UV				BIT0_7_UV

#define BIT0_7_UVR				(u8)((VAL_CELL_UVP_REC/20)&0x00FF)	//低压保护恢�??
#define BYTE_07H_UVR			BIT0_7_UVR

#define BIT0_7_BALV 			(u8)((VAL_BAL_OPEN/20)&0x00FF)		//均衡开�?电压
#define BYTE_08H_BALV			BIT0_7_BALV

#define BIT0_7_PREV				(u8)((2000/20)&0x00FF)				//预充电压
#define BYTE_09H_PREV			BIT0_7_PREV

#define BIT0_7_L0V				(u8)(1000/20)				//低电压�?��?�充电电�?
#define BYTE_0AH_L0V			BIT0_7_L0V

#define BIT0_7_PFV				(u8)(5000/20)				//二�?�过充电保护电压设置寄存�?，放大一些，不用这个
#define BYTE_0BH_PFV			BIT0_7_PFV



#define BIT4_7_OCD1V			(0<<4)		//1000，放电过流保�?1保护电压=100mV�?0000�?20mV
#define BIT0_3_OCD1T			(6)			//0110，放电过�?1保护延时1s
#define BYTE_0CH_OCD1V_OCD1T	BIT4_7_OCD1V|BIT0_3_OCD1T

// #define BIT4_7_OCD2V			(15<<4)		//1000，放电过流保�?1保护电压=120mV�?0000�?30mV，弄成最�?
// #define BIT0_3_OCD2T			(6)			//0110，放电过�?1保护延时200ms
// #define BYTE_0DH_OCD2V_OCD2T	BIT4_7_OCD2V|BIT0_3_OCD2T

#define BIT4_7_OCD2V			(3<<4)		//1000，放电过流保�?1保护电压=120mV�?0000�?30mV，弄成最�?
#define BIT0_3_OCD2T			(7)			//0110，放电过�?1保护延时200ms
#define BYTE_0DH_OCD2V_OCD2T	BIT4_7_OCD2V|BIT0_3_OCD2T

#define BIT4_7_SCV				(0<<4)		//1000，短�?保护电压=290mV,0010�?110mV
#define BIT0_3_SCT				(1)			//0001，短�?保护延时64us
#define BYTE_0EH_SCV_SCT		BIT4_7_SCV|BIT0_3_SCT

#define BIT4_7_OCCV				(0<<4)		//1000，充电过流保护电�?=100mV,0000�?20mV
#define BIT0_3_OCCT				(10)		//1010，充电过流保护延�?1s
#define BYTE_0FH_OCCV_OCCT		BIT4_7_OCCV|BIT0_3_OCCT

#define BIT6_7_CHS				(0<<6)		//充放电状态监测电�?=200uV，就是BSTATUS3的CHGING和DSGING位的�?位的界限
#define BIT4_5_MOST				(0<<4)		//充放电MOSFET开�?延时=64us
#define BIT2_3_OCRT				(0<<2)		//充放电过流自动回复延�?=32s
#define BIT0_1_PFT				(0)			//二�?�过充保护延�?=9s
#define BYTE_10H_MOST_OCRT_PFT	BIT6_7_CHS|BIT4_5_MOST|BIT2_3_OCRT|BIT0_1_PFT


/*
摄氏�?
充电高温保护------->
充电高温保护恢�??--->
充电低温保护------->
充电低温保护恢�??--->
放电高温保护------->
放电高温保护恢�??--->
放电低温保护------->
放电低温保护恢�??--->
//因为没有TR的值，所以先换算�?(X + 40)
*/
#define VAL_CHG_OTP				((u8)110)
#define VAL_CHG_OTP_RCV			((u8)105)
#define VAL_CHG_UTP				((u8)35)
#define VAL_CHG_UTP_RCV			((u8)40)
#define VAL_DSG_OTP				((u8)110)
#define VAL_DSG_OTP_RCV			((u8)105)
#define VAL_DSG_UTP				((u8)35)
#define VAL_DSG_UTP_RCV			((u8)40)

#define BYTE_11H_OTC			VAL_CHG_OTP
#define BYTE_12H_OTCR			VAL_CHG_OTP_RCV
#define BYTE_13H_UTC			VAL_CHG_UTP
#define BYTE_14H_UTCR			VAL_CHG_UTP_RCV
#define BYTE_15H_OTD			VAL_DSG_OTP
#define BYTE_16H_OTDR			VAL_DSG_OTP_RCV
#define BYTE_17H_UTD			VAL_DSG_UTP
#define BYTE_18H_UTDR			VAL_DSG_UTP_RCV

#define BYTE_19H_TR				10			//内部默�?�应该是10kΩ，实际是多少读出来便�?，这�?数值无�?

#define AFE_ID				0x34

#define E2PROM_ID			0xA0

#define VAL_CELL_OVP			((u16)3750)	//单位mV	
#define VAL_CELL_OVP_REC		((u16)3650)	//单位mV，过充保护恢�?
#define VAL_CELL_UVP			((u16)2500)	//单位mV	
#define VAL_CELL_UVP_REC		((u16)2600)	//单位mV，低压保护恢�?
#define VAL_BAL_OPEN			((u16)4160)	//单位mV，均衡开�?电压


//Define MTP register addr
#define MTP_OTC             0x11
#define MTP_OTCR            0x12
#define MTP_UTC             0x13
#define MTP_UTCR            0x14
#define MTP_OTD             0x15
#define MTP_OTDR            0x16
#define MTP_UTD             0x17
#define MTP_UTDR            0x18
#define MTP_TR              0x19

#define MTP_CONF			0x40
#define MTP_BALANCEH		0x41
#define MTP_BALANCEL		0x42
#define MTP_BSTATUS1		0x43
#define MTP_BSTATUS2		0x44
#define MTP_BSTATUS3		0x45
#define MTP_TEMP1			0x46
#define MTP_TEMP2			0x48
#define MTP_TEMP3			0x4A
#define MTP_CUR				0x4C
#define MTP_CELL1			0x4E
#define MTP_CELL2			0x50
#define MTP_CELL3			0x52
#define MTP_CELL4			0x54
#define MTP_CELL5			0x56
#define MTP_CELL6			0x58
#define MTP_CELL7			0x5A
#define MTP_CELL8			0x5C
#define MTP_CELL9			0x5E
#define MTP_CELL10			0x60
#define MTP_CELL11			0x62
#define MTP_CELL12			0x64
#define MTP_CELL13			0x66
#define MTP_CELL14			0x68
#define MTP_CELL15			0x6A
#define MTP_CELL16			0x6C
#define MTP_ADC2			0x6E
#define MTP_BFLAG1			0x70
#define MTP_BFLAG2			0x71
#define MTP_RSTSTAT			0x72

#pragma pack(push, 1)

typedef struct
{
    // 0x40 ~ 0x45
    uint8_t CONF;       // 0x40
    uint8_t BALANCEH;   // 0x41
    uint8_t BALANCEL;   // 0x42
   MTP_REG_BSTATUS1 REG_BSTATUS1;		//43H
	MTP_REG_BSTATUS2 REG_BSTATUS2;		//44H
	MTP_REG_BSTATUS3 REG_BSTATUS3;		//45H

    // 0x46 ~ 0x4B (TEMP1/2/3 raw 16bit signed)
	uint16_t Temp1;
	uint16_t Temp2;
	uint16_t Temp3;

    // 0x4C ~ 0x4D (CUR raw 16bit signed, CUR15 符号位：1 放电 / 0 充电):contentReference[oaicite:1]{index=1}
	uint16_t Curl;

    // 0x4E ~ 0x6D (CELL1~CELL16 raw 16bit signed)
	uint16_t Cell[16];

    // 0x6E ~ 0x6F (CADCD raw 16bit signed, CDATA.15 符号位：1 放电 / 0 充电):contentReference[oaicite:2]{index=2}
	uint16_t Cadc;

    // 0x70 ~ 0x72
    uint8_t BFLAG1;     // 0x70
    uint8_t BFLAG2;     // 0x71 (读后某些 FLG �?动清除，手册有描�?):contentReference[oaicite:3]{index=3}
    // uint8_t RSTSTAT;    // 0x72
	uint8_t crc8;
} sh367309_ram_t;

#pragma pack(pop)

struct SH367309_Read {			/* AD Read	*/
	UINT16		u16VCell[16];   // mv
	UINT16		u16TempBat[3];					
	UINT32		u32VBat;       	// mv
	UINT16      u16Current;     // mA
};

extern const unsigned char SeriesSelect_AFE1[16][16];

#define SH309_RAM_START_ADDR   0x40
#define SH309_RAM_END_ADDR     0x71
#define SH309_RAM_LEN          (SH309_RAM_END_ADDR - SH309_RAM_START_ADDR + 1)

// 编译期校验（如果你编译器不支�? static_assert，就删掉�?
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(sh367309_ram_t) == SH309_RAM_LEN, "sh367309_ram_t size mismatch!");
#endif

enum TempArray {
	AFE1_TEMP1 = 0,
	AFE1_TEMP2,
	AFE1_TEMP3,
	AFE2_TEMP1,
	AFE2_TEMP2,
	AFE2_TEMP3,
	ENV_TEMP1,
	ENV_TEMP2,
	ENV_TEMP3,
	MOS_TEMP1,
	TEMP_NUM
};

#define SYSKMAX   		((UINT16)1536)      // 1.5
#define SYSKDEFAULT		((UINT16)1024)      // 1
#define SYSKMIN   		((UINT16)512)       // 0.5

#define SYSBMAX   		((INT16)30000)      // 30
#define SYSBDEFAULT		((INT16)0)      	// 0
#define SYSBMIN   		((INT16)-30000)     // -30


union System_Status {				//TODO�����⣬Heat��Cool��û��
    UINT32 all;
    struct System_Status_Flag {
		UINT8 b1StartUpBMS			:1;		//BMS��һ�ο�����־λ����ʼΪ1��ȷ���ܴ򿪹�����Ϊϵͳ��ʼ�����?				//�ڶ���8λ
		UINT8 b1Status_MOS_PRE      :1;		//Ԥ��MOS�ܹ���״̬
		UINT8 b1Status_MOS_CHG      :1;		//���MOS�ܹ���״̬
		UINT8 b1Status_MOS_DSG      :1;		//�ŵ�MOS�ܹ���״̬

		UINT8 b1Status_Relay_PRE    :1;		//Ԥ��̵�������״�?
		UINT8 b1Status_Relay_CHG    :1;		//�ֿڳ��̵�������״̬
		UINT8 b1Status_Relay_DSG    :1;		//�ֿڷŵ�̵�������״�?
		UINT8 b1Status_Relay_MAIN   :1;		//ͬ�����̵�������״̬

		UINT8 b1Status_Heat         :1;		//���ȹ���״̬					//��һ��8λ
		UINT8 b1Status_Cool         :1;		//���书��״̬
		UINT8 b1Status_AFE1         :1;		//AFE1״̬
		UINT8 b1Status_AFE2	        :1;		//AFE2״̬

		UINT8 b1Status_Balance		:1;		//���⹦��״̬
		UINT8 b1Status_ToSleep		:1;		//������������״̬
		UINT8 b1Status_BnCloseIO	:1;		//�������MOS�رձ�־λ
		UINT8 b1Status_HeatCloseIO	:1;		//����ʱ�ر�MosRelay
		
		UINT8 b1Status_SysLimits	:1;		//res						//���ĸ�8λ
		UINT8 b1Status_CBCCloseIO	:1;		//res
		UINT8 b1Status_DriverExtCtrl:1;		//����תΪ�ⲿ���ƣ�����������ã�����?
		UINT8 bRcved6				:1;		//res

		UINT8 b4Status_ProjectVer	:4;		//��¼һЩ��Ŀ��Ϣ��Ŀǰ���ߴ���Ϊ1������?0
		
		UINT8 bRcved11				:8;		//res						//������8λ
     }bits;
};

extern volatile union System_Status SystemStatus;

enum FaultFlag {
	CellOvp_First = 1,
	CellUvp_First,
	BatOvp_First,
	BatUvp_First,
	IchgOcp_First,
	IdischgOcp_First,
	CellChgOTp_First,
	CellChgUTp_First,
	CellDsgOTp_First,
	CellDsgUTp_First,
	MosOTp_First,
	VdeltaOvp_First,
	CellSocUp_First,
	
	CellOvp_Second,
	CellUvp_Second,
	BatOvp_Second,
	BatUvp_Second,
	IchgOcp_Second,
	IdischgOcp_Second,
	CellChgOTp_Second,
	CellChgUTp_Second,
	CellDsgOTp_Second,
	CellDsgUTp_Second,
	MosOTp_Second,
	VdeltaOvp_Second,
	CellSocUp_Second,

	CellOvp_Third,
	CellUvp_Third,
	BatOvp_Third,
	BatUvp_Third,
	IchgOcp_Third,
	IdischgOcp_Third,
	CellChgOTp_Third,
	CellChgUTp_Third,
	CellDsgOTp_Third,
	CellDsgUTp_Third,
	MosOTp_Third,
	VdeltaOvp_Third,
	CellSocUp_Third
};
enum SYSTEM_ERROR_COMMAND {
	ERROR_AFE1 = 1,			//Ϊ�˷���ʹ�ú�������ֵ
	ERROR_AFE2,
	ERROR_CAN,
	ERROR_EEPROM_COM,
	ERROR_SPI,
	ERROR_UPPER,
	ERROR_CLIENT,
	ERROR_SCREEN,
	
	ERROR_WIFI,
	ERROR_BLUETOOTH,
	ERROR_APP,
	ERROR_CBC_CHG,
	ERROR_CBC_DSG,
	ERROR_EEPROM_STORE,
	ERROR_HSE,
	ERROR_LSE,
	ERROR_VDEATLE_OVER,
	
	ERROR_BALANCED,
	ERROR_ADC,
	ERROR_SOC_CAIL,
	ERROR_HEAT,
	ERROR_COOL,
	ERROR_TEMP_BREAK,
	ERROR_DSG_SHORT,
	ERROR_NUM = ERROR_DSG_SHORT,


	ERROR_REMOVE_AFE1,
	ERROR_REMOVE_AFE2,
	ERROR_REMOVE_CAN,
	ERROR_REMOVE_EEPROM_COM,
	ERROR_REMOVE_SPI,
	ERROR_REMOVE_UPPER,
	ERROR_REMOVE_CLIENT,
	ERROR_REMOVE_SCREEN,
	
	ERROR_REMOVE_WIFI,
	ERROR_REMOVE_BLUETOOTH,
	ERROR_REMOVE_APP,
	ERROR_REMOVE_CBC_CHG,
	ERROR_REMOVE_CBC_DSG,
	ERROR_REMOVE_EEPROM_STORE,
	ERROR_REMOVE_HSE,
	ERROR_REMOVE_LSE,
	ERROR_REMOVE_VDEATLE_OVER,
	
	ERROR_REMOVE_BALANCED,
	ERROR_REMOVE_ADC,
	ERROR_REMOVE_HEAT,
	ERROR_REMOVE_COOL,
	ERROR_REMOVE_SOC_CAIL,
	ERROR_REMOVE_TEMP_BREAK,
	ERROR_REMOVE_DSG_SHORT,

	ERROR_STATUS_AFE1,
	ERROR_STATUS_AFE2,
	ERROR_STATUS_CAN,
	ERROR_STATUS_EEPROM_COM,
	ERROR_STATUS_SPI,
	ERROR_STATUS_UPPER,
	ERROR_STATUS_CLIENT,
	ERROR_STATUS_SCREEN,
	
	ERROR_STATUS_WIFI,
	ERROR_STATUS_BLUETOOTH,
	ERROR_STATUS_APP,
	ERROR_STATUS_CBC_CHG,
	ERROR_STATUS_CBC_DSG,
	ERROR_STATUS_EEPROM_STORE,
	ERROR_STATUS_HSE,
	ERROR_STATUS_LSE,
	ERROR_STATUS_VDEATLE_OVER,
	
	ERROR_STATUS_BALANCED,
	ERROR_STATUS_ADC,
	ERROR_STATUS_HEAT,
	ERROR_STATUS_COOL,
	ERROR_STATUS_SOC_CAIL,
	ERROR_STATUS_TEMP_BREAK,
	ERROR_STATUS_DSG_SHORT,
};

#define Record_len 10
extern UINT8 FaultPoint_First2;
extern UINT8 FaultPoint_Second2;
extern UINT8 FaultPoint_Third2;
extern UINT16 Fault_record_First2[Record_len];
extern UINT16 Fault_record_Second2[Record_len];
extern UINT16 Fault_record_Third2[Record_len];


extern sh367309_ram_t ram_reg_309;
extern SH367309_REG_STORE SH367309_Reg_Store;

u8 MTPWrite(u8 WrAddr, u8 Length, u8 *WrBuf);
UINT16 GetEndValue(const UINT16 *ptbl, UINT16 tblsize, UINT16 dat);
void FaultWarnRecord2(enum FaultFlag num);
u8 SH367309_UpdataAfeConfig(void);
u8 SH367309_VerifyAfeConfig(void);
u8 App_AFEGet(void);
u8 AFE_Reset(void);
u8 AFE_IsReady(void);
u32 System_ERROR_UserCallback(enum SYSTEM_ERROR_COMMAND errorCode);

#endif
