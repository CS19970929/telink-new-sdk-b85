#ifndef CONF_H_
#define CONF_H_

#include "../../common/types.h"
#include "stdint.h"
#include "flash_store_cfg.h"
#include "bms_afe_backend.h"
#include "sh3673510_project_config.h"

/* Production feature selection. D011 uses fixed Modbus RTU over RS485; SIF/one-wire is not used. */
#define _UL_RENZHENG_ENABLE_
#define _FUNC_UART_
#define MODBUS_RS485_ENABLE              1
#ifndef FAC_TEST
#define _DI_SWITCH_SYS_ONOFF
#endif

#define __SLEEP_VNORMAL__             (3000)
#define __SLEEP_TIMENORMAL__          (60 * 60 * 24)
#define __SLEEP_VLOW__                (2800)
#define __SLEEP_TIMEVLOW__            (60 * 60 * 1)

/* Keep legacy numeric product IDs stable for protocol/storage compatibility. */
#define C21             0
#define C31             1
#define D11             2
#define D31             3
#define C700            4
#define M1PRO           5
#define M23             6
#define M32             7
#define T3MAX           8
#define T3              9
#define M25             10
#define T1_AND_T2       11
#define D3PRO           12
#define C11_AND_C11pro  13
#define test_default    14

/* HS-D011-10S50A is the active product on this branch. */
#define FD_BMS_TYPE                    D11
#define SeriesNum                      SH3673510_D011_CELL_COUNT
/* Existing D11 product capacity: Ah*10. It is product data, not inferred from schematic. */
#define CapacityFactory                116
#define AFE_ODC1                       300
#define AFE_ODC2                       500
#define BMS_HARDWARE_VERDION_DEFAULT   "D011"
#define BMS_SOFTWARE_VERDION_DEFAULT   "V1.0"
#define BMS_SERIAL_NUMBER_DEFAULT      "D011-UNSET"

/* Legacy SOC/current-sense compatibility values. */
#define CS_Res                         2
#define CS_Res_Num                     2

#define DEV_NAME_STR  "BT_D011"
#define DEV_NAME_LEN  (sizeof(DEV_NAME_STR)-1)
#define DEV_NAME_STR2 "BT_D011_FACTORY"
#define DEV_NAME_LEN2 (sizeof(DEV_NAME_STR2)-1)

typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t  INT32;
typedef int16_t  INT16;
typedef int8_t   INT8;

#define FAC_INIT_soc (60)

typedef enum _CUR {
    CurCHG = 0,
    CurDSG
} _Cur;

#define UPDNLMT16(Var,Max,Min) {(Var)=((Var)>=(Max))?(Max):(Var);(Var)=((Var)<=(Min))?(Min):(Var);}
#define Feed_IWatchDog ;
#define log_i(...) ;

/* D011 board code uses only canonical D011_* schematic nets from
 * sh3673510_project_config.h. Legacy cross-board GPIO aliases are forbidden. */

typedef struct
{
    uint16_t cnt_PA0_irq;
    uint16_t cnt_bms1_keyirq;
    uint16_t bq33100_read_cnt;
    uint16_t pec_err_cnt;
    uint8_t isdebugenable;
    uint16_t CHG;
    uint16_t DSG;
    uint16_t cnt_enter_chg_open;
    uint16_t cnt_enter_dsg_open;
    uint8_t wakeup_reason;
    bool wakeup_rtc;
    uint8_t time_enter_rtc;
    bool power_on;
    uint16_t enter_rtc_delay;
    bool low_power_mode;
    bool enable_current_test;
    bool enable_log_test_first;
    bool enable_log_test_balance;
    bool enable_kv_test;
    uint16_t cnt1;
    uint16_t cnt2;
    uint16_t cnt3;
} Time_T;

extern Time_T sys_time;

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
#define FW_UPGRADE_RESET_RUNTIME_EPOCH   0x0001
#endif

#endif /* CONF_H_ */
