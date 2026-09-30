#pragma once
/* Product inputs; register composition is in the shared SH backend. */
#ifndef BMS_BOARD_DEBUG_LED_ENABLE
#define BMS_BOARD_DEBUG_LED_ENABLE 0
#endif
#define SH3673510_BOARD_CELL_COUNT               4u
#define SH3673510_BOARD_SHUNT_UOHM              100u
#define SH3673510_PRODUCT_BALANCE_SUPPORTED      0u /* TODO_VERIFY_HW */
#define SH3673510_PRODUCT_HEATER_SUPPORTED       0u /* TODO_VERIFY_HW */
#define SH3673510_PRODUCT_HEATER_NTC_SUPPORTED 0u
#define SH3673510_PRODUCT_MOS_NTC_SUPPORTED 0u
