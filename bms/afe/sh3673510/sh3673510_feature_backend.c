/*
 * 文件功能：SH3673510 均衡 和功能快照适配；
 * 仅处理有效 cell 通道及真实板级能力。
 * bms/afe/sh3673510/sh3673510_feature_backend.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_afe_driver.h"
#include "bms_state.h"
#include "bms_diag.h"
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#include "sh3673520.h"
#include "sh3673520_reg.h"
#include <string.h>

#define SH_FEATURE_BALANCE_REFRESH_TICKS (20000u * 32u) /* 20 秒，短于既有硬件超时。 */
static uint32_t s_balance_requested;
static uint32_t s_balance_refresh_tick;

/* 根据有效串数生成电芯通道掩码。 */
static uint32_t sh_valid_cell_mask(void)
{
    return (1uL << SH3673510_BOARD_CELL_COUNT) - 1uL;
}

/* 读取 SH 硬件当前均衡通道掩码。 */
static uint8_t sh_read_balance_mask(uint32_t *mask)
{
    uint8_t data[3];
    if (mask == 0) return 0u;
    if (SH3673520_ReadRegs(SH3673520_REG_BALANCEH, data, 3u) != SH3673520_OK) return 0u;
    *mask = ((((uint32_t)data[0] & 0x0Fu) << 16) |
             ((uint32_t)data[1] << 8) | data[2]) & sh_valid_cell_mask();
    return 1u;
}

/* 设置有效通道范围内的后端均衡掩码。 */
uint8_t sh3673510_backend_set_balance_mask(uint32_t cell_mask)
{
    uint32_t actual;
    cell_mask &= sh_valid_cell_mask();
    if (!sh_read_balance_mask(&actual)) return 0u;

    if (cell_mask == 0u) {
        s_balance_refresh_tick = bms_diag_tick();
        if (actual != 0u && !sh3673510_control_set_balance(0u)) return 0u;
        if (!sh_read_balance_mask(&actual) || actual != 0u) return 0u;
        s_balance_requested = 0u;
        return 1u;
    }

    if (actual != cell_mask || cell_mask != s_balance_requested ||
        (uint32_t)(bms_diag_tick() - s_balance_refresh_tick) >= SH_FEATURE_BALANCE_REFRESH_TICKS) {
        if (!sh3673510_control_set_balance(cell_mask)) return 0u;
        if (!sh_read_balance_mask(&actual) || actual != cell_mask) return 0u;
        s_balance_refresh_tick = bms_diag_tick();
    }
    s_balance_requested = cell_mask;
    return 1u;
}

/* 取得后端缓存或回读的均衡状态掩码。 */
uint8_t sh3673510_backend_get_balance_mask(uint32_t *cell_mask)
{
    return sh_read_balance_mask(cell_mask);
}
