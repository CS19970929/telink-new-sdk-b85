#ifndef BMS_FEATURES_H_
#define BMS_FEATURES_H_

#include <stdint.h>
#include "bms_afe.h"

#define BMS_FEATURE_SERVICE_PERIOD_MS 200u

/* Temperature encoding: (degC + 40) * 10. */
#ifndef BMS_HEATER_ENABLE_DEFAULT
#define BMS_HEATER_ENABLE_DEFAULT 0u /* D013 board heater wiring TODO_VERIFY_HW */
#endif
#ifndef BMS_HEATER_START_TEMP_X10
#define BMS_HEATER_START_TEMP_X10 400u
#endif
#ifndef BMS_HEATER_STOP_TEMP_X10
#define BMS_HEATER_STOP_TEMP_X10 450u
#endif

/* Independent balance business defaults. These must never reuse Vdelta
 * protection parameters. Start voltage is runtime configurable. */
#ifndef BMS_BALANCE_ENABLE_DEFAULT
#define BMS_BALANCE_ENABLE_DEFAULT 0u /* D013 cell/balance wiring TODO_VERIFY_HW */
#endif
#ifndef BMS_BALANCE_START_VOLTAGE_MV_DEFAULT
#define BMS_BALANCE_START_VOLTAGE_MV_DEFAULT 3400u
#endif
#ifndef BMS_BALANCE_START_DELTA_MV_DEFAULT
#define BMS_BALANCE_START_DELTA_MV_DEFAULT 50u
#endif
#ifndef BMS_BALANCE_STOP_DELTA_MV_DEFAULT
#define BMS_BALANCE_STOP_DELTA_MV_DEFAULT 30u
#endif

#ifndef BMS_BALANCE_TRUST_CONFIRM_MS
#define BMS_BALANCE_TRUST_CONFIRM_MS 1000u
#endif
#ifndef BMS_BALANCE_CELL_PLAUSIBLE_MIN_MV
#define BMS_BALANCE_CELL_PLAUSIBLE_MIN_MV 1000u
#endif
#ifndef BMS_BALANCE_CELL_PLAUSIBLE_MAX_MV
#define BMS_BALANCE_CELL_PLAUSIBLE_MAX_MV 5000u
#endif
#ifndef BMS_BALANCE_CELL_MAX_STEP_MV
#define BMS_BALANCE_CELL_MAX_STEP_MV 250u
#endif
#ifndef BMS_BALANCE_SUSPECT_DELTA_MV
#define BMS_BALANCE_SUSPECT_DELTA_MV 1000u
#endif

#ifndef BMS_OPENWIRE_FIRST_IDLE_MS
#define BMS_OPENWIRE_FIRST_IDLE_MS 10000u
#endif
#ifndef BMS_OPENWIRE_PERIOD_MS
#define BMS_OPENWIRE_PERIOD_MS 300000u
#endif

typedef enum {
    BMS_HEATER_IDLE = 0u,
    BMS_HEATER_ARMING = 1u,
    BMS_HEATER_ACTIVE = 2u
} bms_heater_state_t;

void bms_features_init(void);
void bms_features_service(void);
void bms_features_on_afe_invalid(void);

uint8_t bms_features_heater_on(void);
bms_heater_state_t bms_features_heater_state(void);
uint8_t bms_features_charge_session_active(void);
uint8_t bms_features_balance_voltage_trusted(void);
uint8_t bms_features_openwire_suspected(void);
uint8_t bms_features_charge_hard_blocked(void);
uint8_t bms_features_charge_direction_blocked(void);
uint8_t bms_features_charge_blocked(void);
uint8_t bms_features_discharge_blocked(void);
uint8_t bms_features_openwire_active(void);
void bms_features_get_openwire_result(bms_afe_openwire_result_t *result);
uint32_t bms_features_diag_reasons(uint8_t charge);

#endif /* BMS_FEATURES_H_ */
