#include "bms_selftest_internal.h"
#include "bms_fault_inject.h"
#include "bms_failsafe.h"

#include "sh367309_datadeal.h"

static u8 g_bms_adc_fault_count[3];
static u8 g_bms_afe_comm_fault_count;
static u8 g_bms_afe_config_fault_count;
static u8 g_bms_mos_fault_count;
static u8 g_bms_mos_charge_feedback;
static u8 g_bms_mos_discharge_feedback;
static u8 g_bms_mos_precharge_feedback;

void BMS_SelfTest_ReportAdcSample(u8 channel, u16 millivolts)
{
    if (channel >= 3u)
    {
        return;
    }
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_ADC))
    {
        millivolts = 0u;
    }
    if ((millivolts <= 1u) || (millivolts >= 3299u))
    {
        if (g_bms_adc_fault_count[channel] < 0xffu) ++g_bms_adc_fault_count[channel];
    }
    else
    {
        g_bms_adc_fault_count[channel] = 0u;
    }
}

void BMS_SelfTest_ReportAfeStatus(u8 communication_ok, u8 configuration_ok)
{
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_AFE_COMM)) communication_ok = 0u;
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_AFE_CONFIG)) configuration_ok = 0u;
    g_bms_afe_comm_fault_count = communication_ok ? 0u :
        (g_bms_afe_comm_fault_count < 0xffu ? (u8)(g_bms_afe_comm_fault_count + 1u) : 0xffu);
    g_bms_afe_config_fault_count = configuration_ok ? 0u :
        (g_bms_afe_config_fault_count < 0xffu ? (u8)(g_bms_afe_config_fault_count + 1u) : 0xffu);
}

void BMS_SelfTest_ReportMosFeedback(u8 charge_on, u8 discharge_on, u8 precharge_on)
{
    g_bms_mos_charge_feedback = charge_on ? 1u : 0u;
    g_bms_mos_discharge_feedback = discharge_on ? 1u : 0u;
    g_bms_mos_precharge_feedback = precharge_on ? 1u : 0u;
}

void BMS_SelfTest_ApplicationProcess(void)
{
    u8 mismatch;

    if ((g_bms_adc_fault_count[0] >= BMS_SELFTEST_ADC_FAULT_LIMIT) ||
        (g_bms_adc_fault_count[1] >= BMS_SELFTEST_ADC_FAULT_LIMIT) ||
        (g_bms_adc_fault_count[2] >= BMS_SELFTEST_ADC_FAULT_LIMIT))
    {
        BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_ADC, BMS_FAULT_ADC);
    }
    if (g_bms_afe_comm_fault_count >= BMS_SELFTEST_AFE_FAULT_LIMIT)
    {
        BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_AFE, BMS_FAULT_AFE_COMM);
    }
    if (g_bms_afe_config_fault_count >= BMS_SELFTEST_AFE_FAULT_LIMIT)
    {
        BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_AFE, BMS_FAULT_AFE_CONFIG);
    }

    if (BMS_FailSafe_IsActive())
    {
        mismatch = (u8)(g_bms_mos_charge_feedback || g_bms_mos_discharge_feedback ||
                        g_bms_mos_precharge_feedback);
    }
    else
    {
        mismatch = (u8)((g_bms_mos_charge_feedback != SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS) ||
                        (g_bms_mos_discharge_feedback != SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS) ||
                        (g_bms_mos_precharge_feedback != SH367309_Reg_Store.REG_MTP_CONF.bits.PCHMOS));
    }
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_MOS_FEEDBACK)) mismatch = 1u;
    if (mismatch)
    {
        if (g_bms_mos_fault_count < 0xffu) ++g_bms_mos_fault_count;
        if (g_bms_mos_fault_count >= BMS_SELFTEST_MOS_FAULT_LIMIT)
        {
            BMS_SelfTest_RecordFailure(BMS_SELFTEST_ITEM_MOS, BMS_FAULT_MOS_FEEDBACK);
        }
    }
    else
    {
        g_bms_mos_fault_count = 0u;
    }
}
