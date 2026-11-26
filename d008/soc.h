#pragma once
#include "control.h"
#include "meter.h"
typedef struct {
    int64_t remaining_uas;
    uint8_t percent;
} SocState;
typedef struct {
    Meter meter;
    SocState state;
    bool initialized, charging, discharging, near_full, near_empty, confirmed_full;
    uint32_t direction_ms, edge_ms, display_ms;
} Soc;

void soc_manual(Soc *soc, const Config *c, unsigned percent, uint32_t ms);
void soc_sample(Soc *soc, const Config *c, const Sample *s, const Outputs *output,
                bool valid, bool voltage_valid, uint32_t tick, uint32_t ms);
