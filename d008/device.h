#pragma once
#include "store.h"
typedef struct {
    Config config;
    Soc soc;
    Control control;
    Outputs outputs;
    Sample sample;
    bool valid, configured, afe_fault, wire_fault, storage_fault;
    uint8_t allowed;
} Device;
extern Device device;
void device_init(void);
void device_service(uint32_t ms);
void device_measure(uint32_t tick,uint32_t ms);
bool device_save_soc(void);
bool device_set_soc(unsigned percent,uint32_t ms);
bool device_set_config(const Config *candidate);
void device_powerdown(bool acc);
bool device_busy(void);
