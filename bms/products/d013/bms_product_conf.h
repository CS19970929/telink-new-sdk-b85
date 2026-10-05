#ifndef BMS_PRODUCT_CONF_H_
#define BMS_PRODUCT_CONF_H_

#include "sh3673510_project_config.h"

/* Production feature selection. D013 uses fixed Modbus RTU over direct UART; SIF/one-wire is not used. */
#define _UL_RENZHENG_ENABLE_
#define _FUNC_UART_
#define MODBUS_RS485_ENABLE              0
#ifndef FAC_TEST
#define _DI_SWITCH_SYS_ONOFF
#endif

#define __SLEEP_VNORMAL__             (3000)
#define __SLEEP_TIMENORMAL__          (60 * 60 * 24)
#define __SLEEP_VLOW__                (2800)
#define __SLEEP_TIMEVLOW__            (60 * 60 * 1)

/* Keep legacy numeric wire product ID stable; storage has its own product tag. */
#define FD_BMS_TYPE 2u
#define SeriesNum                      SH3673510_BOARD_CELL_COUNT
/* Existing D11 product capacity: Ah*10. It is product data, not inferred from schematic. */
#define CapacityFactory                116
#define AFE_ODC1                       300
#define AFE_ODC2                       500
#define BMS_HARDWARE_VERDION_DEFAULT   "D013"
#define BMS_SOFTWARE_VERDION_DEFAULT   "V1.0"
#define BMS_SERIAL_NUMBER_DEFAULT      "D013-UNSET"

/* Legacy SOC/current-sense compatibility values: 2mV : 20A. */

#define DEV_NAME_STR  "BT_D013"
#define DEV_NAME_LEN  (sizeof(DEV_NAME_STR)-1)
#define DEV_NAME_STR2 "BT_D013_FACTORY"
#define DEV_NAME_LEN2 (sizeof(DEV_NAME_STR2)-1)


#define BMS_DEFAULT_CUV3_MV 3000u
#define BMS_DEFAULT_CUV3_FILTER 1000u
#endif
