#include "runtime.h"
#include "drivers.h"
#include "flash_store_cfg.h"
#include "sci_upper.h"
#include "sh367309_datadeal.h"

extern struct stCell_Info g_stCellInfoReport;

typedef struct
{
    u32 runtime_min;
    u16 crc;
    u16 flag;
}runtime_store_t;

static u32 g_runtime_min = 0;
static bms_mode_t g_mode = MODE_NORMAL;


/* 简单CRC */
static u16 runtime_crc(u8 *data,u16 len)
{
    u16 crc = 0;

    for(u16 i = 0;i < len;i++)
    {
        crc += data[i];
    }

    return crc;
}


/* 从Flash读取 */
static void runtime_flash_load(void)
{
    runtime_store_t data;

    flash_read_page(FLASH_ADR_RUNTIME,sizeof(runtime_store_t),(u8 *)&data);

    if(data.flag != RUNTIME_FLAG)
    {
        g_runtime_min = 0;
        return;
    }

    u16 crc = runtime_crc((u8*)&data,sizeof(runtime_store_t)-4);

    if(crc != data.crc)
    {
        g_runtime_min = 0;
        return;
    }

    g_runtime_min = data.runtime_min;
}


/* 写Flash */
static void runtime_flash_save(void)
{
    runtime_store_t data;
    runtime_store_t read_check = {0};

    data.runtime_min = g_runtime_min;
    data.flag = RUNTIME_FLAG;

    data.crc = runtime_crc((u8*)&data,sizeof(runtime_store_t)-4);

    //todo 会有风险，断电？？？
    flash_erase_sector(FLASH_ADR_RUNTIME);
    flash_write_page(FLASH_ADR_RUNTIME,sizeof(runtime_store_t),(u8*)&data);

    // flash_read_page(FLASH_ADR_RUNTIME,sizeof(runtime_store_t),(u8 *)&read_check);

    // if(read_check.flag != data.flag)
    //     System_ERROR_UserCallback(ERROR_CAN);
    // if(read_check.runtime_min != data.runtime_min)
    //     System_ERROR_UserCallback(ERROR_CAN);
    // if(read_check.crc != data.crc)
    //     System_ERROR_UserCallback(ERROR_CAN);
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