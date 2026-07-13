#include "bms_selftest_port.h"

#include "app.h"
#include "conf.h"
#include "drivers.h"
#include "sh367309_datadeal.h"

#define BMS_RESET_MARKER_REG DEEP_ANA_REG1

static volatile u8 g_bms_port_application_ready;

static _attribute_ram_code_ void bms_port_gpio_output_low(GPIO_PinTypeDef pin)
{
    u8 bit = (u8)(pin & 0xffu);

    BM_SET(reg_gpio_func(pin), bit);
    BM_CLR(reg_gpio_oen(pin), bit);
    BM_CLR(reg_gpio_out(pin), bit);
}

void BMS_Port_ForceDangerousOutputsOffEarly(void)
{
    bms_port_gpio_output_low(MCC_C_PIN);
    bms_port_gpio_output_low(AFE_CTL_PIN);
#ifdef _UL_RENZHENG_ENABLE_
    bms_port_gpio_output_low(RF_EN_PIN);
#endif
}

void BMS_Port_ForceDangerousOutputsOff(void)
{
    u8 zero = 0u;

    BMS_Port_ForceDangerousOutputsOffEarly();
    if (!g_bms_port_application_ready)
    {
        return;
    }

    SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 0u;
    SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 0u;
    SH367309_Reg_Store.REG_MTP_CONF.bits.PCHMOS = 0u;
    SH367309_Reg_Store.u8_MTP_BALANCEH = 0u;
    SH367309_Reg_Store.u8_MTP_BALANCEL = 0u;
    (void)MTPWrite(MTP_CONF, 1u, &SH367309_Reg_Store.REG_MTP_CONF.all);
    (void)MTPWrite(MTP_BALANCEH, 1u, &zero);
    (void)MTPWrite(MTP_BALANCEL, 1u, &zero);
}

void BMS_Port_SetApplicationReady(u8 ready)
{
    g_bms_port_application_ready = ready ? 1u : 0u;
}

u8 BMS_Port_IsApplicationReady(void)
{
    return g_bms_port_application_ready;
}

u8 BMS_Port_IsOtaWorking(void)
{
    return ota_is_working ? 1u : 0u;
}

u8 BMS_Port_IsLowPower(void)
{
    return sys_time.low_power_mode ? 1u : 0u;
}

u8 BMS_Port_VerifyAfeConfiguration(void)
{
    return SH367309_VerifyAfeConfig() ? 1u : 0u;
}

u32 BMS_Port_GetRunningImageBase(void)
{
    return ota_program_offset ? 0u : ota_program_bootAddr;
}

u8 BMS_Port_ReadResetMarker(void)
{
    return analog_read(BMS_RESET_MARKER_REG);
}

void BMS_Port_WriteResetMarker(u8 marker)
{
    analog_write(BMS_RESET_MARKER_REG, marker);
}
