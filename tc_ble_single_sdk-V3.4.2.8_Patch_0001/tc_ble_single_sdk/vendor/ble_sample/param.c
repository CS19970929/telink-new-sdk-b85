#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "param.h"
#include "bms_cold_kv_store.h"
#include <string.h>

PARAM_T g_tParam;

static void param_fill_default(PARAM_T *param)
{
    param->ParamVer = PARAM_VER;
    bms_cold_kv_store_get_default_protect(&param->protect);
}

static int param_protect_equals(const struct PRT_E2ROM_PARAS *a, const struct PRT_E2ROM_PARAS *b)
{
    return (memcmp(a, b, sizeof(*a)) == 0);
}

void LoadParam(void)
{
#if defined(PARAM_SAVE_TO_EEPROM)
#error "bms_cold_kv_store currently supports Flash-backed param storage only"
#endif
    PARAM_T legacy_param;
    struct PRT_E2ROM_PARAS default_protect;

    memset(&legacy_param, 0xFF, sizeof(legacy_param));

#ifdef PARAM_SAVE_TO_FLASH
    flash_read_page(PARAM_ADDR, sizeof(PARAM_T), (u8 *)&legacy_param);
#endif

    if (!bms_cold_kv_store_init()) {
        param_fill_default(&g_tParam);
        return;
    }

    g_tParam.ParamVer = PARAM_VER;
    if (!bms_cold_kv_store_get_protect(&g_tParam.protect)) {
        param_fill_default(&g_tParam);
        (void)bms_cold_kv_store_set_protect(&g_tParam.protect);
        return;
    }

    bms_cold_kv_store_get_default_protect(&default_protect);
    if ((legacy_param.ParamVer == PARAM_VER) &&
        !param_protect_equals(&legacy_param.protect, &g_tParam.protect) &&
        param_protect_equals(&g_tParam.protect, &default_protect)) {
        g_tParam.protect = legacy_param.protect;
        (void)bms_cold_kv_store_set_protect(&g_tParam.protect);
    }
}

void SaveParam(void)
{
    g_tParam.ParamVer = PARAM_VER;
    (void)bms_cold_kv_store_set_protect(&g_tParam.protect);
}
