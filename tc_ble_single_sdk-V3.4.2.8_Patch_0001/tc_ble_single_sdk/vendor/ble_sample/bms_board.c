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
    /* D011 charger presence is provided by SH3673510 C+/VCHGR ADC through the
     * AFE semantic API. INT-WK-MCU is a wake circuit and is not used as the
     * charging-source truth. If the AFE detector cannot be read, fail safe: do
     * not start charge heating. */
    return 0u;
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

uint8_t bms_board_heater_allowed(void)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
    return 1u;
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    /* D011 preserves its product wake/switch qualification without letting
     * the SH backend own the heater state machine. */
    return sh3673510_board_wake_active() ? 1u : 0u;
#else
    return 0u;
#endif
}

void bms_board_heater_set(uint8_t enabled)
{
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
    gpio_write(HEATER_EN_PIN, enabled ? 1u : 0u);
#elif (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510)
    /* PB5 heater-fuse trigger remains outside this reversible heater API. */
    sh3673510_board_set_heater(enabled ? 1u : 0u);
#else
    (void)enabled;
#endif
}
