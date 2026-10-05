/* 文件功能：产品容量、名称、采样及通信功能配置。 */
#ifndef BMS_PRODUCT_CONF_H_
#define BMS_PRODUCT_CONF_H_

// #include "types.h"
// #include "tl_common.h"
// #include "drivers.h"
#include "dvc1124_project_config.h"

// #define __VIRTURE_CURRENT__
// #define FAC_TEST
// #define DISP_VBAT_AND_TEMP_

/*
 * 仅功耗对比：0 取消周期唤醒期限，采样依赖其他事件，
 * 可能错过 DVC Open-Wire COW 窗口；正常保护/SOC 时序保持 1，修改后重编译。
 */
#ifndef BMS_APP_SAMPLE_WAKEUP_ENABLE
#define BMS_APP_SAMPLE_WAKEUP_ENABLE 1u
#endif
#if (BMS_APP_SAMPLE_WAKEUP_ENABLE != 0u) && (BMS_APP_SAMPLE_WAKEUP_ENABLE != 1u)
#error "BMS_APP_SAMPLE_WAKEUP_ENABLE must be 0 or 1"
#endif

/* D008 测量下限：|I| <= 200 mA 不适合可靠上报/积分。 */

#define _FUNC_SIF_
#define _FUNC_UART_

/* D008 无独立开关；ACC-MCU 是预留接口，不是供电请求。 */

#define __SLEEP_VNORMAL__             	(3000)
#define	__SLEEP_TIMENORMAL__	          (60 * 60 * 24)
#define __SLEEP_VLOW__     		          (2800)
#define	__SLEEP_TIMEVLOW__		          (60 * 60 * 1)

/* 移除无关遗留产品表时保留持久化类型 ID 和出厂容量回退，容量单位 Ah * 10。 */
#define FD_BMS_TYPE   12u
#define CapacityFactory 78u

#define  BMS_SOFTWARE_VERDION_DEFAULT  	"V8.8"

/*
 * HS-D008 为 24S DVC1124-2 板；容量/保护值保留原参数存储，物理串数应与本分支一致；
 * 20S 装配可覆盖 DVC1124_DEFAULT_CELL_COUNT。
 */
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


/* HS-D008 原理图 MCU 物理网络。 */
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
