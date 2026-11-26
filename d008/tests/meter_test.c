#include <assert.h>
#include <stdio.h>
#include "../meter.h"

int main(void)
{
    Meter meter = {0};
    uint32_t phase = UINT32_MAX - 1000u, now = phase, missing = 0;
    int64_t total = 0;
    uint32_t i;
    assert(!sample_periods(now, &phase));
    assert(!meter_sample(&meter, now, 1000, true, &missing));
    /* 24 小时、不等间隔，多次环回；成对的抖动不改变物理总时长。 */
    for (i = 0; i < 432000; ++i) {
        now += SAMPLE_TICKS + ((i & 1u) ? 197u : (uint32_t)-197);
        total += meter_sample(&meter, now, 1000, true, &missing);
        assert(!missing);
    }
    assert(total == INT64_C(86400000000));
    assert(sample_periods(phase + 3u*SAMPLE_TICKS + 7u, &phase) == 3);
    assert(!sample_periods(phase + 7u, &phase));
    assert(meter_sample(&meter, now += SAMPLE_TICKS, -1000, true, &missing) == 0);
    assert(meter_sample(&meter, now += SAMPLE_TICKS, -1000, true, &missing) == -200000);
    assert(!meter_sample(&meter, ++now, 0, false, &missing) && missing == 1);
    assert(!meter_sample(&meter, now += SAMPLE_TICKS, 1, true, &missing) && missing == SAMPLE_TICKS);
    assert(!meter_sample(&meter, now += MAX_MEASURED_GAP+1, 1, true, &missing)
           && missing == MAX_MEASURED_GAP+1);
    for (i = 0, total = 0; i < TICKS_PER_MS; ++i)
        total += meter_sample(&meter, ++now, 1, true, &missing);
    assert(total == 1);
    puts("PASS meter: 24h, jitter, wrap, polarity, failures and fractional charge");
    return 0;
}
