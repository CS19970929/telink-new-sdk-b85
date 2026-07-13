#include "bms_diag.h"

#include "bms_selftest.h"
#include "bms_selftest_cfg.h"
#include "bms_failsafe.h"
#include "bms_fault_inject.h"

static u16 bms_diag_low(u32 value)
{
    return (u16)(value & 0xffffu);
}

static u16 bms_diag_high(u32 value)
{
    return (u16)(value >> 16);
}

u8 BMS_Diag_ReadReg(u16 reg, u16 *value)
{
    const bms_selftest_diag_t *diag;
    u16 offset;

    if ((value == 0) || (reg < BMS_DIAG_REG_BASE) ||
        (reg >= (u16)(BMS_DIAG_REG_BASE + BMS_DIAG_REG_COUNT)))
    {
        return 0u;
    }
    diag = BMS_SelfTest_GetDiag();
    offset = (u16)(reg - BMS_DIAG_REG_BASE);
    switch (offset)
    {
    case 0u: *value = 0x4253u; break;
    case 1u: *value = 0x0001u; break;
    case 2u: *value = diag->state; break;
    case 3u: *value = diag->startup_result; break;
    case 4u: *value = diag->runtime_result; break;
    case 5u: *value = diag->active_fault; break;
    case 6u: *value = diag->last_fatal_fault; break;
    case 7u: *value = diag->current_item; break;
    case 8u: *value = (u16)(diag->flags | (BMS_DIAG_TEST_BUILD ? 0x8000u : 0u)); break;
    case 9u: *value = bms_diag_low(diag->fault_mask); break;
    case 10u: *value = bms_diag_high(diag->fault_mask); break;
    case 11u: *value = bms_diag_low(diag->expected_flash_crc); break;
    case 12u: *value = bms_diag_high(diag->expected_flash_crc); break;
    case 13u: *value = bms_diag_low(diag->actual_flash_crc); break;
    case 14u: *value = bms_diag_high(diag->actual_flash_crc); break;
    case 15u: *value = bms_diag_low(diag->flash_progress); break;
    case 16u: *value = bms_diag_high(diag->flash_progress); break;
    case 17u: *value = bms_diag_low(diag->flash_total); break;
    case 18u: *value = bms_diag_high(diag->flash_total); break;
    case 19u: *value = bms_diag_low(diag->irq_count); break;
    case 20u: *value = bms_diag_high(diag->irq_count); break;
    case 21u: *value = bms_diag_low(diag->irq_delta); break;
    case 22u: *value = bms_diag_high(diag->irq_delta); break;
    case 23u: *value = bms_diag_low(diag->stack_high_water); break;
    case 24u: *value = bms_diag_high(diag->stack_high_water); break;
    case 25u: *value = bms_diag_low(diag->heartbeat_seen); break;
    case 26u: *value = bms_diag_low(diag->heartbeat_missing); break;
    case 27u: *value = diag->runtime_cycles; break;
    case 28u: *value = BMS_FailSafe_GetFlags(); break;
    case 29u: *value = bms_diag_low(BMS_FaultInject_GetMask()); break;
    case 30u: *value = bms_diag_high(BMS_FaultInject_GetMask()); break;
    case 31u: *value = BMS_SELFTEST_ENABLE ? 1u : 0u; break;
    default: *value = 0u; break;
    }
    return 1u;
}
