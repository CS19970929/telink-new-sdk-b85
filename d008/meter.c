#include "meter.h"

uint32_t sample_periods(uint32_t now, uint32_t *phase)
{
    uint32_t periods = (now - *phase) / SAMPLE_TICKS;
    *phase += periods * SAMPLE_TICKS;
    return periods;
}

int64_t meter_sample(Meter *meter, uint32_t now, int32_t ma, bool valid,
                     uint32_t *missing_ticks)
{
    uint32_t dt = now - meter->last_tick;
    int64_t delta = 0;
    *missing_ticks = 0;
    if (meter->started) {
        if (valid && meter->last_valid && dt <= MAX_MEASURED_GAP) {
            /* (I0 + I1) / 2 × tick / 16000，保留正负亚微库仑。 */
            int64_t scaled = ((int64_t)ma + meter->last_ma) * dt + meter->remainder;
            delta = scaled / (2 * (int32_t)TICKS_PER_MS);
            meter->remainder = (int32_t)(scaled - delta * (2 * (int32_t)TICKS_PER_MS));
        } else {
            *missing_ticks = dt;
        }
    }
    meter->started = true;
    meter->last_tick = now;
    meter->last_valid = valid;
    meter->last_ma = ma;
    return delta;
}
