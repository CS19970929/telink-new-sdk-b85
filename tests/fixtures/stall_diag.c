#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#include "bms_stall_diag.h"
#include "bms_timing.h"
#include "bms_sleep_status.h"
#define BMS_PRODUCT_ID 14u
#define MCU_CORE_TYPE 1
#define MCU_CORE_825x 1
#define PM_TIM_RECOVER_MODE 1
#define BMS_DIAG_BUILD_ID 0x12345678u
#define _attribute_ram_code_
#define DEEP_ANA_REG0 0x3Au
#define DEEP_ANA_REG1 0x3Bu
#define DEEP_ANA_REG6 0x35u
#define DEEP_ANA_REG7 0x36u
#define DEEPSLEEP_MODE 0x80u
typedef unsigned SleepMode_TypeDef;
typedef unsigned SleepWakeupSrc_TypeDef;
typedef int (*cpu_pm_handler_t)(SleepMode_TypeDef,SleepWakeupSrc_TypeDef,unsigned);
static uint8_t digital[0x800], analog[256];
#define REG_ADDR8(a) digital[a]
#define REG_ADDR16(a) ((uint16_t)digital[a]|((uint16_t)digital[(a)+1u]<<8))
#define reg_system_tick_mode digital[0x74C]
#define reg_system_tick_ctrl digital[0x74F]
static uint32_t reg_irq_src;
static struct { unsigned recover_flag; } pm_timRecover;
static unsigned analog_reads,analog_writes,clock_reads,driver_calls;
static uint32_t tick,driver_cost;
static unsigned expected_mode=2u,expected_deep_marker,restart_in_pm;
static jmp_buf simulated_restart;
uint32_t bms_time_now_32k(void){return 0u;}
static uint32_t clock_time(void){++clock_reads;return tick;}
static uint8_t analog_read(unsigned a){++analog_reads;return analog[a];}
static void analog_write(unsigned a,uint8_t v){++analog_writes;analog[a]=v;}
static unsigned pm_get_wakeup_src(void){return 7u;}
static int model_pm(SleepMode_TypeDef mode,SleepWakeupSrc_TypeDef src,unsigned deadline){
    assert(mode==expected_mode && src==0x60u && deadline==0xFEDCBA98u);
    if(mode==DEEPSLEEP_MODE)assert(analog[DEEP_ANA_REG7]==expected_deep_marker);
    ++driver_calls;tick+=driver_cost;
    if(restart_in_pm)longjmp(simulated_restart,1);
    return -3;
}
static cpu_pm_handler_t cpu_sleep_wakeup=model_pm;
/* PRODUCTION */
/* SLEEP STATUS */
static void reset_runtime(uint32_t now){
    memset(s_live,0,sizeof(s_live));memset(s_first_gap,0,sizeof(s_first_gap));
    memset(s_first_afe,0,sizeof(s_first_afe));
    s_gap_reason=s_resume_stage=s_running=s_raw_ready=0u;
    s_sleep_reason=s_deep_committed=0u;
    s_irq_enters=s_irq_exits=s_irq_source=0u;s_gap_ms=s_afe_flags=0u;
    tick=now;driver_cost=0u;driver_calls=0u;
    expected_mode=2u;expected_deep_marker=restart_in_pm=0u;
    cpu_sleep_wakeup=model_pm;bms_stall_diag_install_pm();bms_stall_diag_start();
}
int main(void){
    analog[DEEP_ANA_REG1]=254u;digital[0x72]=0xA5u;analog[DEEP_ANA_REG7]=0xD5u;
    bms_stall_diag_boot();assert(analog[DEEP_ANA_REG1]==255u);
    assert(s_boot_words[0]==0xFEFFu && s_boot_words[1]==0xA5u && digital[0x72]==0xA5u);
    assert(s_boot_words[5]==0xD5u && analog[DEEP_ANA_REG7]==0u);
    bms_stall_diag_boot();assert(s_boot_words[0]==0xFF00u);
    unsigned reads=analog_reads,writes=analog_writes;
    reset_runtime(0u);bms_stall_diag_stage(BMS_STALL_SDK_ENTER,0u);
    driver_cost=200u*16000u;
    assert(cpu_sleep_wakeup(2u,0x60u,0xFEDCBA98u)==-3);
    assert(driver_calls==1u && s_live[W_PM_RESULT]==(uint32_t)-3);
    assert(s_live[W_PM_ENTERS]==1u && s_live[W_PM_RETURNS]==1u && !s_gap_reason);
    assert(analog_reads==reads && analog_writes==writes);
    reset_runtime(UINT32_MAX-100u);
    driver_cost=500u*16000u;
    assert(cpu_sleep_wakeup(2u,0x60u,0xFEDCBA98u)==-3);
    assert(s_gap_reason==BMS_STALL_GAP_PM && s_gap_ms==500u);
    assert(s_first_gap[W_PHASE]==BMS_STALL_PM_ENTER);
    uint32_t frozen[32];memcpy(frozen,s_first_gap,sizeof(frozen));
    tick+=800u*16000u;bms_stall_diag_stage(BMS_STALL_LOOP,1u);
    assert(!memcmp(frozen,s_first_gap,sizeof(frozen)));
    reset_runtime(UINT32_MAX-100u);bms_stall_diag_stage(BMS_STALL_SDK_ENTER,0u);
    tick+=501u*16000u;bms_stall_diag_stage(BMS_STALL_SDK_RETURN,0u);
    assert(s_gap_reason==BMS_STALL_GAP_SDK && s_first_gap[W_PHASE]==BMS_STALL_SDK_ENTER);
    reset_runtime(0u);bms_stall_diag_stage(BMS_STALL_SAMPLE_BEGIN,0u);
    tick=400u*16000u;bms_stall_diag_stage(BMS_STALL_SAMPLE_BEGIN,0u);assert(!s_gap_reason);
    tick+=401u*16000u;bms_stall_diag_stage(BMS_STALL_SAMPLE_BEGIN,0u);
    assert(s_gap_reason==BMS_STALL_GAP_SAMPLE);
    reset_runtime(0u);bms_stall_diag_time(UINT32_MAX-100u);
    bms_stall_diag_stage(BMS_STALL_SDK_ENTER,0u);
    bms_stall_diag_bus(1u,0x59u);bms_stall_diag_time_begin();
    assert(s_live[W_DETAIL]==0x80000259u);
    bms_stall_diag_time((UINT32_MAX-100u)+500u*32u);
    assert(s_gap_reason==BMS_STALL_GAP_32K && s_gap_ms==500u);
    assert(s_first_gap[W_DETAIL]==0x259u);
    bms_stall_diag_afe_flags(3u,0x11u,0x80u);
    bms_stall_diag_guard(5u,10u,35000u);
    bms_stall_diag_afe(0x100u,-2,7u,9u);
    bms_stall_diag_afe_flags(0u,0u,0u);bms_stall_diag_afe(2u,-3,1u,2u);
    assert(s_first_afe[0]==0x0100FFFEu && s_first_afe[3]==0x00031180u);
    assert(s_first_afe[4]==7u && s_first_afe[5]==9u && s_first_afe[6]==5u);
    bms_stall_diag_irq(1u);bms_stall_diag_irq(0u);
    assert(s_irq_enters==1u && s_irq_exits==1u);
    uint8_t bytes[250];unsigned clocks=clock_reads;
    assert(bms_stall_diag_read(0x3A00u,80u,bytes));
    assert(bytes[0]==0x53u && bytes[1]==0x54u && bytes[5]==1u);
    assert(bytes[12]==0x12u && bytes[13]==0x34u && bytes[14]==0x56u && bytes[15]==0x78u);
    assert(bms_stall_diag_read(0x3A50u,80u,bytes));
    assert(!bms_stall_diag_read(0x3A9Fu,2u,bytes));
    assert(!bms_stall_diag_read(0x3A00u,126u,bytes));
    assert(!bms_stall_diag_read(0x3A00u,1u,0));
    assert(clock_reads==clocks && analog_reads==reads && analog_writes==writes);
    /* 真实休眠发布仅复制原因；原驱动调用仍一次透传。 */
    reset_runtime(0u);
    bms_sleep_status_publish(BMS_SLEEP_REASON_AFE,0u,1800000u,1800000u,0u,0u);
    bms_stall_diag_deep_commit();
    assert(s_live[W_PHASE]==BMS_STALL_DEEP_COMMIT && s_live[W_DETAIL]==7u);
    expected_mode=DEEPSLEEP_MODE;expected_deep_marker=0xD7u;
    assert(cpu_sleep_wakeup(DEEPSLEEP_MODE,0x60u,0xFEDCBA98u)==-3);
    assert(driver_calls==1u && analog[DEEP_ANA_REG7]==0u); /* 调用返回不留深睡唤醒证据。 */
    /* longjmp只模拟驱动未返回的控制流，随后模拟新启动；不模拟硅片复位语义。 */
    reset_runtime(0u);bms_sleep_status_publish(BMS_SLEEP_REASON_COMMAND,0u,500u,500u,0u,0u);
    bms_stall_diag_deep_commit();expected_mode=DEEPSLEEP_MODE;expected_deep_marker=0xD1u;restart_in_pm=1u;
    if(setjmp(simulated_restart)==0) {
        (void)cpu_sleep_wakeup(DEEPSLEEP_MODE,0x60u,0xFEDCBA98u);
        assert(0);
    }
    assert(analog[DEEP_ANA_REG7]==0xD1u);
    bms_stall_diag_boot();assert(s_boot_words[5]==0xD1u && analog[DEEP_ANA_REG7]==0u);
    assert(bms_stall_diag_read(0x3A0Du,1u,bytes) && bytes[0]==0u && bytes[1]==0xD1u);
    /* 按B85头文件的WDT/chip reset清REG7模型，不能误判为DeepSleep。 */
    analog[DEEP_ANA_REG7]=0u;digital[0x72]=1u;bms_stall_diag_boot();
    assert(!s_boot_words[5] && s_boot_words[1]==1u && digital[0x72]==1u);
    reset_runtime(0u);expected_mode=DEEPSLEEP_MODE;expected_deep_marker=0xDEu;
    assert(cpu_sleep_wakeup(DEEPSLEEP_MODE,0x60u,0xFEDCBA98u)==-3);
    s_running=0u; /* 初始化阶段的未知DeepSleep不冒充明确的boot hold。 */
    assert(cpu_sleep_wakeup(DEEPSLEEP_MODE,0x60u,0xFEDCBA98u)==-3);
    bms_stall_diag_stage(BMS_STALL_BOOT_HOLD,0u);expected_deep_marker=0xDFu;
    assert(cpu_sleep_wakeup(DEEPSLEEP_MODE,0x60u,0xFEDCBA98u)==-3);
    return 0;
}
