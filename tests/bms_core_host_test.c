#include <assert.h>
#include <string.h>
#include "bms_crc.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"

bms_protection_params_t g_bms_protection_params;
uint8_t bms_protection_params_valid(void) { return 1u; }

int main(void)
{
    static const uint8_t reference[] = "123456789";
    bms_sw_protection_inputs_t inputs;
    assert(mb_crc16(reference, 9u) == 0x4B37u);
    assert(mb_crc16(reference, 0u) == 0xFFFFu);
    bms_error_clear(BMS_ERROR_AFE1);
    for (unsigned i = 0u; i < 300u; ++i) bms_error_raise(BMS_ERROR_AFE1);
    assert(bms_error_get(BMS_ERROR_AFE1) == UINT8_MAX);
    bms_error_clear(BMS_ERROR_AFE1);
    assert(bms_error_get(BMS_ERROR_AFE1) == 0u);
    memset(&inputs, 0, sizeof(inputs));
    bms_sw_protection_init();
    bms_sw_protection_update_groups(&inputs, 0u, 0u);
    assert(!bms_sw_protection_charge_blocked());
    assert(!bms_sw_protection_discharge_blocked());
    return 0;
}
