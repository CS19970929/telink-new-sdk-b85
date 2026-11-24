/* 只转换回放格式。所有 SOC 数值由固件使用的 bms_soc.c 计算。 */
#include "bms_soc.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    bms_soc_t soc;
    bms_soc_config_t config = {{0, 0}, 0, 0, 0};
    bms_soc_checkpoint_t checkpoint = {0};
    bms_soc_sample_t input = {0};
    unsigned flags;
    uint8_t initialized = 0u;

    if (argc != 5) return 2;
    config.capacity_0p1ah = (uint16_t)strtoul(argv[1], 0, 10);
    config.chemistry = (uint8_t)strtoul(argv[2], 0, 10);
    config.cell_count = (uint8_t)strtoul(argv[3], 0, 10);
    checkpoint.shown = (uint8_t)strtoul(argv[4], 0, 10);
    bms_soc_defaults(config.chemistry, &config.limits);
    if (checkpoint.shown <= 100u) {
        checkpoint.capacity_0p1ah = config.capacity_0p1ah;
        checkpoint.charge_as10 = (uint32_t)config.capacity_0p1ah *
            BMS_SOC_UNITS_PER_0P1AH * checkpoint.shown / 100u;
    }

    puts("timestamp_ms,charge_as10,estimate,shown,endpoint,cycles");
    while (scanf("%" SCNu32 " %" SCNu32 " %" SCNd32 " %" SCNu16 " %" SCNu16
                 " %" SCNu16 " %" SCNu16 " %u", &input.now_ms, &input.current_time_ms,
                 &input.current_ma, &input.min_mv, &input.max_mv, &input.mean_mv,
                 &input.temperature_x10, &flags) == 8) {
        if (!initialized || (flags & 256u)) {
            if (initialized) checkpoint = soc.state;
            bms_soc_init(&soc, &config, &checkpoint, input.now_ms);
            initialized = 1u;
        }
        input.current_valid = (flags & 1u) != 0u;
        input.voltage_valid = (flags & 2u) != 0u;
        input.voltage_fresh = (flags & 4u) != 0u;
        input.charge_cutoff = (flags & 8u) != 0u;
        input.discharge_cutoff = (flags & 16u) != 0u;
        input.resting_allowed = (flags & 32u) != 0u;
        input.line_check_active = (flags & 64u) != 0u;
        input.line_check_passed = (flags & 128u) != 0u;
        (void)bms_soc_step(&soc, &input);
        printf("%" PRIu32 ",%" PRIu32 ",%u,%u,%u,%u\n",
               input.now_ms, soc.state.charge_as10, (unsigned)bms_soc_percent(&soc),
               (unsigned)soc.state.shown, (unsigned)soc.state.endpoint, (unsigned)soc.state.cycles);
    }
    return initialized && feof(stdin) ? 0 : 2;
}
