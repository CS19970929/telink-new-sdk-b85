/* D011：10S / 250 uOhm；TS3 加热、TS4 MOS 10K-3435，PB5 启动保持低。 */
#pragma once

#define SH3673510_BOARD_CELL_COUNT              10u
#define SH3673510_BOARD_SHUNT_UOHM              250u
#define SH3673510_PRODUCT_BALANCE_SUPPORTED      1u
#define SH3673510_PRODUCT_HEATER_SUPPORTED       1u
#define SH3673510_PRODUCT_HEATER_NTC_SUPPORTED 1u
#define SH3673510_PRODUCT_MOS_NTC_SUPPORTED 1u

/* 不可逆加热熔断必须经独立状态机授权；启动保持无效电平。 */
#define BMS_BOARD_HEATER_CHG_PIN                     GPIO_PB4
#define BMS_BOARD_HEATER_FUSE_TRIGGER_PIN              GPIO_PB5  /*
 * 不可逆加热熔断触发；独立熔断状态机验证并授权前保持低。
 */
#define BMS_BOARD_HEATER_FUSE_SAFE_LEVEL               0u

#include "../sh3673510_defaults.h"
