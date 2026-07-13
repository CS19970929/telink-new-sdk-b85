#include "bms_selftest_internal.h"

#include "bms_failsafe.h"
#include "bms_fault_inject.h"
#include "bms_selftest_port.h"

#include "drivers.h"

#define BMS_HEARTBEAT_REQUIRED ((u32)BMS_HEARTBEAT_AFE | (u32)BMS_HEARTBEAT_PROTECTION | \
                                (u32)BMS_HEARTBEAT_MOS | (u32)BMS_HEARTBEAT_SELFTEST)
#define BMS_DIAG_FLAG_DEEP_RETENTION  (1u << 0)
#define BMS_DIAG_FLAG_TEST_BUILD      (1u << 1)
#define BMS_DIAG_FLAG_INJECTION       (1u << 2)
#define BMS_DIAG_FLAG_OTA_PAUSED      (1u << 3)
#define BMS_DIAG_FLAG_LOW_POWER       (1u << 4)

static bms_selftest_diag_t g_bms_diag;
static volatile u32 g_bms_fault_mask;
static volatile u32 g_bms_fault_mask_inv = 0xffffffffu;
static volatile u32 g_bms_heartbeat_seen;
static volatile u32 g_bms_heartbeat_seen_inv = 0xffffffffu;
static u32 g_bms_runtime_tick;
static u32 g_bms_heartbeat_tick;
static u8 g_bms_early_boot_done;
static u8 g_bms_early_boot_ok;

static void bms_store_pair(volatile u32 *value, volatile u32 *inverse, u32 next)
{
    *value = next;
    *inverse = ~next;
}

static u8 bms_pair_ok(volatile u32 *value, volatile u32 *inverse)
{
    return ((*value ^ *inverse) == 0xffffffffu) ? 1u : 0u;
}

static u8 bms_fault_is_fatal(bms_fault_reason_t reason)
{
    switch (reason)
    {
    case BMS_FAULT_CPU_REGISTER:
    case BMS_FAULT_CPU_FLAG:
    case BMS_FAULT_CONTROL_FLOW:
    case BMS_FAULT_FLASH_MANIFEST:
    case BMS_FAULT_FLASH_CRC:
    case BMS_FAULT_RAM:
    case BMS_FAULT_STACK:
    case BMS_FAULT_CLOCK:
    case BMS_FAULT_INTERRUPT_LOST:
    case BMS_FAULT_INTERRUPT_FREQUENT:
    case BMS_FAULT_WATCHDOG_HEARTBEAT:
    case BMS_FAULT_RETENTION:
    case BMS_FAULT_INTERNAL_DATA:
        return 1u;
    default:
        return 0u;
    }
}

void BMS_SelfTest_EarlyBoot(void)
{
#if BMS_SELFTEST_ENABLE
    BMS_Port_ForceDangerousOutputsOffEarly();
    g_bms_early_boot_ok = (u8)(BMS_SelfTest_CpuStartup() && BMS_SelfTest_ControlFlowStartup());
    g_bms_early_boot_done = 1u;
#endif
}

void BMS_SelfTest_BoardInit(void)
{
#if BMS_SELFTEST_ENABLE
    BMS_Port_ForceDangerousOutputsOffEarly();
    BMS_FailSafe_Init();
    bms_store_pair(&g_bms_fault_mask, &g_bms_fault_mask_inv, 0u);
    bms_store_pair(&g_bms_heartbeat_seen, &g_bms_heartbeat_seen_inv, 0u);
    g_bms_diag.state = BMS_SELFTEST_STATE_RESET;
    g_bms_diag.flags = (u16)((BMS_DIAG_TEST_BUILD ? BMS_DIAG_FLAG_TEST_BUILD : 0u) |
                             (BMS_DIAG_FAULT_INJECT_ENABLE ? BMS_DIAG_FLAG_INJECTION : 0u));
#endif
}

void BMS_SelfTest_RecordFailure(bms_selftest_item_t item, bms_fault_reason_t reason)
{
    u32 mask;

    if (!bms_pair_ok(&g_bms_fault_mask, &g_bms_fault_mask_inv))
    {
        reason = BMS_FAULT_INTERNAL_DATA;
    }
    mask = g_bms_fault_mask;
    if ((u8)reason < 32u)
    {
        mask |= (1u << (u8)reason);
    }
    bms_store_pair(&g_bms_fault_mask, &g_bms_fault_mask_inv, mask);
    g_bms_diag.current_item = (u16)item;
    g_bms_diag.active_fault = (u16)reason;
    g_bms_diag.fault_mask = mask;
    g_bms_diag.state = BMS_SELFTEST_STATE_FAIL_SAFE;
    BMS_FailSafe_Enter(reason, bms_fault_is_fatal(reason));
    g_bms_diag.last_fatal_fault = BMS_FailSafe_GetLastFatalReason();
}

