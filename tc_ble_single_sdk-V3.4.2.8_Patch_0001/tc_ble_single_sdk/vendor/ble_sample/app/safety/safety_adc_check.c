#include "safety_manager.h"

#include "sci_upper.h"

extern struct stCell_Info g_stCellInfoReport;

#define SAFETY_TEMP_CHANNELS 10u

int Safety_AdcRuntimeCheck(void)
{
#if SAFETY_ENABLE
    u8 i;

#if SAFETY_TEST_ENABLE && SAFETY_INJECT_ADC_FAULT
    return 0;
#endif

    /* AFE 数据未形成有效帧前不做误判。 */
    if ((g_stCellInfoReport.u16VCellMax == 0u) || (g_stCellInfoReport.u16VCellMin == 0u))
    {
        return 1;
    }

    if ((g_stCellInfoReport.u16VCellMax > SAFETY_CELL_VOLT_MAX_MV) ||
        (g_stCellInfoReport.u16VCellMin < SAFETY_CELL_VOLT_MIN_MV))
    {
        return 0;
    }

    if ((g_stCellInfoReport.u16Ichg > SAFETY_CURRENT_MAX_A10) ||
        (g_stCellInfoReport.u16IDischg > SAFETY_CURRENT_MAX_A10))
    {
        return 0;
    }

    for (i = 0u; i < SAFETY_TEMP_CHANNELS; ++i)
    {
        u16 temp = g_stCellInfoReport.u16Temperature[i];
        if ((temp != 0u) && (temp > SAFETY_TEMP_RAW_MAX))
        {
            return 0;
        }
    }

    return 1;
#else
    return 1;
#endif
}
