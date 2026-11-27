#pragma once
#include "product.h"

enum { CHARGE = 1, DISCHARGE = 2 };
typedef struct {
    uint16_t latched, pending;
    uint32_t since[LIMIT_COUNT], load_since;
    uint8_t current_faults;
    bool load_seen, balance_hot;
    uint32_t balance;
} Control;
typedef struct {
    uint8_t drive, ack, reverse_modes;
    uint32_t balance;
    bool empty_trip, full_trip;
} Outputs;
Outputs control_sample(Control *state, const Config *c, const Sample *s,
                       uint32_t ms, uint8_t allowed, bool current_qualified,
                       bool load_removed);
void control_acknowledged(Control *state, uint8_t ack, uint8_t alarms);
void control_gap(Control *state);
