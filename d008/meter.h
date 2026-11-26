#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TICKS_PER_MS 16000u
#define SAMPLE_TICKS (200u * TICKS_PER_MS)
#define MAX_MEASURED_GAP (5u * SAMPLE_TICKS)

typedef struct {
    uint32_t last_tick;
    int32_t last_ma;
    int32_t remainder;
    bool started;
    bool last_valid;
} Meter;

/* 每个到期的主循环只取一次样，返回越过的计划周期数。 */
uint32_t sample_periods(uint32_t now, uint32_t *phase);
/* 每个闭合区间的放电量，单位 µA·s，充电返回负值。 */
int64_t meter_sample(Meter *meter, uint32_t now, int32_t ma, bool valid,
                     uint32_t *missing_ticks);
