#ifndef CONF_H_
#define CONF_H_

// #include "types.h"
// #include "tl_common.h"
// #include "drivers.h"
#include "../../common/types.h"
#include "stdint.h"
#include "flash_store_cfg.h"

// #define FAC_TEST
#define _UL_RENZHENG_ENABLE_

#define _FUNC_SIF_
#define _FUNC_UART_

#define C21         0
#define C31         1
#define D11         2
#define D31         3
#define C700         4
#define M1PRO         5
#define M23         6
#define M32         7
#define T3MAX         8
#define T3         9
#define M25         10
#define T1_AND_T2         11
#define D3PRO         12
#define C11_AND_C11pro         13
#define test_default         14

#define FD_BMS_TYPE   C11_AND_C11pro

#if (FD_BMS_TYPE == C21)
#define SeriesNum  (10)
#define CapacityFactory (58)
#define AFE_ODC2       		(300) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "C21"
#elif (FD_BMS_TYPE == C31)
#define SeriesNum  (10)
#define CapacityFactory (58)
#define AFE_ODC2       		(400) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "C31"
#elif (FD_BMS_TYPE == D11)
#define SeriesNum  (10)
#define CapacityFactory (116)
#define AFE_ODC2       		(300) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "D11"
#elif (FD_BMS_TYPE == D31)
#define SeriesNum  (10)
#define CapacityFactory (116)
#define  BMS_HARDWARE_VERDION_DEFAULT   "D31"
#elif (FD_BMS_TYPE == C700)
#define SeriesNum  (10)
#define CapacityFactory (87)
#define  BMS_HARDWARE_VERDION_DEFAULT   "C700"
#elif (FD_BMS_TYPE == M1PRO)
#define SeriesNum  (13)
#define CapacityFactory (116)
#define  BMS_HARDWARE_VERDION_DEFAULT   "M1PRO"
#elif (FD_BMS_TYPE == M23)
#define SeriesNum  (10)
#define CapacityFactory (116)
#define  BMS_HARDWARE_VERDION_DEFAULT   "M23"
#elif (FD_BMS_TYPE == M32)
#define SeriesNum  (13)
#define CapacityFactory (116)
#define  BMS_HARDWARE_VERDION_DEFAULT   "M32"
#elif (FD_BMS_TYPE == T3MAX)
#define SeriesNum  (10)
#define CapacityFactory (180)
#define  BMS_HARDWARE_VERDION_DEFAULT   "T3MAX"
#elif (FD_BMS_TYPE == T3)
#define SeriesNum  (10)
#define CapacityFactory (270)
#define  BMS_HARDWARE_VERDION_DEFAULT   "T3"
#elif (FD_BMS_TYPE == M25)
#define SeriesNum  (13)
#define CapacityFactory (145)
#define  BMS_HARDWARE_VERDION_DEFAULT   "M25"
#elif (FD_BMS_TYPE == T1_AND_T2)
#define SeriesNum  (13)
#define CapacityFactory (225)
#define  BMS_HARDWARE_VERDION_DEFAULT   "T1/T2"
#elif (FD_BMS_TYPE == D3PRO)
#define SeriesNum  (10)
#define CapacityFactory (78)
#define AFE_ODC2       		(300) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "D3PRO"
#elif (FD_BMS_TYPE == C11_AND_C11pro)
#define SeriesNum  (13)
#define CapacityFactory (104)
#define  BMS_HARDWARE_VERDION_DEFAULT   "C11"
#define AFE_ODC2       		(400) 
#else
#define SeriesNum  (10)
#define CapacityFactory (100)
#define  BMS_HARDWARE_VERDION_DEFAULT   "test"
#define AFE_ODC2       		(400) 
#endif

// #define DEV_NAME_STR  "BT_star001"
#define DEV_NAME_STR  "BT_FD190126F03200046_007"
#define DEV_NAME_LEN  (sizeof(DEV_NAME_STR)-1)

#define DEV_NAME_STR2  "BT_FD260228F03200046_666"
#define DEV_NAME_LEN2  (sizeof(DEV_NAME_STR2)-1)


typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef int16_t INT16;
typedef int8_t INT8;

#define __INIT_SOC__        (99)

#define FAC_INIT_soc (60)

typedef enum _CUR {
CurCHG = 0, CurDSG
}_Cur;

#define UPDNLMT16(Var,Max,Min)	{(Var)=((Var)>=(Max))?(Max):(Var);(Var)=((Var)<=(Min))?(Min):(Var);}

#define Feed_IWatchDog ;
#define log_i(...)   ;

#define  RF_EN_PIN              (GPIO_PD4)
#define  AFE1_PRO_EN_PIN        (GPIO_PD7)
#define  SW_PIN                 (GPIO_PA0)
#define  MCC_C_PIN              (GPIO_PA1)
#define  CHG_IN_PIN              (GPIO_PB1)
#define  ADC_NTC_PIN              (GPIO_PB4)
#define  ADC_VBUS_PIN              (GPIO_PB5)
#define  AFE_CTL_PIN              (GPIO_PB6)
#define  CHG_WK_PIN              (GPIO_PB7)
#define  OWC_TX_PIN              (GPIO_PC2)
#define  OWC_RX_PIN              (GPIO_PC3)
#define  ADC_NMOS_PIN              (GPIO_PC4)
#define  ADC_BUSEN_PIN              (GPIO_PD2)
#define  ADC_EN_PIN              (GPIO_PD3)

typedef struct 
{
   uint16_t    cnt_PA0_irq;
  uint16_t cnt_bms1_keyirq;
  uint16_t    bq33100_read_cnt;
  uint16_t    pec_err_cnt;
  
  uint8_t isdebugenable;
	uint16_t CHG;
	uint16_t DSG;

  uint16_t  cnt_enter_chg_open;
  uint16_t  cnt_enter_dsg_open;

   uint8_t  wakeup_reason;
  bool     wakeup_rtc;
  uint8_t time_enter_rtc;
  bool power_on;

  uint16_t enter_rtc_delay;
  bool     low_power_mode;
}Time_T;

extern Time_T  sys_time;

#endif
