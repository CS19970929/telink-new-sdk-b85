#include "dvc1124_commands.h"
#include "dvc1124.h"

uint8_t DVC1124_ClearAlarmFlags(uint8_t flag_mask)
{
    uint8_t write_value;

    if (flag_mask == 0u) return 1u;

    /* V1.2: ALARM is W0C. 0 clears selected flags; 1 leaves others unchanged. */
    write_value = (uint8_t)~flag_mask;
    return DVC1124_WriteRegisters(DVC1124_REG_ALARM, &write_value, 1u);
}

uint8_t DVC1124_StartCadcCalibration(void)
{
    uint8_t value;

    /*
     * CAMZ is a self-clearing command. Read only the safe CADC control byte,
     * preserve its persistent fields, then set CAMZ. Do not require readback=1.
     */
    if (!DVC1124_ReadRegisters(DVC1124_REG_CADC_CTRL, &value, 1u)) return 0u;
    value |= DVC1124_CADC_CAMZ_MASK;
    return DVC1124_WriteRegisters(DVC1124_REG_CADC_CTRL, &value, 1u);
}
