/* 主循环上下文拥有累计时间；原始 32K 仅保留兼容观测。 */
#include "bms_time.h"
#include "tl_common.h"
#include "drivers.h"

static uint32_t s_last_raw_tick;
static uint32_t s_elapsed_ms;
static uint8_t s_fraction_ticks;

void bms_time_init(void)
{
    s_last_raw_tick = pm_get_32k_tick();
    s_elapsed_ms = 0u;
    s_fraction_ticks = 0u;
}

uint32_t bms_time_ms(void)
{
    uint32_t now = pm_get_32k_tick();
    uint32_t delta = now - s_last_raw_tick;
    uint32_t fraction = delta % 32u + s_fraction_ticks;
    s_last_raw_tick = now;
    /* 先取商余，delta 接近 UINT32_MAX 时也不发生加法溢出。 */
    s_elapsed_ms += delta / 32u + fraction / 32u;
    s_fraction_ticks = (uint8_t)(fraction % 32u);
    return s_elapsed_ms;
}

uint32_t bms_time_now_32k(void)
{
    return pm_get_32k_tick();
}
