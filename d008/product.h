#pragma once
#include <stdint.h>
#include <stdbool.h>

/* 此装配使用 PC0/1，GP1～4 NTC，GP5/6 低边驱动，8 × 2 mΩ。 */
#define D008_BLE_NAME "BT_FD190126F03200046_007"
#define SHUNT_UOHM 250u
#define NO_CELL 61001u
enum { OV, UV, PACK_OV, PACK_UV, CHARGE_OC, DISCHARGE_OC, CHARGE_HOT,
       CHARGE_COLD, DISCHARGE_HOT, DISCHARGE_COLD, MOS_HOT, CELL_DELTA, LIMIT_COUNT };
typedef struct {
    uint16_t trip, release, delay_ms;
} Limit;
typedef struct {
    uint16_t cells, nmc, capacity_dah, full_mv, empty_mv;
    Limit sw[LIMIT_COUNT];
    uint16_t hw_ov_mv, hw_uv_mv, hw_ov_ms, hw_uv_ms;
    uint16_t hw_d1_a10, hw_c1_a10, hw_d2_a10, hw_c2_a10, hw_sc_a10;
    uint16_t hw_d1_ms, hw_c1_ms, hw_d2_ms, hw_c2_ms, hw_sc_us;
    uint16_t balance_on, balance_mv, balance_start_mv, balance_stop_mv;
    uint16_t heat_on, heat_start_x10, heat_stop_x10;
    int32_t offset_ma, gain_ppm;
    uint8_t sn[32];
} Config;

typedef struct {
    uint16_t mv[24], min_mv, max_mv, temp_x10[4];
    uint32_t sum_mv;
    int32_t raw_ma, ma;
    uint8_t alarms, fet, min_index, max_index;
    bool current_fresh;
} Sample;

void config_defaults(Config *c, unsigned cells, bool nmc);
bool config_valid(const Config *c);
