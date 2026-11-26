#pragma once
#include "product.h"

enum { CHARGE = 1, DISCHARGE = 2 };
typedef struct {
    uint16_t latched, pending;
    uint32_t since[LIMIT_COUNT], load_since, heat_since, fuse_since;
    bool load_seen, charging, heat, fuse_timing, fuse_trip, balance_hot;
    uint32_t balance;
} Control;
typedef struct {
    uint8_t drive, ack;
    uint32_t balance;
    bool heat, fuse, empty_trip, full_trip;
} Outputs;
Outputs control_sample(Control *state, const Config *c, const Sample *s,
                       uint32_t ms, uint8_t allowed, bool load_removed, bool wire_fault);
void control_gap(Control *state);
