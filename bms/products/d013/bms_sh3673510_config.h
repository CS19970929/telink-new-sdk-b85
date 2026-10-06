/* D013：4S / 100 uOhm；无 balance/heater/MOS NTC，板级映射待实板确认。 */
#pragma once

#define SH3673510_BOARD_CELL_COUNT               4u
#define SH3673510_BOARD_SHUNT_UOHM              100u
#define SH3673510_PRODUCT_BALANCE_SUPPORTED      0u /* 待验证硬件。 */
#define SH3673510_PRODUCT_HEATER_SUPPORTED       0u /* 待验证硬件。 */
#define SH3673510_PRODUCT_HEATER_NTC_SUPPORTED 0u
#define SH3673510_PRODUCT_MOS_NTC_SUPPORTED 0u

#include "../sh3673510_defaults.h"
