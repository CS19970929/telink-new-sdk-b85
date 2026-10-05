#ifndef BMS_PRODUCT_CONF_H_
#define BMS_PRODUCT_CONF_H_

#include "sh3673510_project_config.h"

/* Production feature selection. D014 uses fixed Modbus RTU over isolated RS485; SIF/one-wire is not used. */
#define _UL_RENZHENG_ENABLE_
#define _FUNC_UART_
#define MODBUS_RS485_ENABLE              1
/* Board-only UART DMA/RS485 line test. Never enabled in production by default. */
#ifndef BMS_RS485_TX_DIAG_ENABLE
#define BMS_RS485_TX_DIAG_ENABLE         0
#endif
#ifndef FAC_TEST
#define _DI_SWITCH_SYS_ONOFF
#endif

#define __SLEEP_VNORMAL__             (3000)
#define __SLEEP_TIMENORMAL__          (60 * 60 * 24)
#define __SLEEP_VLOW__                (2800)
#define __SLEEP_TIMEVLOW__            (60 * 60 * 1)

/* Keep legacy numeric product IDs stable for protocol/storage compatibility. */
#define FD_BMS_TYPE 2u
#define SeriesNum                      SH3673510_BOARD_CELL_COUNT

/*
 * Product requirements not present on the schematic are intentionally not
 * invented here. CapacityFactory remains a development
 * default until D014 product parameters are signed off; protection is still
 * governed by the persisted software profile + independent AFE HW profile.
 */
#define CapacityFactory                116
#define BMS_HARDWARE_VERDION_DEFAULT   "D014"
#define BMS_SOFTWARE_VERDION_DEFAULT   "V1.6"
#define BMS_SERIAL_NUMBER_DEFAULT      "D014-20260925"

/* Legacy SOC/current-sense compatibility fields; SH3673510 current conversion
 * uses SH3673510_BOARD_SHUNT_UOHM (667uOhm) directly. */

#define DEV_NAME_STR  "BT_D014"
#define DEV_NAME_LEN  (sizeof(DEV_NAME_STR)-1)
#define DEV_NAME_STR2 "BT_D014_FACTORY"
#define DEV_NAME_LEN2 (sizeof(DEV_NAME_STR2)-1)


#define BMS_DEFAULT_CUV3_MV 3000u
#define BMS_DEFAULT_CUV3_FILTER 1000u
#endif
