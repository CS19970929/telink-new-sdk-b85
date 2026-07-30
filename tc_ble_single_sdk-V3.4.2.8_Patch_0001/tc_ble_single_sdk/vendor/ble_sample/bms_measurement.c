#include "bms_measurement.h"
#include <string.h>

static bms_measurement_t g_bms_measurement;

void bms_measurement_init(void)
{
    memset(&g_bms_measurement, 0, sizeof(g_bms_measurement));
}

void bms_measurement_publish(const bms_measurement_t *measurement)
{
    if (measurement != 0) {
        g_bms_measurement = *measurement;
    }
}

void bms_measurement_invalidate_all(uint32_t now_ms)
{
    g_bms_measurement.afe_frame_valid = 0u;
    g_bms_measurement.pack_adc_valid = 0u;
    g_bms_measurement.current_valid = 0u;
    g_bms_measurement.battery_temp_valid = 0u;
    g_bms_measurement.mos_temp_valid = 0u;
    g_bms_measurement.cell_valid_mask = 0u;
    g_bms_measurement.timestamp_ms = now_ms;
}

void bms_measurement_note_wakeup(uint32_t now_ms)
{
    bms_measurement_invalidate_all(now_ms);
    g_bms_measurement.afe_timestamp_ms = now_ms;
    g_bms_measurement.adc_timestamp_ms = now_ms;
}

const bms_measurement_t *bms_measurement_get(void)
{
    return &g_bms_measurement;
}

uint8_t bms_measurement_is_fresh_and_complete(uint32_t now_ms)
{
    uint32_t required_mask;

    if ((g_bms_measurement.series_count == 0u) ||
        (g_bms_measurement.series_count > BMS_CELL_MAX)) {
        return 0u;
    }
    required_mask = (1UL << g_bms_measurement.series_count) - 1UL;
    if ((g_bms_measurement.cell_valid_mask & required_mask) != required_mask) {
        return 0u;
    }
    if (!g_bms_measurement.afe_frame_valid ||
        !g_bms_measurement.pack_adc_valid ||
        !g_bms_measurement.current_valid ||
        !g_bms_measurement.battery_temp_valid ||
        !g_bms_measurement.mos_temp_valid) {
        return 0u;
    }
    if ((uint32_t)(now_ms - g_bms_measurement.afe_timestamp_ms) > BMS_SAMPLE_STALE_MS ||
        (uint32_t)(now_ms - g_bms_measurement.adc_timestamp_ms) > BMS_SAMPLE_STALE_MS) {
        return 0u;
    }
    return 1u;
}
