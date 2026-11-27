#pragma once
#include "control.h"
#include "meter.h"
typedef struct {
    int64_t remaining_uas;
    uint8_t percent;
} SocState;
typedef struct {
    int64_t remaining_uas;
    uint64_t discharged_uas;
    uint32_t soc_id, cycle_id;
    uint16_t capacity_dah, percent;
    uint32_t factory_id;
} SocSnapshot;

typedef struct {
    Meter meter;
    SocState state;
    uint64_t discharged_uas;
    uint32_t soc_id, cycle_id, factory_id, discharge_remainder;
    bool initialized, charging, discharging, near_full, near_empty, confirmed_full;
    uint32_t direction_ms, edge_ms, display_ms;
} Soc;

SocSnapshot soc_snapshot(const Soc *soc,const Config *c);
bool soc_restore(Soc *soc,const Config *c,const SocSnapshot *snapshot);
void soc_resize(Soc *soc,unsigned previous_capacity,unsigned capacity);
uint16_t soc_cycles(const Soc *soc,const Config *c);
void soc_set_cycles(Soc *soc,const Config *c,unsigned cycles);
unsigned soc_estimate(const Config *c,const Sample *s);
void soc_manual(Soc *soc, const Config *c, unsigned percent, uint32_t ms);
void soc_sample(Soc *soc, const Config *c, const Sample *s, const Outputs *output,
                bool valid, bool voltage_valid, uint32_t tick, uint32_t ms);
