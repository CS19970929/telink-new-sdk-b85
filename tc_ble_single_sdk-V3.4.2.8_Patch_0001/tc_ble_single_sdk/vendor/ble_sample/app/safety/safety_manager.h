#ifndef SAFETY_MANAGER_H_
#define SAFETY_MANAGER_H_

#include "tl_common.h"
#include "safety_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SAFETY_OK = 0,

    SAFETY_CPU_FAULT,
    SAFETY_FLASH_FAULT,
    SAFETY_RAM_FAULT,
    SAFETY_CLOCK_FAULT,
    SAFETY_WATCHDOG_FAULT,
    SAFETY_FLOW_FAULT,

    SAFETY_ADC_FAULT,
    SAFETY_AFE_COMM_FAULT,
    SAFETY_MOS_FAULT,
} SafetyFaultType;

typedef enum
{
    SAFETY_FLOW_ID_STARTUP_BEGIN = 0x11u,
    SAFETY_FLOW_ID_STARTUP_END = 0x12u,
    SAFETY_FLOW_ID_MAIN_LOOP_ENTER = 0x21u,
    SAFETY_FLOW_ID_MAIN_LOOP_EXIT = 0x22u,
    SAFETY_FLOW_ID_RUNTIME_BEGIN = 0x31u,
    SAFETY_FLOW_ID_RUNTIME_END = 0x32u,
} SafetyFlowId;

typedef struct
{
    u32 magic;
    u32 time_tick;
    u32 fault;
    u32 fault_inv;
    u32 reset_reason;
    u16 vcell_max;
    u16 vcell_min;
    u16 current_chg;
    u16 current_dsg;
    u16 temp_max;
    u16 temp_min;
} SafetyFaultLog;

void Safety_Init(void);
void Safety_StartUpTest(void);
void Safety_RuntimeTask(void);
void Safety_NotifyApplicationReady(void);
void Safety_RecordResetReason(u32 reason);
void Safety_ReportFault(SafetyFaultType fault);
void Safety_EnterFailSafe(SafetyFaultType fault);
void Safety_EnterFailSafeHw(SafetyFaultType fault);
void Safety_FlowCheckPoint(u32 id);

int Safety_IsHealthy(void);
int Safety_IsApplicationReady(void);
SafetyFaultType Safety_GetLastFault(void);
const SafetyFaultLog *Safety_GetFaultLog(void);

int Safety_CpuStartupTest(void);
int Safety_CpuRuntimeTest(void);
int Safety_FlashStartupTest(void);
int Safety_FlashRuntimeTask(void);
int Safety_RamStartupTest(void);
int Safety_RamRuntimeTask(void);
int Safety_ClockStartupTest(void);
int Safety_ClockRuntimeTest(void);
int Safety_WatchdogStartupTest(void);
int Safety_WatchdogRuntimeTest(void);
int Safety_FlowCheckInit(void);
int Safety_FlowCheckValidate(void);
int Safety_AdcRuntimeCheck(void);
int Safety_CommRuntimeCheck(void);
int Safety_MosRuntimeCheck(void);

#ifdef __cplusplus
}
#endif

#endif