static u8 bms_startup_step(bms_selftest_item_t item, u8 ok, bms_fault_reason_t reason)
{
    g_bms_diag.current_item = (u16)item;
    if (!ok)
    {
        BMS_SelfTest_RecordFailure(item, reason);
        return 0u;
    }
    return 1u;
}

u8 BMS_SelfTest_Startup(u8 deep_retention_wakeup)
{
    u8 ok = 1u;

#if BMS_SELFTEST_ENABLE
    g_bms_diag.state = BMS_SELFTEST_STATE_STARTUP;
    g_bms_diag.startup_result = 0u;
    g_bms_diag.active_fault = BMS_FAULT_NONE;
    if (deep_retention_wakeup) g_bms_diag.flags |= BMS_DIAG_FLAG_DEEP_RETENTION;
    BMS_Port_SetApplicationReady(1u);

    if (deep_retention_wakeup && BMS_FaultInject_ShouldFail((u8)BMS_FAULT_RETENTION))
    {
        ok = bms_startup_step(BMS_SELFTEST_ITEM_RAM, 0u, BMS_FAULT_RETENTION);
    }
    if (!g_bms_early_boot_done || !g_bms_early_boot_ok)
    {
        ok = bms_startup_step(BMS_SELFTEST_ITEM_CPU, 0u, BMS_FAULT_CPU_REGISTER);
    }
    if (ok) ok = bms_startup_step(BMS_SELFTEST_ITEM_CPU, BMS_SelfTest_CpuStartup(), BMS_FAULT_CPU_REGISTER);
    if (ok) ok = bms_startup_step(BMS_SELFTEST_ITEM_CONTROL_FLOW, BMS_SelfTest_ControlFlowStartup(), BMS_FAULT_CONTROL_FLOW);
    if (ok) ok = bms_startup_step(BMS_SELFTEST_ITEM_RAM, BMS_SelfTest_RamStartup(), BMS_FAULT_RAM);
    if (ok) ok = bms_startup_step(BMS_SELFTEST_ITEM_STACK, BMS_SelfTest_StackStartup(), BMS_FAULT_STACK);
    if (ok) ok = bms_startup_step(BMS_SELFTEST_ITEM_CLOCK, BMS_SelfTest_ClockStartup(), BMS_FAULT_CLOCK);
    if (ok) ok = bms_startup_step(BMS_SELFTEST_ITEM_FLASH, BMS_SelfTest_FlashStartup(), BMS_FAULT_FLASH_CRC);
    if (ok) ok = bms_startup_step(BMS_SELFTEST_ITEM_AFE, BMS_Port_VerifyAfeConfiguration(), BMS_FAULT_AFE_CONFIG);

    if (ok && !BMS_FailSafe_IsActive())
    {
        g_bms_diag.startup_result = 1u;
        g_bms_diag.current_item = BMS_SELFTEST_ITEM_NONE;
        return 1u;
    }
    BMS_Port_ForceDangerousOutputsOff();
    return 0u;
#else
    (void)deep_retention_wakeup;
    return 1u;
#endif
}

void BMS_SelfTest_RuntimeInit(void)
{
#if BMS_SELFTEST_ENABLE
    g_bms_runtime_tick = clock_time();
    g_bms_heartbeat_tick = g_bms_runtime_tick;
    BMS_SelfTest_FlashRuntimeReset();
    if (!BMS_FailSafe_IsActive())
    {
        g_bms_diag.state = BMS_SELFTEST_STATE_RUNTIME;
    }
#endif
}

static void bms_runtime_periodic_tests(void)
{
    if (!BMS_SelfTest_CpuRuntime()) BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_CPU, BMS_FAULT_CPU_REGISTER);
    if (!BMS_SelfTest_ControlFlowRuntime()) BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_CONTROL_FLOW, BMS_FAULT_CONTROL_FLOW);
    if (!BMS_SelfTest_RamRuntimeStep()) BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_RAM, BMS_FAULT_RAM);
    if (!BMS_SelfTest_StackRuntime()) BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_STACK, BMS_FAULT_STACK);
    if (!BMS_SelfTest_ClockRuntime()) BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_CLOCK, BMS_FAULT_CLOCK);
    (void)BMS_SelfTest_InterruptRuntime();
    BMS_SelfTest_ApplicationProcess();
    ++g_bms_diag.runtime_cycles;
    g_bms_diag.runtime_result = BMS_FailSafe_IsActive() ? 0u : 1u;
    BMS_SelfTest_ReportHeartbeat(BMS_HEARTBEAT_SELFTEST);
}

