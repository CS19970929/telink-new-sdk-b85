#ifndef BMS_FEATURES_H_
#define BMS_FEATURES_H_

#include <stdint.h>
#include "bms_afe.h"

/* Common feature service is called after each valid AFE sample (currently 200 ms). */
#define BMS_FEATURE_SERVICE_PERIOD_MS 200u

/* Temperature encoding throughout the existing firmware is (degC + 40) * 10. */
#ifndef BMS_HEATER_START_TEMP_X10
#define BMS_HEATER_START_TEMP_X10 400u /* start only below 0 degC */
#endif
#ifndef BMS_HEATER_STOP_TEMP_X10
#define BMS_HEATER_STOP_TEMP_X10 450u  /* stop at +5 degC: deliberate hysteresis */
#endif

#ifndef BMS_OPENWIRE_FIRST_IDLE_MS
#define BMS_OPENWIRE_FIRST_IDLE_MS 10000u
#endif
#ifndef BMS_OPENWIRE_PERIOD_MS
#define BMS_OPENWIRE_PERIOD_MS 300000u
#endif

void bms_features_init(void);
void bms_features_service(void);
void bms_features_on_afe_invalid(void);

uint8_t bms_features_heater_on(void);
uint8_t bms_features_heater_fuse_fired(void);
uint8_t bms_features_charge_blocked(void);
uint8_t bms_features_discharge_blocked(void);
uint8_t bms_features_openwire_active(void);
void bms_features_get_openwire_result(bms_afe_openwire_result_t *result);

#endif /* BMS_FEATURES_H_ */
