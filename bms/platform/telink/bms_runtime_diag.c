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
#include "bms_product.h"
#include "bms_afe_backend.h"
#include "bms_state.h"
#include "bms_parameters.h"

/* 从应用与后端缓存汇总运行诊断，不额外采样 AFE。 */
void bms_diag_poll_runtime(uint8_t valid, int32_t raw_current_ma,
                           int32_t current_ma, uint32_t tick_32k)
{
    bms_soc_diag_t soc;
    bms_diag_runtime_sample(valid,raw_current_ma,current_ma,tick_32k,bms_afe_current_recovery_pending());
    BMS_LOG(BMS_LOG_DEBUG, BMS_LOG_AFE, BMS_LOG_CELL_RANGE,
            ((uint32_t)g_bms_report.cell_min_mv << 16) | g_bms_report.cell_max_mv,
            ((uint32_t)g_bms_report.cell_min_index << 16) | g_bms_report.cell_max_index);
    bms_soc_get_diag(&soc);
    bms_diag_runtime_soc(&soc);
    bms_diag_runtime_faults(g_bms_report.fault_first.all,
        g_bms_report.fault_second.all,g_bms_report.fault_third.all);
#if BMS_AFE_BACKEND != BMS_AFE_BACKEND_DVC1124
    /* DVC 在主循环停机分支之前刷新，保留原诊断时点与总线静默资格。 */
    bms_storage_platform_diag_poll();
    bms_parameters_diag_poll();
    bms_afe_diag_poll();
#endif
}
