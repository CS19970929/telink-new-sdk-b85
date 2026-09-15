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

uint8_t dvc1124_backend_get_feature_snapshot(bms_afe_feature_snapshot_t *out)
{
    dvc1124_snapshot_t s;
    dvc1124_config_t cfg;
    uint8_t bat_index;
    uint8_t mos_index;

    if (out == 0) return 0u;
    memset(out, 0, sizeof(*out));
    DVC1124_GetSnapshot(&s);
    if (!s.valid) return 0u;
    DVC1124_GetConfig(&cfg);

    out->valid = 1u;
    out->cell_count = cfg.cell_count;
    bat_index = (cfg.battery_ntc_gp != 0u) ? (uint8_t)(cfg.battery_ntc_gp - 1u) : 0xFFu;
    mos_index = (cfg.mos_ntc_gp != 0u) ? (uint8_t)(cfg.mos_ntc_gp - 1u) : 0xFFu;

    if (bat_index < 4u && s.ntc_res_ohm[bat_index] != 0u) {
        out->battery_temp_valid = 1u;
        out->battery_temp_min_x10 = g_stCellInfoReport.u16Temperature[bat_index];
        out->battery_temp_max_x10 = g_stCellInfoReport.u16Temperature[bat_index];
    }
    if (mos_index < 4u && s.ntc_res_ohm[mos_index] != 0u) {
        out->heater_temp_valid = 1u;
        out->mos_temp_valid = 1u;
        out->heater_temp_x10 = g_stCellInfoReport.u16Temperature[mos_index];
        out->mos_temp_x10 = out->heater_temp_x10;
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
