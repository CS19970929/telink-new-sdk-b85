#include "dvc1124.h"

/*
 * DVC1124-2 0x76 mixes COTF (bit7, read-clear) with COTT[6:0] (RW).
 *
 * Any legitimate threshold access must read the register, which consumes COTF.
 * Preserve every observed COTF in a software sticky flag so configuration and
 * diagnostics never silently lose the hardware event.
 */
static uint8_t s_core_ot_event_latched;

static uint8_t dvc1124_read_core_ot_raw(uint8_t *raw)
{
    if (raw == 0) return 0u;
    if (!DVC1124_ReadRegisters(DVC1124_REG_CORE_OT, raw, 1u)) return 0u;

    if ((*raw & DVC1124_CORE_OT_FLAG_MASK) != 0u)
        s_core_ot_event_latched = 1u;

    return 1u;
}

uint8_t DVC1124_ReadCoreOtThresholdCode(uint8_t *threshold_code)
{
    uint8_t raw;

    if (threshold_code == 0) return 0u;
    if (!dvc1124_read_core_ot_raw(&raw)) return 0u;

    *threshold_code = DVC1124_FIELD_GET(DVC1124_CORE_OT_THRESHOLD_MASK,
                                        DVC1124_CORE_OT_THRESHOLD_SHIFT,
                                        raw);
    return 1u;
}

uint8_t DVC1124_SetCoreOtThresholdCode(uint8_t threshold_code)
{
    uint8_t raw;
    uint8_t target;
    uint8_t verify;

    if (threshold_code > 0x7Fu) return 0u;

    /* First read explicitly captures and preserves any pending RC event. */
    if (!dvc1124_read_core_ot_raw(&raw)) return 0u;
    (void)raw;

    /* COTF is RC/status, not configuration. Write only COTT[6:0]. */
    target = DVC1124_FIELD_PREP(DVC1124_CORE_OT_THRESHOLD_MASK,
                                DVC1124_CORE_OT_THRESHOLD_SHIFT,
                                threshold_code);
    if (!DVC1124_WriteRegisters(DVC1124_REG_CORE_OT, &target, 1u)) return 0u;

    /* Readback may itself observe a new COTF; preserve it before verification. */
    if (!dvc1124_read_core_ot_raw(&verify)) return 0u;
    return ((verify & DVC1124_CORE_OT_THRESHOLD_MASK) == target) ? 1u : 0u;
}

uint8_t DVC1124_GetCoreOtEventLatched(void)
{
    return s_core_ot_event_latched;
}

void DVC1124_ClearCoreOtEventLatched(void)
{
    s_core_ot_event_latched = 0u;
}
