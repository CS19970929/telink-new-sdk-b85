/*
 * 文件功能：SH3673510 均衡/Open-Wire 和功能快照适配；
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
static uint32_t s_ow_tick;
static uint32_t s_ow_raw[2];
#define SH_OPENWIRE_TIMEOUT_TICKS (2500u * 32u)

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
uint8_t sh3673510_backend_openwire_stop(void)
{
    if (!sh_ow_enable(0u)) return 0u;
    s_ow_busy = 0u;
    return 1u;
}

/* 开始后端非阻塞电芯断线检测。 */
uint8_t sh3673510_backend_openwire_start(void)
{
    uint8_t stale;
    if (s_ow_busy || !SH3673520_IsReady()) return 0u;
    s_ow_seen_odd = 0u; s_ow_seen_even = 0u; s_ow_attempts = 0u; s_ow_mask = 0u;
    s_ow_raw[0] = s_ow_raw[1] = 0u;
    /* FLAG3 是读清寄存器，仅本检测所有者读取，启动时排除上次完成标志。 */
    if (SH3673520_ReadReg(SH3673520_REG_FLAG3, &stale) != SH3673520_OK) return 0u;
    s_ow_busy = 1u;
    s_ow_tick = bms_diag_tick();
    if (!sh_ow_enable(1u) || !sh_ow_trigger()) return 0u;
    return 1u;
}

static void sh_ow_result(bms_afe_openwire_result_t *out, uint8_t error)
{
    if (out == 0) return;
    memset(out, 0, sizeof(*out));
    out->cell_count = SH3673510_BOARD_CELL_COUNT;
    out->phase_coverage = (uint8_t)(s_ow_seen_even | (s_ow_seen_odd << 1));
    out->raw_phase[0] = s_ow_raw[0]; out->raw_phase[1] = s_ow_raw[1];
    out->open_cell_mask = s_ow_mask & sh_valid_cell_mask();
    out->error = error;
}

/* 推进后端断线检测并返回阶段或结果。 */
bms_afe_diag_state_t sh3673510_backend_openwire_poll(bms_afe_openwire_result_t *out)
{
    uint8_t flag3, data[3];
    uint32_t raw, valid_mask = sh_valid_cell_mask();
    if (!s_ow_busy) return BMS_AFE_DIAG_IDLE;
    sh_ow_result(out, BMS_OW_ERR_NONE);
    /* 主循环丢失完成标志时也有墙钟上限，不能只靠已完成次数退出。 */
    if ((uint32_t)(bms_diag_tick()-s_ow_tick) >= SH_OPENWIRE_TIMEOUT_TICKS) {
        sh_ow_result(out, BMS_OW_ERR_TIMEOUT); return BMS_AFE_DIAG_ERROR;
    }
    if (SH3673520_ReadReg(SH3673520_REG_FLAG3, &flag3) != SH3673520_OK) {
        sh_ow_result(out, BMS_OW_ERR_IO); return BMS_AFE_DIAG_ERROR;
    }
    if ((flag3 & SH3673520_FLAG3_OWD_FLG_MASK) == 0u) return BMS_AFE_DIAG_BUSY;
    if (SH3673520_ReadRegs(SH3673520_REG_OWDH, data, 3u) != SH3673520_OK) {
        sh_ow_result(out, BMS_OW_ERR_IO); return BMS_AFE_DIAG_ERROR;
    }
    raw = (((uint32_t)data[0] & 0x0Fu) << 16) | ((uint32_t)data[1] << 8) | data[2];
    if (flag3 & SH3673520_FLAG3_OWD_IND_MASK) {
        s_ow_raw[1] = raw; s_ow_mask |= raw & 0x000AAAAAu; s_ow_seen_odd = 1u;
    } else {
        s_ow_raw[0] = raw; s_ow_mask |= raw & 0x00055555u; s_ow_seen_even = 1u;
    }
    ++s_ow_attempts;
    if (s_ow_seen_odd && s_ow_seen_even) {
        if (out != 0) {
            sh_ow_result(out, BMS_OW_ERR_NONE); out->valid = 1u;
            /* 手册第 16 页示例与第 44 页位编号存在冲突，未覆盖原始异常不能报健康。
             * 保留已有示例的保护位；双方原始掩码均为零才可确认健康并解除旧断线保护。 */
            out->determinate = (out->open_cell_mask != 0u ||
                ((s_ow_raw[0] | s_ow_raw[1]) & valid_mask) == 0u) ? 1u : 0u;
        }
        return BMS_AFE_DIAG_READY;
    }
    if (s_ow_attempts >= 3u) {
        sh_ow_result(out, BMS_OW_ERR_INCOMPLETE); return BMS_AFE_DIAG_ERROR;
    }
    if (!sh_ow_trigger()) {
        sh_ow_result(out, BMS_OW_ERR_IO); return BMS_AFE_DIAG_ERROR;
    }
    sh_ow_result(out, BMS_OW_ERR_NONE);
    return BMS_AFE_DIAG_BUSY;
}
