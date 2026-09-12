#include "dvc1124.h"
#include "dvc1124_commands.h"

#include "bms_state.h"
#include "conf.h"
#include "drivers.h"
#include "param.h"
#include "sh367309_datadeal.h"
#include "tl_common.h"

#define DVC_BMS_SAMPLE_PERIOD_MS 200u

typedef enum
{
    DVC_FILTER_BAT_OVP = 0,
    DVC_FILTER_BAT_UVP,
    DVC_FILTER_CHG_OTP,
    DVC_FILTER_CHG_UTP,
    DVC_FILTER_DSG_OTP,
    DVC_FILTER_DSG_UTP,
    DVC_FILTER_MOS_OTP,
    DVC_FILTER_COUNT,
} dvc_filter_id_t;

typedef enum
{
    DVC_LIMIT_HIGH = 0,
    DVC_LIMIT_LOW,
} dvc_limit_direction_t;

typedef struct
{
    uint16_t assert_count;
    uint8_t active;
} dvc_filter_state_t;

extern struct stCell_Info g_stCellInfoReport;

static dvc_filter_state_t s_filters[DVC_FILTER_COUNT];
static union MDLCHGFAULT_REG s_prev_managed_faults;

static uint16_t dvc_filter_samples(uint16_t filter_10ms)
{
    uint32_t delay_ms = (uint32_t)filter_10ms * 10u;
    uint32_t samples;

    if (delay_ms == 0u) return 1u;

    samples = (delay_ms + DVC_BMS_SAMPLE_PERIOD_MS - 1u) /
              DVC_BMS_SAMPLE_PERIOD_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

static void dvc_filter_reset(dvc_filter_id_t id)
{
    s_filters[id].assert_count = 0u;
    s_filters[id].active = 0u;
}

static uint8_t dvc_filter_update(dvc_filter_id_t id,
                                 dvc_limit_direction_t direction,
                                 uint16_t value,
                                 uint16_t trip,
                                 uint16_t recover,
                                 uint16_t filter_10ms)
{
    dvc_filter_state_t *state = &s_filters[id];
    uint8_t asserted;
    uint8_t recovered;
    uint16_t required;

    if (trip == 0u)
    {
        dvc_filter_reset(id);
        return 0u;
    }

    if (direction == DVC_LIMIT_HIGH)
    {
        asserted = (value >= trip) ? 1u : 0u;
        recovered = (value <= recover) ? 1u : 0u;
    }
    else
    {
        asserted = (value <= trip) ? 1u : 0u;
        recovered = (value >= recover) ? 1u : 0u;
    }

    if (state->active)
    {
        if (recovered)
        {
            dvc_filter_reset(id);
        }
        return state->active;
    }

    if (!asserted)
    {
        state->assert_count = 0u;
        return 0u;
    }

    required = dvc_filter_samples(filter_10ms);
    if (state->assert_count < required)
    {
        ++state->assert_count;
    }
    if (state->assert_count >= required)
    {
        state->active = 1u;
        state->assert_count = 0u;
    }

    return state->active;
}

static uint16_t dvc_get_configured_temperature(uint8_t gp)
{
    if ((gp == 0u) || (gp > 4u)) return 0u;
    return g_stCellInfoReport.u16Temperature[gp - 1u];
}

static uint8_t dvc_clear_recovered_hw_latches(uint8_t alarm)
{
    uint8_t clear_mask = 0u;
    uint8_t verify;

    if ((alarm & DVC1124_ALARM_COV_MASK) &&
        (g_tParam.protect.u16VcellOvp_Rcv != 0u) &&
        (g_stCellInfoReport.u16VCellMax <= g_tParam.protect.u16VcellOvp_Rcv))
    {
        clear_mask |= DVC1124_ALARM_COV_MASK;
    }

    if ((alarm & DVC1124_ALARM_CUV_MASK) &&
        (g_tParam.protect.u16VcellUvp_Rcv != 0u) &&
        (g_stCellInfoReport.u16VCellMin >= g_tParam.protect.u16VcellUvp_Rcv))
    {
        clear_mask |= DVC1124_ALARM_CUV_MASK;
    }

    if (gpio_read(CHG_IN_PIN))
    {
        clear_mask |= (uint8_t)(alarm &
            (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK));
    }

    if (gpio_read(SW_PIN))
    {
        clear_mask |= (uint8_t)(alarm &
            (DVC1124_ALARM_OCD1_MASK |
             DVC1124_ALARM_OCD2_MASK |
             DVC1124_ALARM_SCD_MASK));
    }

    if (clear_mask == 0u) return alarm;
    if (!DVC1124_ClearAlarmFlags(clear_mask)) return alarm;
    if (!DVC1124_ReadRegisters(DVC1124_REG_ALARM, &verify, 1u)) return alarm;
    return verify;
}

static union MDLCHGFAULT_REG dvc_managed_fault_snapshot(void)
{
    union MDLCHGFAULT_REG managed;

    managed.all = 0u;
    managed.bits.b1CellOvp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp;
    managed.bits.b1CellUvp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp;
    managed.bits.b1BatOvp = g_stCellInfoReport.unMdlFault_Third.bits.b1BatOvp;
    managed.bits.b1BatUvp = g_stCellInfoReport.unMdlFault_Third.bits.b1BatUvp;
    managed.bits.b1IchgOcp = g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp;
    managed.bits.b1IdischgOcp = g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp;
    managed.bits.b1CellChgOtp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp;
    managed.bits.b1CellChgUtp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp;
    managed.bits.b1CellDischgOtp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp;
    managed.bits.b1CellDischgUtp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp;
    managed.bits.b1TmosOtp = g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp;
    return managed;
}

static void dvc_record_rising_faults(union MDLCHGFAULT_REG now)
{
#define DVC_RECORD_RISING(field, event) \
    do { \
        if (now.bits.field && !s_prev_managed_faults.bits.field) \
            FaultWarnRecord2(event); \
    } while (0)

    DVC_RECORD_RISING(b1CellOvp, CellOvp_Third);
    DVC_RECORD_RISING(b1CellUvp, CellUvp_Third);
    DVC_RECORD_RISING(b1BatOvp, BatOvp_Third);
    DVC_RECORD_RISING(b1BatUvp, BatUvp_Third);
    DVC_RECORD_RISING(b1IchgOcp, IchgOcp_Third);
    DVC_RECORD_RISING(b1IdischgOcp, IdischgOcp_Third);
    DVC_RECORD_RISING(b1CellChgOtp, CellChgOTp_Third);
    DVC_RECORD_RISING(b1CellChgUtp, CellChgUTp_Third);
    DVC_RECORD_RISING(b1CellDischgOtp, CellDsgOTp_Third);
    DVC_RECORD_RISING(b1CellDischgUtp, CellDsgUTp_Third);
    DVC_RECORD_RISING(b1TmosOtp, MosOTp_Third);

#undef DVC_RECORD_RISING
    s_prev_managed_faults = now;
}

static void dvc_publish_faults(uint8_t alarm, const dvc1124_config_t *cfg)
{
    uint16_t battery_temp = 0u;
    uint16_t mos_temp = 0u;
    uint16_t pack_x100 = g_stCellInfoReport.u16VCellTotle;
    union MDLCHGFAULT_REG managed;

    g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp =
        (alarm & DVC1124_ALARM_COV_MASK) ? 1u : 0u;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp =
        (alarm & DVC1124_ALARM_CUV_MASK) ? 1u : 0u;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp =
        (alarm & (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK)) ? 1u : 0u;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp =
        (alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK)) ? 1u : 0u;

    g_stCellInfoReport.unMdlFault_Third.bits.b1BatOvp =
        dvc_filter_update(DVC_FILTER_BAT_OVP, DVC_LIMIT_HIGH,
                          pack_x100,
                          g_tParam.protect.u16VbusOvp_Third,
                          g_tParam.protect.u16VbusOvp_Rcv,
                          g_tParam.protect.u16VbusOvp_Filter);

    g_stCellInfoReport.unMdlFault_Third.bits.b1BatUvp =
        dvc_filter_update(DVC_FILTER_BAT_UVP, DVC_LIMIT_LOW,
                          pack_x100,
                          g_tParam.protect.u16VbusUvp_Third,
                          g_tParam.protect.u16VbusUvp_Rcv,
                          g_tParam.protect.u16VbusUvp_Filter);

    if (cfg != NULL)
    {
        battery_temp = dvc_get_configured_temperature(cfg->battery_ntc_gp);
        mos_temp = dvc_get_configured_temperature(cfg->mos_ntc_gp);
    }

    if (battery_temp != 0u)
    {
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp =
            dvc_filter_update(DVC_FILTER_CHG_OTP, DVC_LIMIT_HIGH,
                              battery_temp,
                              g_tParam.protect.u16TChgOTp_Third,
                              g_tParam.protect.u16TChgOTp_Rcv,
                              g_tParam.protect.u16TChgOTp_Filter);
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp =
            dvc_filter_update(DVC_FILTER_CHG_UTP, DVC_LIMIT_LOW,
                              battery_temp,
                              g_tParam.protect.u16TchgUTp_Third,
                              g_tParam.protect.u16TchgUTp_Rcv,
                              g_tParam.protect.u16TchgUTp_Filter);
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp =
            dvc_filter_update(DVC_FILTER_DSG_OTP, DVC_LIMIT_HIGH,
                              battery_temp,
                              g_tParam.protect.u16TdischgOTp_Third,
                              g_tParam.protect.u16TdischgOTp_Rcv,
                              g_tParam.protect.u16TdischgOTp_Filter);
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp =
            dvc_filter_update(DVC_FILTER_DSG_UTP, DVC_LIMIT_LOW,
                              battery_temp,
                              g_tParam.protect.u16TdischgUTp_Third,
                              g_tParam.protect.u16TdischgUTp_Rcv,
                              g_tParam.protect.u16TdischgUTp_Filter);
    }
    else
    {
        dvc_filter_reset(DVC_FILTER_CHG_OTP);
        dvc_filter_reset(DVC_FILTER_CHG_UTP);
        dvc_filter_reset(DVC_FILTER_DSG_OTP);
        dvc_filter_reset(DVC_FILTER_DSG_UTP);
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp = 0u;
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp = 0u;
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp = 0u;
        g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp = 0u;
    }

    if (mos_temp != 0u)
    {
        g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp =
            dvc_filter_update(DVC_FILTER_MOS_OTP, DVC_LIMIT_HIGH,
                              mos_temp,
                              g_tParam.protect.u16TmosOTp_Third,
                              g_tParam.protect.u16TmosOTp_Rcv,
                              g_tParam.protect.u16TmosOTp_Filter);
    }
    else
    {
        dvc_filter_reset(DVC_FILTER_MOS_OTP);
        g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp = 0u;
    }

    if (alarm & DVC1124_ALARM_SCD_MASK)
    {
        if (!System_ERROR_UserCallback(ERROR_STATUS_CBC_DSG))
        {
            (void)System_ERROR_UserCallback(ERROR_CBC_DSG);
        }
    }
    else if (System_ERROR_UserCallback(ERROR_STATUS_CBC_DSG))
    {
        (void)System_ERROR_UserCallback(ERROR_REMOVE_CBC_DSG);
    }

    managed = dvc_managed_fault_snapshot();
    dvc_record_rising_faults(managed);
}

