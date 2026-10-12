/* 文件功能：DVC1124 功能快照和均衡适配；把芯片能力映射到公共功能策略。 */
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

/* 取得均衡和温度策略需要的后端快照。 */
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
    /* 任一通道与请求不符就重发，包括残留非零位和部分通道提前关闭。 */
    if (cell_mask != s_balance_request || actual != cell_mask) {
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
