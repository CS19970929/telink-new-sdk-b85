#ifndef CONF_H_
#define CONF_H_

#include "../../common/types.h"
#include <stdint.h>

#include "flash_store_cfg.h"
#include "dvc1124.h"
#include "dvc1124_config_store.h"

/* Active product features for HS-D008. */
#define _UL_RENZHENG_ENABLE_
#define _FUNC_SIF_
#define _FUNC_UART_
#define _DI_SWITCH_SYS_ONOFF

#define __SLEEP_VNORMAL__      3000u
#define __SLEEP_TIMENORMAL__   (60u * 60u * 24u)
#define __SLEEP_VLOW__         2800u
#define __SLEEP_TIMEVLOW__     (60u * 60u)

/*
 * Current product defaults.
 * FD_BMS_TYPE keeps the deployed legacy protocol value (D3PRO == 12); the
 * physical board identity and cell count are HS-D008 / DVC1124-2.
 */
#define FD_BMS_TYPE                     12u
#define SeriesNum                       DVC1124_DEFAULT_CELL_COUNT
#define CapacityFactory                 78u
#define AFE_ODC1                        300u
#define AFE_ODC2                        500u
#define BMS_HARDWARE_VERDION_DEFAULT    "D008"
#define BMS_SOFTWARE_VERDION_DEFAULT    "V1.0"
#define BMS_SERIAL_NUMBER_DEFAULT       "D003-20260817"

/* Legacy SH367309 current-sense defaults still used by the compatibility unit. */
#define CS_Res                          2u
#define CS_Res_Num                      2u

#define DEV_NAME_STR  "BT_FD190126F03200046_007"
#define DEV_NAME_LEN  (sizeof(DEV_NAME_STR) - 1u)

#define FAC_INIT_soc 60u

typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t  INT32;
typedef int16_t  INT16;
typedef int8_t   INT8;

/* Legacy no-op hooks still referenced inside the SH367309 compatibility code. */
#define Feed_IWatchDog ;
#define log_i(...)     ;

/* HS-D008 physical MCU nets from the schematic. */
#define RF_EN_PIN       GPIO_PD4
#define AFE1_PRO_EN_PIN GPIO_PD7
#define SW_PIN          GPIO_PA0
#define HEATER_EN_PIN   GPIO_PA1
#define CHG_IN_PIN      GPIO_PB1
#define OWC_TX_PIN      GPIO_PC2
#define OWC_RX_PIN      GPIO_PC3
#define MCU_LDO_PIN     GPIO_PC4
#define SOC25_PIN       GPIO_PB4
#define SOC50_PIN       GPIO_PB5
#define SOC75_PIN       GPIO_PB7
#define SOC100_PIN      GPIO_PD3
#define LED_BLUE_PIN    GPIO_PB6

/*
 * Virtual compatibility selectors. Do not remap these legacy SH367309 names
 * to unrelated HS-D008 physical pins while app.c still uses the compatibility
 * layer.
 */
#define MCC_C_PIN       DVC1124_VPIN_LEGACY_MCC
#define AFE_CTL_PIN     DVC1124_VPIN_AFE_CTL
#define CHG_WK_PIN      CHG_IN_PIN
#define ADC_NTC_PIN     DVC1124_VPIN_ADC_BAT
#define ADC_VBUS_PIN    DVC1124_VPIN_ADC_PACK
#define ADC_NMOS_PIN    DVC1124_VPIN_ADC_MOS
#define ADC_BUSEN_PIN   DVC1124_VPIN_NOOP0
#define ADC_EN_PIN      DVC1124_VPIN_NOOP1

/* Runtime state still shared with app.c. Keep test-only fields conditional. */
typedef struct
{
    bool low_power_mode;
#if defined(__TEST_SOC__) || defined(__VIRTURE_CURRENT__)
    uint16_t CHG;
    uint16_t DSG;
    uint8_t isdebugenable;
#endif
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
#define FW_UPGRADE_RESET_RUNTIME_EPOCH   0x0001u
#endif

/*
 * Temporary DVC1124 compatibility aliases.
 * app_config.h is parsed before Telink driver declarations complete, so these
 * aliases must remain here until app.c/sh367309_datadeal.c are fully decoupled.
 */
#if defined(DVC1124_AFE_PROJECT) && DVC1124_AFE_PROJECT && \
    defined(I2C_SLAVE_DEVICE_NO_START_EN) && !defined(DVC1124_IMPLEMENTATION)
#define App_AFEGet               DVC1124_ConfigStore_BmsApp_AFEGet
#define AFE_Reset                DVC1124_ConfigStore_AFE_Reset
#define AFE_IsReady              DVC1124_AFE_IsReady
#define AFE_Sleep                DVC1124_AFE_Sleep
#define SH367309_UpdataAfeConfig DVC1124_ConfigStore_UpdataAfeConfig
#define MTPWrite                 DVC1124_BmsCompatMTPWrite

#define adc_base_init(pin) \
    DVC1124_CompatAdcBaseInit((unsigned int)(pin))
#define adc_sample_and_get_result() \
    DVC1124_CompatAdcSample()

#define gpio_set_func(pin, func) \
    DVC1124_CompatGpioSetFunc((unsigned int)(pin), (unsigned int)(func))
#define gpio_set_input_en(pin, value) \
    DVC1124_CompatGpioSetInputEn((unsigned int)(pin), (unsigned int)(value))
#define gpio_set_output_en(pin, value) \
    DVC1124_CompatGpioSetOutputEn((unsigned int)(pin), (unsigned int)(value))
#define gpio_write(pin, value) \
    DVC1124_CompatGpioWrite((unsigned int)(pin), (unsigned int)(value))
#endif

#endif /* CONF_H_ */