static uint8_t dvc_charge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *fault = &g_stCellInfoReport.unMdlFault_Third.bits;

    return (fault->b1CellOvp || fault->b1BatOvp || fault->b1IchgOcp ||
            fault->b1CellChgOtp || fault->b1CellChgUtp || fault->b1TmosOtp ||
            System_ERROR_UserCallback(ERROR_STATUS_TEMP_BREAK)) ? 1u : 0u;
}

static uint8_t dvc_discharge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *fault = &g_stCellInfoReport.unMdlFault_Third.bits;

    return (fault->b1CellUvp || fault->b1BatUvp || fault->b1IdischgOcp ||
            fault->b1CellDischgOtp || fault->b1CellDischgUtp || fault->b1TmosOtp ||
            System_ERROR_UserCallback(ERROR_STATUS_CBC_DSG) ||
            System_ERROR_UserCallback(ERROR_STATUS_TEMP_BREAK)) ? 1u : 0u;
}

static uint8_t dvc_enforce_fault_fet_state(void)
{
    uint8_t charge_on = SystemStatus.bits.b1Status_MOS_CHG ? 1u : 0u;
    uint8_t discharge_on = SystemStatus.bits.b1Status_MOS_DSG ? 1u : 0u;
    uint8_t requested_charge = charge_on;
    uint8_t requested_discharge = discharge_on;

    if (dvc_charge_blocked()) charge_on = 0u;
    if (dvc_discharge_blocked()) discharge_on = 0u;

    if ((charge_on == requested_charge) && (discharge_on == requested_discharge))
    {
        return 1u;
    }

    if (!DVC1124_SetMosState(charge_on, discharge_on))
    {
        (void)System_ERROR_UserCallback(ERROR_AFE1);
        return 0u;
    }

    return 1u;
}

void DVC1124_BmsApp_AFEGet(void)
{
    dvc1124_snapshot_t snapshot;
    dvc1124_config_t cfg;
    uint8_t alarm;

    DVC1124_App_AFEGet();
    DVC1124_GetSnapshot(&snapshot);
    if (!snapshot.valid) return;

    DVC1124_GetConfig(&cfg);
    alarm = dvc_clear_recovered_hw_latches(snapshot.alarm);
    dvc_publish_faults(alarm, &cfg);
    (void)dvc_enforce_fault_fet_state();
}

uint8_t DVC1124_BmsCompatMTPWrite(uint8_t wr_addr,
                                  uint8_t length,
                                  const uint8_t *wr_buf)
{
    uint8_t value;

    if ((wr_buf == NULL) || (length == 0u)) return 0u;

    if (wr_addr != 0x40u)
    {
        return DVC1124_CompatMTPWrite(wr_addr, length, wr_buf);
    }

    value = wr_buf[0];
    if ((value & 0x10u) && dvc_charge_blocked()) value &= (uint8_t)~0x10u;
    if ((value & 0x20u) && dvc_discharge_blocked()) value &= (uint8_t)~0x20u;
    return DVC1124_CompatMTPWrite(wr_addr, 1u, &value);
}
