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
    /* D014 has no verified heater/fuse output path. Never touch the legacy
     * PB4/PB5 D011 pins when heater support is disabled. */
    if (SH3673510_PRODUCT_HEATER_SUPPORTED)
    {
        sh3673510_board_force_heater_fuse_safe();
        sh3673510_board_set_heater(0u);
    }
#endif
}

uint8_t bms_board_charge_source_present(void)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
    return gpio_read(CHG_IN_PIN) ? 0u : 1u;
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    /* D014 has no separate schematic-backed charger-present GPIO. Charger
     * detection for generic policy must come from a validated AFE/current
     * semantic source. Fail safe here; heater is disabled on D014 anyway. */
    return 0u;
#else
    return 0u;
#endif
}

uint8_t bms_board_heater_supported(void)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
    return 1u;
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    return SH3673510_PRODUCT_HEATER_SUPPORTED ? 1u : 0u;
#else
    return 0u;
#endif
}

uint8_t bms_board_balance_supported(void)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
    return 1u;
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    return SH3673510_PRODUCT_BALANCE_SUPPORTED ? 1u : 0u;
#else
    return 0u;
#endif
}

uint8_t bms_board_heater_allowed(void)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
    return 1u;
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    /* Physical heater capability is the only board-level allow gate. */
    return SH3673510_PRODUCT_HEATER_SUPPORTED ? 1u : 0u;
#else
    return 0u;
#endif
}

void bms_board_heater_set(uint8_t enabled)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
    gpio_write(HEATER_EN_PIN, enabled ? 1u : 0u);
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    if (SH3673510_PRODUCT_HEATER_SUPPORTED)
        sh3673510_board_set_heater(enabled ? 1u : 0u);
    else
        (void)enabled;
#else
    (void)enabled;
#endif
}
