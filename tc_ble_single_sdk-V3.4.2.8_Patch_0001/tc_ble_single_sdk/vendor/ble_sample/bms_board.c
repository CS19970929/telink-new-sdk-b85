#include "bms_board.h"

#include "tl_common.h"
#include "drivers.h"
#include "bms_afe_backend.h"
#include "conf.h"

#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#endif

void bms_board_features_init(void)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
    gpio_set_func(HEATER_EN_PIN, AS_GPIO);
    gpio_write(HEATER_EN_PIN, 0u);
    gpio_set_input_en(HEATER_EN_PIN, 0u);
    gpio_set_output_en(HEATER_EN_PIN, 1u);
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    sh3673510_board_force_heater_fuse_safe();
    sh3673510_board_set_heater(0u);
#endif
}

uint8_t bms_board_charge_source_present(void)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
    return gpio_read(CHG_IN_PIN) ? 0u : 1u;
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    /* D013 currently inherits the D011 active-high INT-WK-MCU code profile.
     * This remains a CODE fact until the D013 schematic/BOM is supplied. */
    return sh3673510_board_wake_active();
#else
    return 0u;
#endif
}

uint8_t bms_board_heater_supported(void)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124) || \
    (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    return 1u;
#else
    return 0u;
#endif
}

void bms_board_heater_set(uint8_t enabled)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
    gpio_write(HEATER_EN_PIN, enabled ? 1u : 0u);
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    sh3673510_board_set_heater(enabled ? 1u : 0u);
#else
    (void)enabled;
#endif
}
