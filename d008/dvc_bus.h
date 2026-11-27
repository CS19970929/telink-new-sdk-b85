#pragma once
#include <stdbool.h>
#include <stdint.h>
void dvc_bus_init(void);
bool dvc_read(uint8_t reg, uint8_t *data, uint8_t length);
bool dvc_write(uint8_t reg, const uint8_t *data, uint8_t length);
bool dvc_set(uint8_t reg, uint8_t value, uint8_t mask);
bool dvc_startup_set(uint8_t reg, uint8_t value, uint8_t mask);
