#ifndef CONF_H_
#define CONF_H_

// #include "types.h"
// #include "tl_common.h"
// #include "drivers.h"
#include "../../common/types.h"
#include "stdint.h"
#include "flash_store_cfg.h"

#define DEV_NAME_STR  "BT_star003"
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
// #define CapacityFactory (50)
#define CapacityFactory (180)

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

#endif
