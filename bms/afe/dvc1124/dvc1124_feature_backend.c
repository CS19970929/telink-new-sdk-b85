/* 文件功能：DVC1124 功能快照、均衡和 Open-Wire 适配；把芯片能力映射到公共功能策略。 */
#include "dvc1124.h"
#include "bms_afe_driver.h"
#include "bms_state.h"
#include <string.h>

static uint32_t s_balance_request;

/* 把驱动均衡状态转换为公共报告掩码。 */
static uint32_t dvc_balance_report_mask(void)
{
    return ((uint32_t)(g_bms_report.balance_bits_high & 0x00FFu) << 16) |
           g_bms_report.balance_bits_low;
}

/* 检查指定 NTC 测量是否有效。 */
static uint8_t dvc_ntc_valid(const dvc1124_snapshot_t *s, uint8_t gp)
{
    if ((s == 0) || (gp == 0u) || (gp > 4u)) return 0u;
    return (s->ntc_res_ohm[gp - 1u] != 0u) ? 1u : 0u;
}

/* 取得指定 NTC 通道的温度编码。 */
static uint16_t dvc_temp(uint8_t gp)
{
    if ((gp == 0u) || (gp > 4u)) return 0u;
    return g_bms_report.temperature_x10[gp - 1u];
}

/* 取得均衡、温度和断线策略需要的后端快照。 */
uint8_t dvc1124_backend_get_feature_snapshot(bms_afe_feature_snapshot_t *out)
{
    dvc1124_snapshot_t s;
    dvc1124_config_t cfg;
    uint16_t bat1;
    uint16_t bat2;

    if (out == 0) return 0u;
    memset(out, 0, sizeof(*out));
    DVC1124_GetSnapshot(&s);
    if (!s.valid) return 0u;
    DVC1124_GetConfig(&cfg);

    out->valid = 1u;
    out->cell_count = cfg.cell_count;

    /* HS-D008 温度职责：GP1 加热 MOS，GP2/GP3 电池，GP4 功率 MOS。 */
    if (dvc_ntc_valid(&s, DVC1124_DEFAULT_BATTERY_NTC_GP) &&
        dvc_ntc_valid(&s, DVC1124_DEFAULT_BATTERY_NTC2_GP))
    {
        bat1 = dvc_temp(DVC1124_DEFAULT_BATTERY_NTC_GP);
        bat2 = dvc_temp(DVC1124_DEFAULT_BATTERY_NTC2_GP);
        out->battery_temp_valid = 1u;
        out->battery_temp_min_x10 = (bat1 <= bat2) ? bat1 : bat2;
        out->battery_temp_max_x10 = (bat1 >= bat2) ? bat1 : bat2;
    }

    if (dvc_ntc_valid(&s, DVC1124_DEFAULT_HEATER_NTC_GP))
    {
        out->heater_temp_valid = 1u;
        out->heater_temp_x10 = dvc_temp(DVC1124_DEFAULT_HEATER_NTC_GP);
    }

    if (dvc_ntc_valid(&s, DVC1124_DEFAULT_MOS_NTC_GP))
    {
        out->mos_temp_valid = 1u;
        out->mos_temp_x10 = dvc_temp(DVC1124_DEFAULT_MOS_NTC_GP);
    }
    return 1u;
}

/* 返回后端可确认的充电源存在状态。 */
uint8_t dvc1124_backend_get_charge_source_present(uint8_t *present)
{
    /*
     * 尚无批准的充电器存在信号。PB1 是负载检测；独立产品策略确认前，
     * 板级回退也返回不存在。
     */
    if (present != 0) *present = 0u;
    return 0u;
}

/* 设置有效通道范围内的后端均衡掩码。 */
uint8_t dvc1124_backend_set_balance_mask(uint32_t cell_mask)
{
    uint32_t valid_mask;
    uint32_t actual;
    dvc1124_config_t cfg;

    DVC1124_GetConfig(&cfg);
    valid_mask = (cfg.cell_count >= 24u) ? 0x00FFFFFFu : ((1uL << cfg.cell_count) - 1uL);
    cell_mask &= valid_mask;
    actual = dvc_balance_report_mask() & valid_mask;
    if (cell_mask != s_balance_request || (cell_mask != 0u && actual == 0u)) {
        if (!DVC1124_SetBalanceMask(cell_mask)) return 0u;
        s_balance_request = cell_mask;
    }
    return DVC1124_BalanceService(cell_mask ? 1u : 0u);
}

/* 取得后端缓存或回读的均衡状态掩码。 */
uint8_t dvc1124_backend_get_balance_mask(uint32_t *cell_mask)
{
    return DVC1124_GetBalanceMask(cell_mask);
}

/* 开始后端非阻塞电芯断线检测。 */
uint8_t dvc1124_backend_openwire_start(void)
{
    return DVC1124_OpenWireBegin();
}

uint8_t dvc1124_backend_openwire_stop(void)
{
    return DVC1124_OpenWireStop();
}

/* 推进后端断线检测并返回阶段或结果。 */
bms_afe_diag_state_t dvc1124_backend_openwire_poll(bms_afe_openwire_result_t *out)
{
    dvc1124_openwire_result_t raw;
    uint8_t i;
    DVC1124_OpenWirePoll();
    DVC1124_OpenWireGetResult(&raw);
    if (raw.state == DVC1124_OPENWIRE_WAITING) return BMS_AFE_DIAG_BUSY;
    if (raw.state == DVC1124_OPENWIRE_IDLE) return BMS_AFE_DIAG_IDLE;
    if (raw.state == DVC1124_OPENWIRE_ERROR) {
        if (out != 0) out->error = raw.error;
        return BMS_AFE_DIAG_ERROR;
    }
    if (raw.state != DVC1124_OPENWIRE_READY) return BMS_AFE_DIAG_ERROR;
    if (out != 0) {
        memset(out, 0, sizeof(*out));
        out->valid = raw.valid;
        out->cell_count = raw.cell_count;
        out->determinate = raw.valid ? 1u : 0u;
        out->phase_coverage = raw.valid ? 1u : 0u;
        out->open_cell_mask = 0u;
        for (i = 0u; i < raw.cell_count && i < BMS_AFE_FEATURE_MAX_CELLS; ++i)
        {
            out->diagnostic_cell_mv[i] = raw.cell_mv[i];
            /*
             * DVC COW 对每个有效单体输入施加 100 uA 下拉；按手册，
             * 断开的采样输入在诊断窗口内被拉至 0 V。使用明确的 0 mV 判据，
             * 不擅自增加产品阈值。
             */
            if (raw.valid && raw.cell_mv[i] == 0u)
                out->open_cell_mask |= (1uL << i);
        }
    }
    return BMS_AFE_DIAG_READY;
}
