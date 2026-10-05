#include "bms_soc_eta.h"

#define SOC_ETA_FILTER_SHIFT                 3u
#define SOC_ETA_STABLE_SECONDS               30u
#define SOC_ETA_STABLE_TICKS                 (5u * SOC_ETA_STABLE_SECONDS)
#define SOC_ETA_VARIATION_MIN_MA             250u
#define SOC_ETA_VARIATION_PERCENT            20u
#define SOC_ETA_TAPER_PERCENT                60u

static uint32_t bms_eta_abs_i32(int32_t value)
{
    return value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
}

void bms_soc_eta_reset(bms_soc_eta_t *eta)
{
    eta->eta_filtered_current_ma = 0;
    eta->eta_variation_ma = 0u;
    eta->eta_peak_current_ma = 0u;
    eta->eta_stable_ticks = 0u;
    eta->time_to_empty_min = BMS_SOC_ETA_MINUTES_INVALID;
    eta->time_to_full_min = BMS_SOC_ETA_MINUTES_INVALID;
    eta->eta_state = BMS_SOC_ETA_INVALID;
    eta->eta_direction = BMS_SOC_ETA_DIR_NONE;
    eta->eta_confidence = 0u;
    eta->eta_valid = 0u;
}

static int32_t bms_eta_filter_step(int32_t filtered, int32_t sample)
{
    if (sample > filtered) {
        uint32_t difference = (uint32_t)sample - (uint32_t)filtered;
        return filtered + (int32_t)(difference >> SOC_ETA_FILTER_SHIFT);
    }
    if (sample < filtered) {
        uint32_t difference = (uint32_t)filtered - (uint32_t)sample;
        return filtered - (int32_t)(difference >> SOC_ETA_FILTER_SHIFT);
    }
    return filtered;
}

static uint16_t bms_eta_minutes(uint32_t capacity_as10, uint32_t current_ma)
{
    uint32_t numerator;
    uint32_t denominator;
    uint32_t remainder;
    uint32_t minutes;
    if (current_ma == 0u) return BMS_SOC_ETA_MINUTES_INVALID;
    /* minutes = capacity_as10 * 100 / current_ma / 60.  Reduce to 5/3
     * before multiplying so the maximum supported capacity stays within
     * 32 bits and no TC32 64-bit runtime helper is introduced. */
    if (capacity_as10 > (0xFFFFFFFFu / 5u))
        return BMS_SOC_ETA_MINUTES_INVALID - 1u;
    if (current_ma > (0xFFFFFFFFu / 3u)) return 0u;
    numerator = capacity_as10 * 5u;
    denominator = current_ma * 3u;
    minutes = numerator / denominator;
    remainder = numerator % denominator;
    if (remainder >= ((denominator / 2u) + (denominator & 1u))) minutes++;
    if (minutes >= BMS_SOC_ETA_MINUTES_INVALID) minutes = BMS_SOC_ETA_MINUTES_INVALID - 1u;
    return (uint16_t)minutes;
}

void bms_soc_eta_update(bms_soc_eta_t *eta, const bms_soc_eta_input_t *input)
{
    uint8_t eta_dir = input->direction;
    uint32_t magnitude;
    uint32_t deviation;
    uint32_t variation_limit;
    uint32_t confidence_drop;

    if (eta_dir == BMS_SOC_ETA_DIR_NONE) {
        bms_soc_eta_reset(eta); return;
    }
    if (eta->eta_direction != eta_dir) {
        bms_soc_eta_reset(eta);
        eta->eta_direction = eta_dir;
        eta->eta_filtered_current_ma = input->current_ma;
        eta->eta_peak_current_ma = bms_eta_abs_i32(input->current_ma);
        eta->eta_stable_ticks = 1u;
        eta->eta_state = BMS_SOC_ETA_STABILIZING;
        return;
    }

    eta->eta_filtered_current_ma =
        bms_eta_filter_step(eta->eta_filtered_current_ma, input->current_ma);
    deviation = (input->current_ma >= eta->eta_filtered_current_ma) ?
        ((uint32_t)input->current_ma -
         (uint32_t)eta->eta_filtered_current_ma) :
        ((uint32_t)eta->eta_filtered_current_ma -
         (uint32_t)input->current_ma);
    if (deviation > eta->eta_variation_ma)
        eta->eta_variation_ma +=
            (deviation - eta->eta_variation_ma) >> SOC_ETA_FILTER_SHIFT;
    else
        eta->eta_variation_ma -=
            (eta->eta_variation_ma - deviation) >> SOC_ETA_FILTER_SHIFT;
    magnitude = bms_eta_abs_i32(eta->eta_filtered_current_ma);
    if (magnitude > eta->eta_peak_current_ma)
        eta->eta_peak_current_ma = magnitude;
    if (eta->eta_stable_ticks < 65535u) eta->eta_stable_ticks++;
    if (eta->eta_stable_ticks < SOC_ETA_STABLE_TICKS) {
        eta->eta_state = BMS_SOC_ETA_STABILIZING; return;
    }

    variation_limit = (magnitude / 100u) * SOC_ETA_VARIATION_PERCENT +
        ((magnitude % 100u) * SOC_ETA_VARIATION_PERCENT) / 100u;
    if (variation_limit < SOC_ETA_VARIATION_MIN_MA)
        variation_limit = SOC_ETA_VARIATION_MIN_MA;
    if (magnitude <= 200u || magnitude <= input->deadband_ma ||
        eta->eta_variation_ma > variation_limit ||
        input->endpoint_active) {
        eta->eta_state = BMS_SOC_ETA_LOW_CONFIDENCE;
        eta->eta_valid = 0u;
        eta->eta_confidence = 20u;
        eta->time_to_empty_min = BMS_SOC_ETA_MINUTES_INVALID;
        eta->time_to_full_min = BMS_SOC_ETA_MINUTES_INVALID;
        return;
    }

    if (eta_dir == BMS_SOC_ETA_DIR_CHARGE &&
        input->near_full &&
        eta->eta_peak_current_ma > 0u &&
        magnitude < (eta->eta_peak_current_ma / 100u) * SOC_ETA_TAPER_PERCENT) {
        eta->eta_state = BMS_SOC_ETA_LOW_CONFIDENCE;
        eta->eta_valid = 0u;
        eta->eta_confidence = 10u;
        eta->time_to_empty_min = BMS_SOC_ETA_MINUTES_INVALID;
        eta->time_to_full_min = BMS_SOC_ETA_MINUTES_INVALID;
        return;
    }

    confidence_drop = eta->eta_variation_ma / (magnitude / 100u);
    if (confidence_drop > 80u) confidence_drop = 80u;
    eta->eta_confidence = (uint8_t)(100u - confidence_drop);
    eta->eta_state = BMS_SOC_ETA_VALID;
    eta->eta_valid = 1u;
    eta->time_to_empty_min = BMS_SOC_ETA_MINUTES_INVALID;
    eta->time_to_full_min = BMS_SOC_ETA_MINUTES_INVALID;
    if (eta_dir == BMS_SOC_ETA_DIR_DISCHARGE)
        eta->time_to_empty_min =
            bms_eta_minutes(input->remaining_as10, magnitude);
    else
        eta->time_to_full_min =
            bms_eta_minutes(input->full_as10 -
                            input->remaining_as10, magnitude);
}
