#include "bms_afe.h"
#include "bms_actuator.h"
#include "bms_diagnostics.h"
#include "bms_safety_config.h"
#include "sh367309_datadeal.h"

static bms_afe_error_t g_error;
static uint32_t g_last_frame_ms;
static uint32_t g_last_config_check_ms;
static uint8_t g_config_valid;
static uint8_t g_frame_valid;
static volatile uint8_t g_alarm_pending;
static uint8_t g_revalidation_required;

void bms_afe_init(void)
{
    g_error = BMS_AFE_ERR_READY;
    g_last_frame_ms = 0u;
    g_last_config_check_ms = 0u;
    g_config_valid = 0u;
    g_frame_valid = 0u;
    g_alarm_pending = 0u;
    g_revalidation_required = 0u;
    bms_set_global_inhibit(BMS_INHIBIT_AFE_COMM, 1u);
    bms_set_global_inhibit(BMS_INHIBIT_AFE_CONFIG, 1u);
}

bms_afe_error_t bms_afe_startup(void)
{
    extern int AFE_PARAM_WRITE_Flag;
    if (!AFE_Reset()) {
        g_error = BMS_AFE_ERR_RESET;
        return g_error;
    }
    if (AFE_IsReady() != 0u) {
        g_error = BMS_AFE_ERR_READY;
        return g_error;
    }
    AFE_PARAM_WRITE_Flag = 1;
    if (!SH367309_UpdataAfeConfig()) {
        g_error = BMS_AFE_ERR_CONFIG_WRITE;
        return g_error;
    }
    if (!SH367309_VerifyAfeConfig()) {
        g_error = BMS_AFE_ERR_CONFIG_VERIFY;
        return g_error;
    }
    g_config_valid = 1u;
    g_error = BMS_AFE_OK;
    bms_diagnostics_set_afe_config_valid(1u);
    bms_set_global_inhibit(BMS_INHIBIT_AFE_CONFIG, 0u);
    return BMS_AFE_OK;
}

void bms_afe_note_frame(uint8_t valid, uint32_t now_ms)
{
    g_frame_valid = valid ? 1u : 0u;
    if (valid) {
        g_last_frame_ms = now_ms;
        if (g_config_valid) {
            g_error = BMS_AFE_OK;
            bms_set_global_inhibit(BMS_INHIBIT_AFE_COMM, 0u);
        }
    } else {
        g_error = BMS_AFE_ERR_COMM_CRC;
        bms_set_global_inhibit(BMS_INHIBIT_AFE_COMM, 1u);
    }
}

void bms_afe_alarm_isr(void)
{
    g_alarm_pending = 1u;
}

void bms_afe_require_reconfigure(void)
{
    g_config_valid = 0u;
    g_alarm_pending = 1u;
    bms_diagnostics_set_afe_config_valid(0u);
    bms_set_global_inhibit(BMS_INHIBIT_AFE_CONFIG, 1u);
}

void bms_afe_poll(uint32_t now_ms)
{
    uint8_t need_check = 0u;
    if (g_last_frame_ms != 0u &&
        (uint32_t)(now_ms - g_last_frame_ms) > BMS_SAMPLE_STALE_MS) {
        g_frame_valid = 0u;
        g_error = BMS_AFE_ERR_SAMPLE_TIMEOUT;
        bms_set_global_inhibit(BMS_INHIBIT_AFE_COMM, 1u);
    }
#if BMS_AFE_RUNTIME_CONFIG_CHECK_ENABLE
    if ((uint32_t)(now_ms - g_last_config_check_ms) >= BMS_CONFIG_CHECK_PERIOD_MS)
        need_check = 1u;
#endif
    if (g_alarm_pending) {
        g_alarm_pending = 0u;
        need_check = 1u;
    }
    if (need_check) {
        g_last_config_check_ms = now_ms;
        if (!SH367309_VerifyAfeConfig()) {
            g_config_valid = 0u;
            g_revalidation_required = 1u;
            g_error = BMS_AFE_ERR_CONFIG_VERIFY;
            bms_diagnostics_set_afe_config_valid(0u);
            bms_set_global_inhibit(BMS_INHIBIT_AFE_CONFIG, 1u);
            bms_actuator_update(now_ms);
            /* Reconfiguration is attempted only while the global inhibit is set. */
            {
                extern int AFE_PARAM_WRITE_Flag;
                AFE_PARAM_WRITE_Flag = 1;
            }
            if (SH367309_UpdataAfeConfig() && SH367309_VerifyAfeConfig()) {
                g_config_valid = 1u;
                bms_diagnostics_set_afe_config_valid(1u);
                bms_set_global_inhibit(BMS_INHIBIT_AFE_CONFIG, 0u);
            }
        } else {
            g_config_valid = 1u;
            bms_diagnostics_set_afe_config_valid(1u);
            bms_set_global_inhibit(BMS_INHIBIT_AFE_CONFIG, 0u);
        }
    }
}

uint8_t bms_afe_take_revalidation_request(void)
{
    uint8_t requested = g_revalidation_required;
    g_revalidation_required = 0u;
    return requested;
}

uint8_t bms_afe_config_valid(void) { return g_config_valid; }
uint8_t bms_afe_frame_valid(void) { return g_frame_valid; }
uint8_t bms_afe_charge_fet_on(void) { return ram_reg_309.REG_BSTATUS3.bits.CHG_FET; }
uint8_t bms_afe_discharge_fet_on(void) { return ram_reg_309.REG_BSTATUS3.bits.DSG_FET; }
bms_afe_error_t bms_afe_last_error(void) { return g_error; }
