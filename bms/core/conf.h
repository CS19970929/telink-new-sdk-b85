#ifndef CONF_H_
#define CONF_H_
#include "common/types.h"
#include <stdint.h>
#include "flash_store_cfg.h"
#include "bms_afe_backend.h"
#include "bms_product_conf.h"
#define FAC_INIT_soc 60u
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef int16_t INT16;
typedef int8_t INT8;

typedef enum _CUR {
CurCHG = 0, CurDSG
}_Cur;

#define UPDNLMT16(Var,Max,Min)	{(Var)=((Var)>=(Max))?(Max):(Var);(Var)=((Var)<=(Min))?(Min):(Var);}

#define Feed_IWatchDog ;
#define log_i(...)   ;

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


#define BMS_STATE_SAVE_INTERVAL_32K (60u * 32000u)
#define BMS_STORAGE_RETRY_INTERVAL_32K (5u * 32000u)
#define BMS_EVENT_SAVE_INTERVAL_32K (60u * 32000u)
#endif
