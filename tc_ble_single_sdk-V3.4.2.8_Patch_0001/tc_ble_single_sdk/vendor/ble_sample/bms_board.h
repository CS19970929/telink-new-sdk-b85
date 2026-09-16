#ifndef BMS_BOARD_H_
#define BMS_BOARD_H_

#include <stdint.h>

/*
 * Board-only feature boundary.
 *
 * The common heater/balance/open-wire policy must not know MCU GPIO numbers.
 * AFE-specific register access also does not belong here. This layer is only
 * for physical product signals such as charger-present, heater output and the
 * irreversible heater-circuit fuse trigger.
 */
void bms_board_features_init(void);
uint8_t bms_board_charge_source_present(void);
uint8_t bms_board_heater_supported(void);
void bms_board_heater_set(uint8_t enabled);
uint8_t bms_board_heater_fuse_supported(void);
void bms_board_heater_fuse_fire(void);

#endif /* BMS_BOARD_H_ */
