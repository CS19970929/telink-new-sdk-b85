#include "dvc1124.h"
#include "bms_afe.h"
#include "bms_state.h"
#include <string.h>

static uint32_t s_balance_request;

static uint32_t dvc_balance_report_mask(void)
{
    return ((uint32_t)(g_stCellInfoReport.u16BalanceFlag2 & 0x00FFu) << 16) |
           g_stCellInfoReport.u16BalanceFlag1;
}

static uint8_t dvc_ntc_valid(const dvc1124_snapshot_t *s, uint8_t gp)
{
    if ((s == 0) || (gp == 0u) || (gp > 4u)) return 0u;
    return (s->ntc_res_ohm[gp - 1u] != 0u) ? 1u : 0u;
}

static uint16_t dvc_temp(uint8_t gp)
{
    if ((gp == 0u) || (gp > 4u)) return 0u;
    return g_stCellInfoReport.u16Temperature[gp - 1u];
}

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

    /* HS-D008 temperature ownership:
     * GP1 heater MOS, GP2/GP3 battery, GP4 power MOS. */
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

uint8_t dvc1124_backend_get_charge_source_present(uint8_t *present)
{
    /* D008 has a schematic-proven active-low CHG-IN/PB1 board input. The DVC
     * backend deliberately reports 'unsupported' so common policy falls back
     * to bms_board_charge_source_present(). */
    if (present != 0) *present = 0u;
    return 0u;
}

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
    DVC1124_BalanceService(cell_mask ? 1u : 0u);
    return 1u;
}

uint8_t dvc1124_backend_get_balance_mask(uint32_t *cell_mask)
{
    if (cell_mask == 0) return 0u;
    *cell_mask = dvc_balance_report_mask();
    return 1u;
}

uint8_t dvc1124_backend_openwire_start(void)
{
    return DVC1124_OpenWireBegin();
}

bms_afe_diag_state_t dvc1124_backend_openwire_poll(bms_afe_openwire_result_t *out)
{
    dvc1124_openwire_result_t raw;
    uint8_t i;
    DVC1124_OpenWirePoll();
    DVC1124_OpenWireGetResult(&raw);
    if (raw.state == DVC1124_OPENWIRE_WAITING) return BMS_AFE_DIAG_BUSY;
    if (raw.state == DVC1124_OPENWIRE_IDLE) return BMS_AFE_DIAG_IDLE;
    if (raw.state == DVC1124_OPENWIRE_ERROR) { DVC1124_OpenWireReset(); return BMS_AFE_DIAG_ERROR; }
    if (raw.state != DVC1124_OPENWIRE_READY) return BMS_AFE_DIAG_ERROR;
    if (out != 0) {
        memset(out, 0, sizeof(*out));
        out->valid = raw.valid;
        out->cell_count = raw.cell_count;
        /* V1.2 specifies the stimulus/timing but not a universal final decision
         * threshold. Keep raw diagnostics and no fault verdict until D008 fixture
         * tests sign off a product criterion. */
        out->determinate = 0u;
        out->open_cell_mask = 0u;
        for (i = 0u; i < raw.cell_count && i < BMS_AFE_FEATURE_MAX_CELLS; ++i)
            out->diagnostic_cell_mv[i] = raw.cell_mv[i];
    }
    DVC1124_OpenWireReset();
    return BMS_AFE_DIAG_READY;
}
