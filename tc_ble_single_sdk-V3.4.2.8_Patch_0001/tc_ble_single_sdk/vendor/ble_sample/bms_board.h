#ifndef BMS_BOARD_H_
#define BMS_BOARD_H_

#include <stdint.h>

/*
 * Board-only feature boundary.
 *
 * The common heater/balance/open-wire policy must not know MCU GPIO numbers.
 * AFE-specific register access also does not belong here.  This layer is only
 * for physical product signals such as charger-present and heater output.
 */
void bms_board_features_init(void);
uint8_t bms_board_charge_source_present(void);
uint8_t bms_board_heater_supported(void);
uint8_t bms_board_heater_allowed(void);
uint8_t bms_board_balance_supported(void);
void bms_board_heater_set(uint8_t enabled);

#endif /* BMS_BOARD_H_ */