void BMS_SelfTest_Process(void)
{
#if BMS_SELFTEST_ENABLE
    BMS_FailSafe_Process();
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_INTERNAL_DATA))
    {
        BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_CONTROL_FLOW, BMS_FAULT_INTERNAL_DATA);
    }
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_WATCHDOG_HEARTBEAT))
    {
        BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_CONTROL_FLOW, BMS_FAULT_WATCHDOG_HEARTBEAT);
    }
    if (BMS_Port_IsOtaWorking())
    {
        g_bms_diag.flags |= BMS_DIAG_FLAG_OTA_PAUSED;
        BMS_SelfTest_FlashRuntimeReset();
    }
    else
    {
        g_bms_diag.flags &= (u16)(~BMS_DIAG_FLAG_OTA_PAUSED);
        if (!BMS_SelfTest_FlashRuntimeStep())
        {
            BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_FLASH, BMS_FAULT_FLASH_CRC);
        }
    }
    if (BMS_Port_IsLowPower())
    {
        g_bms_diag.flags |= BMS_DIAG_FLAG_LOW_POWER;
        return;
    }
    g_bms_diag.flags &= (u16)(~BMS_DIAG_FLAG_LOW_POWER);

    if (clock_time_exceed(g_bms_runtime_tick, BMS_SELFTEST_RUNTIME_PERIOD_US))
    {
        g_bms_runtime_tick = clock_time();
        bms_runtime_periodic_tests();
    }
    if (clock_time_exceed(g_bms_heartbeat_tick, BMS_SELFTEST_HEARTBEAT_WINDOW_US))
    {
        u32 seen = bms_pair_ok(&g_bms_heartbeat_seen, &g_bms_heartbeat_seen_inv) ? g_bms_heartbeat_seen : 0u;
        g_bms_diag.heartbeat_missing = BMS_HEARTBEAT_REQUIRED & ~seen;
        if (g_bms_diag.heartbeat_missing != 0u)
        {
            BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_CONTROL_FLOW, BMS_FAULT_WATCHDOG_HEARTBEAT);
        }
        g_bms_heartbeat_tick = clock_time();
    }
#endif
}

void BMS_SelfTest_ReportHeartbeat(u32 module)
{
    u32 seen;

    if (!bms_pair_ok(&g_bms_heartbeat_seen, &g_bms_heartbeat_seen_inv))
    {
        BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_CONTROL_FLOW, BMS_FAULT_INTERNAL_DATA);
        return;
    }
    seen = g_bms_heartbeat_seen | (module & BMS_HEARTBEAT_REQUIRED);
    bms_store_pair(&g_bms_heartbeat_seen, &g_bms_heartbeat_seen_inv, seen);
    g_bms_diag.heartbeat_seen = seen;
}

u8 BMS_SelfTest_IsHealthy(void)
{
    return (u8)(bms_pair_ok(&g_bms_fault_mask, &g_bms_fault_mask_inv) &&
                bms_pair_ok(&g_bms_heartbeat_seen, &g_bms_heartbeat_seen_inv) &&
                !BMS_FailSafe_IsActive());
}

u8 BMS_SelfTest_WatchdogCanFeed(void)
{
    u32 seen;

    if (!BMS_FailSafe_WatchdogCanFeed())
    {
        return 0u;
    }
    if (BMS_Port_IsLowPower())
    {
        return 1u;
    }
    if (!bms_pair_ok(&g_bms_heartbeat_seen, &g_bms_heartbeat_seen_inv))
    {
        return 0u;
    }
    seen = g_bms_heartbeat_seen;
    if ((seen & BMS_HEARTBEAT_REQUIRED) != BMS_HEARTBEAT_REQUIRED)
    {
        return 0u;
    }
    bms_store_pair(&g_bms_heartbeat_seen, &g_bms_heartbeat_seen_inv, 0u);
    g_bms_diag.heartbeat_seen = 0u;
    g_bms_diag.heartbeat_missing = 0u;
    g_bms_heartbeat_tick = clock_time();
    return 1u;
}

u32 BMS_SelfTest_GetFaultMask(void)
{
    return bms_pair_ok(&g_bms_fault_mask, &g_bms_fault_mask_inv) ? g_bms_fault_mask : 0xffffffffu;
}

const bms_selftest_diag_t *BMS_SelfTest_GetDiag(void)
{
    g_bms_diag.active_fault = BMS_FailSafe_GetReason();
    g_bms_diag.last_fatal_fault = BMS_FailSafe_GetLastFatalReason();
    g_bms_diag.fault_mask = BMS_SelfTest_GetFaultMask();
    g_bms_diag.irq_count = BMS_SelfTest_InterruptCount();
    return &g_bms_diag;
}

void BMS_SelfTest_SetFlashDiag(u32 expected, u32 actual, u32 progress, u32 total)
{
    g_bms_diag.expected_flash_crc = expected;
    g_bms_diag.actual_flash_crc = actual;
    g_bms_diag.flash_progress = progress;
    g_bms_diag.flash_total = total;
}

void BMS_SelfTest_SetIrqDiag(u32 count, u32 delta)
{
    g_bms_diag.irq_count = count;
    g_bms_diag.irq_delta = delta;
}

void BMS_SelfTest_SetStackHighWater(u32 bytes)
{
    g_bms_diag.stack_high_water = bytes;
}
