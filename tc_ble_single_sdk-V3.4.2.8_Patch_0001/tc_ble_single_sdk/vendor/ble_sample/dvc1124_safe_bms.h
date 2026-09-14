#ifndef DVC1124_SAFE_BMS_H_
#define DVC1124_SAFE_BMS_H_

#include <stdint.h>

typedef struct
{
    uint8_t output_enabled;
    uint8_t output_inhibit;
    uint8_t requested_charge_on;
    uint8_t requested_discharge_on;
    uint8_t snapshot_valid_streak;
    uint8_t comm_failure_count;
    uint8_t reinit_cooldown_samples;
    uint8_t short_latched;
    uint16_t short_clear_samples;
} dvc1124_safe_bms_status_t;

void DVC1124_SafeBmsGetStatus(dvc1124_safe_bms_status_t *status);

#endif /* DVC1124_SAFE_BMS_H_ */
