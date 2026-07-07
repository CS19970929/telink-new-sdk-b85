#include "safety_manager.h"

#include "drivers.h"
#include "sci_upper.h"
#include "conf.h"

#define SAFETY_FAULT_LOG_MAGIC 0x53414645u

extern struct stCell_Info g_stCellInfoReport;

static volatile u32 g_safety_initialized;
static volatile u32 g_safety_initialized_inv = 0xFFFFFFFFu;
static volatile u32 g_safety_app_ready;
static volatile u32 g_safety_app_ready_inv = 0xFFFFFFFFu;
static volatile u32 g_safety_reset_reason;
static volatile u32 g_safety_reset_reason_inv = 0xFFFFFFFFu;
static volatile SafetyFaultType g_safety_last_fault = SAFETY_OK;
static volatile u32 g_safety_last_fault_inv = 0xFFFFFFFFu;
static SafetyFaultLog g_safety_fault_log;

static void safety_store_inverse_u32(volatile u32 *value, volatile u32 *value_inv, u32 new_value)
{
    *value = new_value;
    *value_inv = ~new_value;
}

static int safety_check_inverse_u32(volatile u32 *value, volatile u32 *value_inv)
{
    return ((*value) ^ (*value_inv)) == 0xFFFFFFFFu;
}

static void safety_set_last_fault(SafetyFaultType fault)
{
    g_safety_last_fault = fault;
    g_safety_last_fault_inv = ~((u32)fault);
}

static void safety_capture_fault_log(SafetyFaultType fault)
{
    /* 故障快照只写 RAM，避免在异常状态下擦写 Flash。 */
    g_safety_fault_log.magic = SAFETY_FAULT_LOG_MAGIC;
    g_safety_fault_log.time_tick = clock_time();
    g_safety_fault_log.fault = (u32)fault;
    g_safety_fault_log.fault_inv = ~((u32)fault);
    g_safety_fault_log.reset_reason = g_safety_reset_reason;
    g_safety_fault_log.vcell_max = g_stCellInfoReport.u16VCellMax;
    g_safety_fault_log.vcell_min = g_stCellInfoReport.u16VCellMin;
    g_safety_fault_log.current_chg = g_stCellInfoReport.u16Ichg;
    g_safety_fault_log.current_dsg = g_stCellInfoReport.u16IDischg;
    g_safety_fault_log.temp_max = g_stCellInfoReport.u16TempMax;
    g_safety_fault_log.temp_min = g_stCellInfoReport.u16TempMin;
}

void Safety_Init(void)
{
#if SAFETY_ENABLE
    safety_store_inverse_u32(&g_safety_initialized, &g_safety_initialized_inv, 1u);
    safety_store_inverse_u32(&g_safety_app_ready, &g_safety_app_ready_inv, 0u);
    if (!safety_check_inverse_u32(&g_safety_reset_reason, &g_safety_reset_reason_inv))
    {
        safety_store_inverse_u32(&g_safety_reset_reason, &g_safety_reset_reason_inv, 0u);
    }
    safety_set_last_fault(SAFETY_OK);
    g_safety_fault_log.magic = 0u;
    (void)Safety_FlowCheckInit();
#endif
}

void Safety_NotifyApplicationReady(void)
{
#if SAFETY_ENABLE
    safety_store_inverse_u32(&g_safety_app_ready, &g_safety_app_ready_inv, 1u);
#endif
}

void Safety_RecordResetReason(u32 reason)
{
#if SAFETY_ENABLE
    safety_store_inverse_u32(&g_safety_reset_reason, &g_safety_reset_reason_inv, reason);
#else
    (void)reason;
#endif
}

void Safety_ReportFault(SafetyFaultType fault)
{
#if SAFETY_ENABLE
    if (fault == SAFETY_OK)
    {
        return;
    }
    safety_set_last_fault(fault);
    safety_capture_fault_log(fault);
#else
    (void)fault;
#endif
}

int Safety_IsHealthy(void)
{
#if SAFETY_ENABLE
    if (!safety_check_inverse_u32(&g_safety_initialized, &g_safety_initialized_inv))
    {
        return 0;
    }
    if (!safety_check_inverse_u32(&g_safety_app_ready, &g_safety_app_ready_inv))
    {
        return 0;
    }
    if (!safety_check_inverse_u32(&g_safety_reset_reason, &g_safety_reset_reason_inv))
    {
        return 0;
    }
    return (((u32)g_safety_last_fault) ^ g_safety_last_fault_inv) == 0xFFFFFFFFu;
#else
    return 1;
#endif
}

int Safety_IsApplicationReady(void)
{
#if SAFETY_ENABLE
    if (!safety_check_inverse_u32(&g_safety_app_ready, &g_safety_app_ready_inv))
    {
        return 0;
    }
    return g_safety_app_ready != 0u;
#else
    return 1;
#endif
}

SafetyFaultType Safety_GetLastFault(void)
{
#if SAFETY_ENABLE
    if ((((u32)g_safety_last_fault) ^ g_safety_last_fault_inv) != 0xFFFFFFFFu)
    {
        return SAFETY_FLOW_FAULT;
    }
    return g_safety_last_fault;
#else
    return SAFETY_OK;
#endif
}

const SafetyFaultLog *Safety_GetFaultLog(void)
{
    return &g_safety_fault_log;
}

void Safety_EnterFailSafe(SafetyFaultType fault)
{
#if SAFETY_ENABLE
    Safety_ReportFault(fault);

    /* 管理变量异常也按流程故障处理，避免继续运行在未知状态。 */
    if (!Safety_IsHealthy())
    {
        safety_set_last_fault(SAFETY_FLOW_FAULT);
        safety_capture_fault_log(SAFETY_FLOW_FAULT);
    }

    Safety_EnterFailSafeHw(fault);
#else
    (void)fault;
#endif
}
