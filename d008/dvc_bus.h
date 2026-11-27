#pragma once
#include <stdbool.h>
#include <stdint.h>
void dvc_bus_init(void);
bool dvc_read(uint8_t reg, uint8_t *data, uint8_t length);
bool dvc_write(uint8_t reg, const uint8_t *data, uint8_t length);
bool dvc_set(uint8_t reg, uint8_t value, uint8_t mask);
typedef enum { DVC_STARTUP_WAIT, DVC_STARTUP_OK, DVC_STARTUP_ERROR } DvcStartupResult;
typedef struct {
    uint32_t wake_tick, last_try_tick;
    bool retrying;
} DvcStartup;
/* 每次开始 AFE 初始化时，由调用者清零状态。 */
DvcStartupResult dvc_startup_off(DvcStartup *startup);
