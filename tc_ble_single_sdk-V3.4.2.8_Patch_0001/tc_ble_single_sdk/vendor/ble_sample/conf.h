#ifndef CONF_H_
#define CONF_H_

// #include "types.h"
// #include "tl_common.h"
// #include "drivers.h"
#include "../../common/types.h"
#include "stdint.h"
#include "flash_store_cfg.h"

#define _FUNC_SIF_
#define _FUNC_UART_

#define DEV_NAME_STR  "BT_star009"
#define DEV_NAME_LEN  (sizeof(DEV_NAME_STR)-1)


typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef int16_t INT16;
typedef int8_t INT8;



#define __INIT_SOC__        (99)

#define SeriesNum  (10)

#define FAC_INIT_soc (60)
// #define CapacityFactory (87)
#define CapacityFactory (100)
// #define CapacityFactory (180)

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
  bool     test_fun1_soc;
}Time_T;

extern Time_T  sys_time;

#endif
