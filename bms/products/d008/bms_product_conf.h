#ifndef BMS_PRODUCT_CONF_H_
#define BMS_PRODUCT_CONF_H_

// #include "types.h"
// #include "tl_common.h"
// #include "drivers.h"
#include "dvc1124_project_config.h"

// #define __VIRTURE_CURRENT__
// #define FAC_TEST
// #define DISP_VBAT_AND_TEMP_
// #define __TEST_SOC__

/* Power comparison only: 0 removes the application's periodic wake deadline.
 * Sampling then depends on other wake events and can miss the DVC Open-Wire
 * COW window. Keep 1 for normal protection/SOC timing. Rebuild after changing. */
#ifndef BMS_APP_SAMPLE_WAKEUP_ENABLE
#define BMS_APP_SAMPLE_WAKEUP_ENABLE 1u
#endif
#if (BMS_APP_SAMPLE_WAKEUP_ENABLE != 0u) && (BMS_APP_SAMPLE_WAKEUP_ENABLE != 1u)
#error "BMS_APP_SAMPLE_WAKEUP_ENABLE must be 0 or 1"
#endif

/* D008 measurement floor: |I| <= 200 mA is not reliable for reporting/integration. */

#define _FUNC_SIF_
#define _FUNC_UART_

/* D008 has no discrete switch; ACC-MCU is reserved, not a power request. */

#define __SLEEP_VNORMAL__             	(3000)
#define	__SLEEP_TIMENORMAL__	          (60 * 60 * 24)
#define __SLEEP_VLOW__     		          (2800)
#define	__SLEEP_TIMEVLOW__		          (60 * 60 * 1)

/* Preserve the existing persisted type ID and factory-capacity fallback while
 * removing the unrelated legacy product table. Capacity is in Ah * 10. */
#define FD_BMS_TYPE   12u
#define CapacityFactory 78u

#define  BMS_SOFTWARE_VERDION_DEFAULT  	"V8.8"

/* HS-D008 is a 24S DVC1124-2 board. Keep capacity/protection product values in
 * the existing parameter store, but make the physical cell-count identity
 * correct for this branch. A 20S assembly can override DVC1124_DEFAULT_CELL_COUNT. */
#define DVC1124_D008_PROJECT 1
#if DVC1124_D008_PROJECT
#undef SeriesNum
#define SeriesNum  (DVC1124_DEFAULT_CELL_COUNT)
#undef BMS_HARDWARE_VERDION_DEFAULT
#define BMS_HARDWARE_VERDION_DEFAULT "D008"
#endif

#define  BMS_SERIAL_NUMBER_DEFAULT  	"D008-20260930"

#define DEV_NAME_STR  "BT_FD190126F03200046_007"
#define DEV_NAME_LEN  (sizeof(DEV_NAME_STR)-1)

#define DEV_NAME_STR2  "BT_FD260228F03200046_666"
#define DEV_NAME_LEN2  (sizeof(DEV_NAME_STR2)-1)


/* HS-D008 physical MCU nets from the schematic. */
#define RF_EN_PIN              (GPIO_PD4)
#define AFE1_PRO_EN_PIN        (GPIO_PD7)
#define ACC_MCU_PIN            (GPIO_PA0)
#define HEATER_EN_PIN          (GPIO_PA1)
#define CHG_IN_PIN             (GPIO_PB1)
#define OWC_TX_PIN             (GPIO_PC2)
#define OWC_RX_PIN             (GPIO_PC3)
#define MCU_LDO_PIN            (GPIO_PC4)
#define SOC25_PIN              (GPIO_PB4)
#define SOC50_PIN              (GPIO_PB5)
#define SOC75_PIN              (GPIO_PB7)
#define SOC100_PIN             (GPIO_PD3)
#define LED_BLUE_PIN           (GPIO_PB4)


#define BMS_DEFAULT_CUV3_MV 2200u
#define BMS_DEFAULT_CUV3_FILTER 100u
#endif
