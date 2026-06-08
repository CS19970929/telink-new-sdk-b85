#include "bms_afe.h"

#include "app.h"
#include "drivers.h"
#include "sh367309_datadeal.h"
#include "sh3673520_afe.h"

extern struct stCell_Info g_stCellInfoReport;
extern u32 System_ERROR_UserCallback(enum SYSTEM_ERROR_COMMAND errorCode);

void bms_afe_bus_init(void)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    sh3673520_bus_init();
    sh3673520_param_load();
#else
    i2c_gpio_set(I2C_GPIO_GROUP_C0C1);
    i2c_master_init(AFE_ID, (unsigned char)(CLOCK_SYS_CLOCK_HZ / (4 * 100000)));
#endif
}

void bms_afe_reset(void)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    if (!sh3673520_reset()) {
        System_ERROR_UserCallback(ERROR_AFE1);
    }
#else
    AFE_Reset();
#endif
}

u8 bms_afe_is_ready(void)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    u8 ok = sh3673520_is_ready();
    if (!ok) {
        System_ERROR_UserCallback(ERROR_AFE1);
    }
    return ok;
#else
    return AFE_IsReady();
#endif
}

void bms_afe_apply_params(void)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    if (!sh3673520_apply_params()) {
        System_ERROR_UserCallback(ERROR_AFE1);
    }
#else
    SH367309_UpdataAfeConfig();
#endif
}

void bms_afe_sleep(void)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    sh3673520_sleep();
#else
    AFE_Sleep();
#endif
}

void bms_afe_sample(void)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    sh3673520_sample_t sample;
    if (sh3673520_read_sample(&sample)) {
        sh3673520_publish_to_cell_info(&sample, &g_stCellInfoReport);
        System_ERROR_UserCallback(ERROR_REMOVE_AFE1);
    } else {
        System_ERROR_UserCallback(ERROR_AFE1);
    }
#else
    App_AFEGet();
#endif
}

u16 bms_afe_param_read_word(u16 index)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    return sh3673520_param_read_word(index);
#else
    (void)index;
    return 0u;
#endif
}

int bms_afe_param_write_word(u16 index, u16 value)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    return sh3673520_param_write_word(index, value);
#else
    (void)index;
    (void)value;
    return 0;
#endif
}

int bms_afe_param_commit_and_apply(void)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    if (!sh3673520_param_commit()) {
        return 0;
    }
    return sh3673520_apply_params();
#else
    return 1;
#endif
}

int bms_afe_param_is_writable(u16 index)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    return sh3673520_param_is_writable(index);
#else
    (void)index;
    return 0;
#endif
}

u16 bms_afe_get_apply_status(void)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    return sh3673520_get_apply_status();
#else
    return 0u;
#endif
}

u32 bms_afe_get_pack_voltage_mv(void)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    return sh3673520_get_last_pack_mv();
#else
    return g_stCellInfoReport.u16VCellTotle;
#endif
}

int bms_afe_get_signed_current_ma(void)
{
#if (BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520)
    return sh3673520_get_last_current_ma();
#else
    if (g_stCellInfoReport.u16IDischg) {
        return -((int)g_stCellInfoReport.u16IDischg * 100);
    }
    return (int)g_stCellInfoReport.u16Ichg * 100;
#endif
}
