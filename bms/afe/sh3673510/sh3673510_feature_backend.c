/*
 * 文件功能：SH3673510 均衡/Open-Wire 和功能快照适配；
 * 仅处理有效 cell 通道及真实板级能力。
 * bms/afe/sh3673510/sh3673510_feature_backend.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_afe_driver.h"
#include "bms_state.h"
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#include "sh3673520.h"
#include "sh3673520_reg.h"
#include <string.h>

#define SH_FEATURE_BALANCE_REFRESH_SAMPLES 100u /*
 * 按 200 ms 节拍为 20 秒，短于 30.38 秒硬件超时。
 */
static uint32_t s_balance_requested;
static uint16_t s_balance_refresh_count;
static uint8_t s_ow_busy;
static uint8_t s_ow_seen_odd;
static uint8_t s_ow_seen_even;
static uint8_t s_ow_attempts;
static uint32_t s_ow_mask;

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
        s_balance_refresh_count = 0u;
        if (actual != 0u && !sh3673510_control_set_balance(0u)) return 0u;
        if (!sh_read_balance_mask(&actual) || actual != 0u) return 0u;
        s_balance_requested = 0u;
        return 1u;
    }

    if (s_balance_refresh_count < SH_FEATURE_BALANCE_REFRESH_SAMPLES) ++s_balance_refresh_count;
    if (actual != cell_mask || cell_mask != s_balance_requested ||
        s_balance_refresh_count >= SH_FEATURE_BALANCE_REFRESH_SAMPLES) {
        if (!sh3673510_control_set_balance((uint16_t)cell_mask)) return 0u;
        if (!sh_read_balance_mask(&actual) || actual != cell_mask) return 0u;
        s_balance_refresh_count = 0u;
    }
    s_balance_requested = cell_mask;
    return 1u;
}

/* 取得后端缓存或回读的均衡状态掩码。 */
uint8_t sh3673510_backend_get_balance_mask(uint32_t *cell_mask)
{
    uint32_t actual;
    if (cell_mask == 0 || !sh_read_balance_mask(&actual)) return 0u;
    *cell_mask = actual;
    return 1u;
}

/* 配置 SH 断线检测功能开关。 */
static uint8_t sh_ow_enable(uint8_t enable)
{
    uint8_t v, verify;
    if (SH3673520_ReadReg(SH3673520_REG_SCONF3, &v) != SH3673520_OK) return 0u;
    if (enable) v |= SH3673520_SCONF3_OWD_EN_MASK;
    else v &= (uint8_t)~SH3673520_SCONF3_OWD_EN_MASK;
    v &= (uint8_t)~SH3673520_SCONF3_OWD_TRG_MASK;
    if (SH3673520_WriteReg(SH3673520_REG_SCONF3, v) != SH3673520_OK) return 0u;
    if (SH3673520_ReadReg(SH3673520_REG_SCONF3, &verify) != SH3673520_OK) return 0u;
    return ((verify & SH3673520_SCONF3_OWD_EN_MASK) == (v & SH3673520_SCONF3_OWD_EN_MASK)) ? 1u : 0u;
}

/* 触发一次 SH 断线检测转换。 */
static uint8_t sh_ow_trigger(void)
{
    uint8_t v;
    if (SH3673520_ReadReg(SH3673520_REG_SCONF3, &v) != SH3673520_OK) return 0u;
    v |= (uint8_t)(SH3673520_SCONF3_OWD_EN_MASK | SH3673520_SCONF3_OWD_TRG_MASK);
    return (SH3673520_WriteReg(SH3673520_REG_SCONF3, v) == SH3673520_OK) ? 1u : 0u;
}

/* 终止断线检测并恢复正常采样配置。 */
static void sh_ow_abort(void)
{
    (void)sh_ow_enable(0u);
    s_ow_busy = 0u; s_ow_seen_odd = 0u; s_ow_seen_even = 0u; s_ow_attempts = 0u; s_ow_mask = 0u;
}

/* 开始后端非阻塞电芯断线检测。 */
uint8_t sh3673510_backend_openwire_start(void)
{
    if (s_ow_busy || !SH3673520_IsReady()) return 0u;
    s_ow_seen_odd = 0u; s_ow_seen_even = 0u; s_ow_attempts = 0u; s_ow_mask = 0u;
    if (!sh_ow_enable(1u) || !sh_ow_trigger()) { sh_ow_abort(); return 0u; }
    s_ow_busy = 1u;
    return 1u;
}

/* 推进后端断线检测并返回阶段或结果。 */
bms_afe_diag_state_t sh3673510_backend_openwire_poll(bms_afe_openwire_result_t *out)
{
    uint8_t flag3, data[3], i;
    uint32_t raw, valid_mask = sh_valid_cell_mask();
    if (!s_ow_busy) return BMS_AFE_DIAG_IDLE;
    if (SH3673520_ReadReg(SH3673520_REG_FLAG3, &flag3) != SH3673520_OK) { sh_ow_abort(); return BMS_AFE_DIAG_ERROR; }
    if ((flag3 & SH3673520_FLAG3_OWD_FLG_MASK) == 0u) return BMS_AFE_DIAG_BUSY;
    if (SH3673520_ReadRegs(SH3673520_REG_OWDH, data, 3u) != SH3673520_OK) { sh_ow_abort(); return BMS_AFE_DIAG_ERROR; }
    raw = (((uint32_t)data[0] & 0x0Fu) << 16) | ((uint32_t)data[1] << 8) | data[2];
    if (flag3 & SH3673520_FLAG3_OWD_IND_MASK) { s_ow_mask |= raw & 0x000AAAAAu; s_ow_seen_odd = 1u; }
    else { s_ow_mask |= raw & 0x00055555u; s_ow_seen_even = 1u; }
    ++s_ow_attempts;
    if (s_ow_seen_odd && s_ow_seen_even) {
        if (out != 0) {
            memset(out, 0, sizeof(*out)); out->valid = 1u; out->determinate = 1u;
            out->cell_count = SH3673510_BOARD_CELL_COUNT; out->open_cell_mask = s_ow_mask & valid_mask;
            for (i = 0u; i < SH3673510_BOARD_CELL_COUNT; ++i) out->diagnostic_cell_mv[i] = g_bms_report.u16VCell[i];
        }
        (void)sh_ow_enable(0u); s_ow_busy = 0u; s_ow_attempts = 0u;
        return BMS_AFE_DIAG_READY;
    }
    if (s_ow_attempts >= 3u || !sh_ow_trigger()) { sh_ow_abort(); return BMS_AFE_DIAG_ERROR; }
    return BMS_AFE_DIAG_BUSY;
}
