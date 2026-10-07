/*
 * 文件功能：Telink 运行状态采集入口；将 AFE/SOC/存储缓存提交给可移植诊断核心，
 * 不增加 AFE 总线读取。
 * bms/platform/telink/bms_runtime_diag.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_debug_log.h"
#include "bms_diag.h"
#include "bms_afe.h"
#include "bms_storage_platform.h"
#include "bms_soc.h"
#include "bms_product_conf.h"
#include "bms_afe_backend.h"
#include "bms_state.h"

/* 从应用与后端缓存汇总运行诊断，不额外采样 AFE。 */
void bms_diag_poll_runtime(uint8_t valid, int32_t current_ma,
                           uint32_t tick_32k, uint8_t factory)
{
    bms_soc_diag_t soc;
    bms_afe_aux_measurements_t sample;
    int32_t raw=0;
    if (valid && bms_afe_get_aux_measurements(&sample)) raw=sample.raw_current_ma;
    bms_diag_runtime_sample(valid,raw,current_ma,tick_32k,bms_afe_current_recovery_pending());
    BMS_LOG(BMS_LOG_DEBUG, BMS_LOG_AFE, BMS_LOG_CELL_RANGE,
            ((uint32_t)g_stCellInfoReport.u16VCellMin << 16) | g_stCellInfoReport.u16VCellMax,
            ((uint32_t)g_stCellInfoReport.u16VCellMinPosition << 16) | g_stCellInfoReport.u16VCellMaxPosition);
    bms_soc_get_diag(&soc);
    bms_diag_runtime_soc(soc.soc_estimate,soc.soc_display,soc.ocv_state,soc.ocv_center,
        soc.ocv_low,soc.ocv_high,soc.ocv_confidence,soc.rest_seconds,soc.learning_state,
        soc.capacity_learned,soc.learned_capacity_0p1ah,soc.current_deadband_ma);
    bms_diag_runtime_soc_extended(&soc);
    bms_diag_runtime_faults(g_stCellInfoReport.unMdlFault_First.all,
        g_stCellInfoReport.unMdlFault_Second.all,g_stCellInfoReport.unMdlFault_Third.all);
    bms_diag_runtime_mode(factory);
    bms_storage_platform_diag_poll();
    bms_parameters_diag_poll();
    bms_afe_diag_poll();
}
