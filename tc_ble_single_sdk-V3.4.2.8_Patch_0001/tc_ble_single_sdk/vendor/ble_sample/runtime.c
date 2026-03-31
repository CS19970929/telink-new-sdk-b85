#include "runtime.h"
#include "drivers.h"
#include "flash_store_cfg.h"
#include "runtime_state_store.h"
#include "sci_upper.h"
#include "sh367309_datadeal.h"

extern struct stCell_Info g_stCellInfoReport;

static u32 g_runtime_min = 0;
static bms_mode_t g_mode = MODE_NORMAL;



static void runtime_flash_load(void)
{
    flash_runtime_state_t state;

    if (runtime_state_store_load(&state))
    {
        g_runtime_min = state.runtime_min;
    }
    else
    {
        g_runtime_min = 0;
    }
}


/* 写Flash */
static void runtime_flash_save(void)
{
    flash_runtime_state_t state;

    runtime_state_store_load(&state);
    state.runtime_min = g_runtime_min;
    state.factory_expired = (g_runtime_min >= FACTORY_TIME_LIMIT_MIN) ? 1 : 0;
    state.bms_mode = state.factory_expired ? MODE_NORMAL : MODE_FACTORY;
    runtime_state_store_save(&state);
}

/* 初始化 */
void Runtime_Init(void)
{
    runtime_flash_load();

    if(g_runtime_min >= FACTORY_TIME_LIMIT_MIN)
    {
        g_mode = MODE_NORMAL;
    }
    else
    {
        g_mode = MODE_FACTORY;
    }
}


extern void enter_fac_mode(bool on);
/* 每分钟调用一次 */
void Runtime_1MinTask(void)
{
    if(g_mode == MODE_NORMAL)
        return;

    g_runtime_min++;

    if(g_runtime_min >= FACTORY_TIME_LIMIT_MIN)
    {
        g_mode = MODE_NORMAL;

        runtime_flash_save();   // 最后保存一次
        enter_fac_mode(false);
        return;
    }

    runtime_flash_save();
}


bms_mode_t Runtime_GetMode(void)
{
    return g_mode;
}
u32 Runtime_Get_runtime(void)
{
    return g_runtime_min;
}