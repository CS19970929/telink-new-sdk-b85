#include "bms_failsafe.h"

#include "bms_selftest_cfg.h"
#include "bms_selftest_port.h"

#define BMS_FAILSAFE_MARKER_MAGIC             0xA0u
#define BMS_FAILSAFE_MARKER_MASK              0xF0u
#define BMS_FAILSAFE_REASON_MASK              0x0Fu
#define BMS_FAILSAFE_FLAG_ACTIVE              (1u << 0)
#define BMS_FAILSAFE_FLAG_FATAL_PENDING_RESET (1u << 1)
#define BMS_FAILSAFE_FLAG_PERMANENT_DIAG      (1u << 2)

static volatile u16 g_bms_failsafe_reason;
static volatile u16 g_bms_failsafe_reason_inv = 0xffffu;
static volatile u16 g_bms_failsafe_last_fatal;
static volatile u16 g_bms_failsafe_last_fatal_inv = 0xffffu;
static volatile u16 g_bms_failsafe_flags;
static volatile u16 g_bms_failsafe_flags_inv = 0xffffu;

static void bms_failsafe_store(volatile u16 *value, volatile u16 *inverse, u16 next)
{
    *value = next;
    *inverse = (u16)(~next);
}

static u8 bms_failsafe_pair_ok(volatile u16 *value, volatile u16 *inverse)
{
    return ((u16)(*value ^ *inverse) == 0xffffu) ? 1u : 0u;
}

void BMS_FailSafe_Init(void)
{
    u8 marker = BMS_Port_ReadResetMarker();

    bms_failsafe_store(&g_bms_failsafe_reason, &g_bms_failsafe_reason_inv, BMS_FAULT_NONE);
    bms_failsafe_store(&g_bms_failsafe_last_fatal, &g_bms_failsafe_last_fatal_inv, BMS_FAULT_NONE);
    bms_failsafe_store(&g_bms_failsafe_flags, &g_bms_failsafe_flags_inv, 0u);

    if ((marker & BMS_FAILSAFE_MARKER_MASK) == BMS_FAILSAFE_MARKER_MAGIC)
    {
        u16 previous = (u16)(marker & BMS_FAILSAFE_REASON_MASK);
        if (previous == 0u)
        {
            previous = BMS_FAULT_INTERNAL_DATA;
        }
        bms_failsafe_store(&g_bms_failsafe_reason, &g_bms_failsafe_reason_inv, previous);
        bms_failsafe_store(&g_bms_failsafe_last_fatal, &g_bms_failsafe_last_fatal_inv, previous);
        bms_failsafe_store(&g_bms_failsafe_flags, &g_bms_failsafe_flags_inv,
                           (u16)(BMS_FAILSAFE_FLAG_ACTIVE | BMS_FAILSAFE_FLAG_PERMANENT_DIAG));
    }
}

void BMS_FailSafe_Enter(bms_fault_reason_t reason, u8 fatal_mcu_fault)
{
    u16 flags;

    if (reason == BMS_FAULT_NONE)
    {
        reason = BMS_FAULT_INTERNAL_DATA;
    }
    if (!bms_failsafe_pair_ok(&g_bms_failsafe_flags, &g_bms_failsafe_flags_inv))
    {
        reason = BMS_FAULT_INTERNAL_DATA;
        fatal_mcu_fault = 1u;
        flags = BMS_FAILSAFE_FLAG_ACTIVE;
    }
    else
    {
        flags = (u16)(g_bms_failsafe_flags | BMS_FAILSAFE_FLAG_ACTIVE);
    }

    bms_failsafe_store(&g_bms_failsafe_reason, &g_bms_failsafe_reason_inv, (u16)reason);
    if (fatal_mcu_fault)
    {
        flags |= BMS_FAILSAFE_FLAG_FATAL_PENDING_RESET;
        bms_failsafe_store(&g_bms_failsafe_last_fatal, &g_bms_failsafe_last_fatal_inv, (u16)reason);
        BMS_Port_WriteResetMarker((u8)(BMS_FAILSAFE_MARKER_MAGIC | ((u8)reason & BMS_FAILSAFE_REASON_MASK)));
    }
    bms_failsafe_store(&g_bms_failsafe_flags, &g_bms_failsafe_flags_inv, flags);
    BMS_Port_ForceDangerousOutputsOff();
}

void BMS_FailSafe_Process(void)
{
    if (!bms_failsafe_pair_ok(&g_bms_failsafe_reason, &g_bms_failsafe_reason_inv) ||
        !bms_failsafe_pair_ok(&g_bms_failsafe_last_fatal, &g_bms_failsafe_last_fatal_inv) ||
        !bms_failsafe_pair_ok(&g_bms_failsafe_flags, &g_bms_failsafe_flags_inv))
    {
        BMS_FailSafe_Enter(BMS_FAULT_INTERNAL_DATA, 1u);
        return;
    }
    if (g_bms_failsafe_flags & BMS_FAILSAFE_FLAG_ACTIVE)
    {
        BMS_Port_ForceDangerousOutputsOff();
    }
}

u8 BMS_FailSafe_IsActive(void)
{
    if (!bms_failsafe_pair_ok(&g_bms_failsafe_flags, &g_bms_failsafe_flags_inv))
    {
        return 1u;
    }
    return (g_bms_failsafe_flags & BMS_FAILSAFE_FLAG_ACTIVE) ? 1u : 0u;
}

u8 BMS_FailSafe_AllowOutputs(void)
{
    return BMS_FailSafe_IsActive() ? 0u : 1u;
}

u8 BMS_FailSafe_WatchdogCanFeed(void)
{
    if (!bms_failsafe_pair_ok(&g_bms_failsafe_flags, &g_bms_failsafe_flags_inv))
    {
        return 0u;
    }
    if (g_bms_failsafe_flags & BMS_FAILSAFE_FLAG_FATAL_PENDING_RESET)
    {
        return 0u;
    }
    return 1u;
}

u16 BMS_FailSafe_GetReason(void)
{
    return bms_failsafe_pair_ok(&g_bms_failsafe_reason, &g_bms_failsafe_reason_inv) ?
           g_bms_failsafe_reason : (u16)BMS_FAULT_INTERNAL_DATA;
}

u16 BMS_FailSafe_GetLastFatalReason(void)
{
    return bms_failsafe_pair_ok(&g_bms_failsafe_last_fatal, &g_bms_failsafe_last_fatal_inv) ?
           g_bms_failsafe_last_fatal : (u16)BMS_FAULT_INTERNAL_DATA;
}

u16 BMS_FailSafe_GetFlags(void)
{
    return bms_failsafe_pair_ok(&g_bms_failsafe_flags, &g_bms_failsafe_flags_inv) ?
           g_bms_failsafe_flags : (u16)(BMS_FAILSAFE_FLAG_ACTIVE | BMS_FAILSAFE_FLAG_FATAL_PENDING_RESET);
}
