/* 文件功能：产品容量、名称、采样及通信功能配置。 */
#ifndef BMS_PRODUCT_CONF_H_
#define BMS_PRODUCT_CONF_H_

#include "sh3673510_project_config.h"

/* 量产选择：D013 固定直连 UART Modbus RTU，不使用 SIF/单线。 */
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

/* 保持历史数字线协议产品 ID；存储有独立标记。 */
#define FD_BMS_TYPE 2u
#define SeriesNum                      SH3673510_BOARD_CELL_COUNT
/* 现有 D11 容量单位 Ah*10，是产品数据，不能由原理图推断。 */
#define CapacityFactory                116
#define BMS_HARDWARE_VERDION_DEFAULT   "D013"
#define BMS_SOFTWARE_VERDION_DEFAULT   "V1.0"
#define BMS_SERIAL_NUMBER_DEFAULT      "D013-UNSET"


#define DEV_NAME_STR  "BT_D013"
#define DEV_NAME_LEN  (sizeof(DEV_NAME_STR)-1)
#define DEV_NAME_STR2 "BT_D013_FACTORY"
#define DEV_NAME_LEN2 (sizeof(DEV_NAME_STR2)-1)


#define BMS_DEFAULT_CUV3_MV 3000u
#define BMS_DEFAULT_CUV3_FILTER 1000u
#endif
