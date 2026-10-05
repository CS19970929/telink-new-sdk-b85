#include "bms_afe_driver.h"
#include "bms_diag.h"
#include "bms_features.h"
#include <string.h>

void bms_afe_diag_poll(void)
{
    sh3673510_fet_diag_detail_t detail;
    uint8_t command = 0u, command_valid = 0u, driver = 0u, driver_valid = 0u;
    uint8_t c = 0u, d = 0u;
    uint16_t words[11];
    uint16_t guard = bms_afe_get_guard_diagnostic_bits();
    uint16_t params = bms_diag_cached_word(144u);
    uint32_t common = 0u, charge = 0u, discharge = 0u;
    memset(&detail, 0, sizeof(detail));
    memset(words, 0, sizeof(words));
    /* Both driver getters are cached RAM only: no transaction on a silenced bus. */
    (void)sh3673510_bms_afe_get_fet_diagnostics(&command, &command_valid, &driver, &driver_valid);
    (void)sh3673510_bms_afe_get_fet_diag_detail(&detail);
    bms_afe_get_requested_fets(&c, &d);
    if (!(params & 1u)) common |= DIAG_BLOCK_PARAMS;
    if (!(params & 2u)) common |= DIAG_BLOCK_UPGRADE;
    if (!(guard & DIAG_GUARD_OUTPUT_ENABLED)) common |= DIAG_BLOCK_OUTPUT;
    if (guard & (DIAG_GUARD_COMM_INHIBIT | DIAG_GUARD_BUS_SILENCED)) common |= DIAG_BLOCK_COMM;
    if (c && (!command_valid || !(command & 1u)))
        charge = detail.charge_block_reasons | bms_features_diag_reasons(1u) | common;
    if (d && (!command_valid || !(command & 2u)))
        discharge = detail.discharge_block_reasons | bms_features_diag_reasons(0u) | common;
    bms_diag_mos((uint16_t)(c | (d << 1)), charge, discharge);
    bms_diag_command(command, command_valid);
    bms_diag_driver(driver, driver_valid);
    words[0] = detail.flag1;
    words[1] = detail.flag2;
    words[2] = params;
    words[3] = detail.bstatus2;
    words[4] = detail.backend_state;
    words[5] = guard;
    words[6] = detail.sensor_state;
    words[7] = detail.mos_ntc_raw;
    words[8] = (uint16_t)detail.mos_ntc_ohm;
    words[9] = (uint16_t)(detail.mos_ntc_ohm >> 16);
    words[10] = detail.mos_temp_x10;
    bms_diag_backend_details(words);
}
