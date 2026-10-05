#include "bms_diag.h"
#include "bms_afe.h"
#include "bms_storage_platform.h"
#include "bms_soc.h"
#include "conf.h"
#include "bms_state.h"

void bms_diag_poll_runtime(uint8_t valid, int32_t current_ma,
                           uint32_t tick_32k, uint8_t factory)
{
    bms_soc_diag_t soc;
    bms_afe_aux_measurements_t sample;
    int32_t raw=0;
    if (valid && bms_afe_get_aux_measurements(&sample)) raw=sample.raw_current_ma;
    bms_diag_runtime_sample(valid,raw,current_ma,tick_32k,bms_afe_current_recovery_pending());
    bms_soc_get_diag(&soc);
    bms_diag_runtime_soc(soc.soc_estimate,soc.soc_display,soc.ocv_state,soc.ocv_center,
        soc.ocv_low,soc.ocv_high,soc.ocv_confidence,soc.rest_seconds,soc.learning_state,
        soc.capacity_learned,soc.learned_capacity_0p1ah,soc.current_deadband_ma);
    bms_diag_runtime_soc_extended(&soc);
    bms_diag_runtime_faults(g_stCellInfoReport.unMdlFault_First.all,
        g_stCellInfoReport.unMdlFault_Second.all,g_stCellInfoReport.unMdlFault_Third.all);
    bms_diag_runtime_mode(factory);
    bms_storage_platform_diag_poll();
    bms_param_diag_poll();
    bms_afe_diag_poll();
}
