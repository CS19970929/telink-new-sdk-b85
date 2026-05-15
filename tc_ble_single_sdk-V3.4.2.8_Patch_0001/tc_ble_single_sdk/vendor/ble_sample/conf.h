#ifndef CONF_H_
#define CONF_H_

// #include "types.h"
// #include "tl_common.h"
// #include "drivers.h"
#include "../../common/types.h"
#include "stdint.h"
#include "flash_store_cfg.h"

// #define FAC_TEST
// #define DISP_VBAT_AND_TEMP_
#define _UL_RENZHENG_ENABLE_

#define _FUNC_SIF_
#define _FUNC_UART_

#ifndef  FAC_TEST
#define _DI_SWITCH_SYS_ONOFF	
#endif // ! FAC_TEST

#define __SLEEP_VNORMAL__             	(3000)
#define	__SLEEP_TIMENORMAL__	          (60 * 60 * 24)	
#define __SLEEP_VLOW__     		          (2800)
#define	__SLEEP_TIMEVLOW__		          (60 * 60 * 1)

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

#define FD_BMS_TYPE   T1_AND_T2

#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D004" 
#define  BMS_SERIAL_NUMBER_DEFAULT  	"20260513"

#if (FD_BMS_TYPE == C21)
#define SeriesNum  (10)
#define CapacityFactory (58)
#define AFE_ODC1       		(300) 
#define AFE_ODC2       		(500) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "C21"
#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D004" 
#elif (FD_BMS_TYPE == C31)
#define SeriesNum  (10)
#define CapacityFactory (58)
#define AFE_ODC1       		(300) 
#define AFE_ODC2       		(500) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "C31"
#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D004" 
#elif (FD_BMS_TYPE == D11)
#define SeriesNum  (10)
#define CapacityFactory (116)
#define AFE_ODC1       		(300) 
#define AFE_ODC2       		(500) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "D11"
#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D004" 
#elif (FD_BMS_TYPE == D31)
#define SeriesNum  (10)
#define CapacityFactory (116)
#define AFE_ODC1       		(300) 
#define AFE_ODC2       		(500) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "D31"
#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D004" 
#elif (FD_BMS_TYPE == C700)
#define SeriesNum  (10)
#define CapacityFactory (87)
#define AFE_ODC1       		(300) 
#define AFE_ODC2       		(500) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "C700"
#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D004" 
#elif (FD_BMS_TYPE == M1PRO)
#define SeriesNum  (13)
#define CapacityFactory (116)
#define AFE_ODC1       		(400) 
#define AFE_ODC2       		(600) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "M1PRO"
#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D004" 
#elif (FD_BMS_TYPE == M23)
#define SeriesNum  (10)
#define CapacityFactory (116)
#define AFE_ODC1       		(400) 
#define AFE_ODC2       		(600) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "M23"
#elif (FD_BMS_TYPE == M32)
#define SeriesNum  (13)
#define CapacityFactory (116)
#define AFE_ODC1       		(400) 
#define AFE_ODC2       		(600) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "M32"
#elif (FD_BMS_TYPE == T3MAX)
#define SeriesNum  (10)
#define CapacityFactory (180)
#define AFE_ODC1       		(400) 
#define AFE_ODC2       		(600) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "T3MAX"
#elif (FD_BMS_TYPE == T3)
#define SeriesNum  (10)
#define CapacityFactory (270)
#define AFE_ODC1       		(600) 
#define AFE_ODC2       		(800) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "T3"
#elif (FD_BMS_TYPE == M25)
#define SeriesNum  (13)
#define CapacityFactory (145)
#define AFE_ODC1       		(500) 
#define AFE_ODC2       		(800) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "M25"
#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D006" 
#elif (FD_BMS_TYPE == T1_AND_T2)
#define SeriesNum  (13)
#define CapacityFactory (225)
#define AFE_ODC1       		(500) 
#define AFE_ODC2       		(800) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "T1/T2"
#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D007" 
#elif (FD_BMS_TYPE == D3PRO)
#define SeriesNum  (10)
#define CapacityFactory (78)
#define AFE_ODC1       		(300) 
#define AFE_ODC2       		(500) 
#define  BMS_HARDWARE_VERDION_DEFAULT   "D3PRO"
#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D003" 
#elif (FD_BMS_TYPE == C11_AND_C11pro)
#define SeriesNum  (13)
#define CapacityFactory (104)
#define  BMS_HARDWARE_VERDION_DEFAULT   "C11"
#define  BMS_SOFTWARE_VERDION_DEFAULT  	"D002" 
#define AFE_ODC1       		(400) 
#define AFE_ODC2       		(600) 
#else
#define SeriesNum  (13)
#define CapacityFactory (100)
#define  BMS_HARDWARE_VERDION_DEFAULT   "cs_666_test"
#define AFE_ODC1       		(400) 
#define AFE_ODC2       		(1000) 
#endif

#if (FD_BMS_TYPE == C21)
#define CS_Res			  2
#define CS_Res_Num		2
#elif (FD_BMS_TYPE == T1_AND_T2)
#define CS_Res			  2
#define CS_Res_Num		3
#elif (FD_BMS_TYPE == M25)
#define CS_Res			  2
#define CS_Res_Num		3
#else
#define CS_Res			  2
#define CS_Res_Num		2
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

// #define __INIT_SOC__        (99)

#define FAC_INIT_soc (60)

/*
 * 升级后一次性参数重�?控制�?
 * 默�?�值为 0，表示本次固件不触发该类参数重置�?
 * 如果你想在某次升级后强制重置，把对应 epoch 改成一�?新的�? 0 值�?
 * 设�?��?��?�运行到这版固件时会执�?�一次重�?，并�? epoch 落盘；后�?重启不会重�?�执行�?
 */
#ifndef FW_UPGRADE_RESET_PROTECT_EPOCH
#define FW_UPGRADE_RESET_PROTECT_EPOCH   0u
#endif

#ifndef FW_UPGRADE_RESET_SYSTEM_EPOCH
#define FW_UPGRADE_RESET_SYSTEM_EPOCH    0u
#endif

#ifndef FW_UPGRADE_RESET_SOC_EPOCH
#define FW_UPGRADE_RESET_SOC_EPOCH       0u
#endif

#ifndef FW_UPGRADE_RESET_EVENT_LOG_EPOCH
#define FW_UPGRADE_RESET_EVENT_LOG_EPOCH 0u
#endif

#ifndef FW_UPGRADE_RESET_RUNTIME_EPOCH
#define FW_UPGRADE_RESET_RUNTIME_EPOCH   0u
#endif

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
  bool     enable_current_test;
  bool     enable_log_test_first;
  bool     enable_log_test_balance;
  bool     enable_kv_test;
  uint16_t cnt1;
  uint16_t cnt2;
  uint16_t cnt3;
}Time_T;

extern Time_T  sys_time;

#endif
