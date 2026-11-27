#include "product.h"
#include <string.h>

#ifndef D008_CAPACITY_DAH
#define D008_CAPACITY_DAH 200
#endif

void config_defaults(Config *c, unsigned cells, bool nmc)
{
    /* 工程候选按当前装配合同取值。快保护还需分流标定、Vgs 和 SOA 验收。 */
    memset(c, 0, sizeof(*c));
    c->cells = cells; c->nmc = nmc; c->capacity_dah = D008_CAPACITY_DAH;
    c->full_mv = nmc ? 4180 : 3550; c->empty_mv = nmc ? 3100 : 2600;
    c->sw[OV] = (Limit){nmc ? 4200 : 3500, nmc ? 4200 : 3600, nmc ? 4200 : 3650, nmc ? 4100 : 3550, 1000};
    c->sw[UV] = (Limit){nmc ? 3200 : 3000, nmc ? 3100 : 2900, nmc ? 3000 : 2500, nmc ? 3100 : 2900, 1000};
    c->sw[PACK_OV] = (Limit){cells*c->sw[OV].alarm1/10, cells*c->sw[OV].alarm2/10, cells*c->sw[OV].trip/10, cells*(nmc ? 4100 : 3500)/10, 1000};
    c->sw[PACK_UV] = (Limit){cells*c->sw[UV].alarm1/10, cells*c->sw[UV].alarm2/10, cells*c->sw[UV].trip/10, cells*(nmc ? 3100 : 3000)/10, 1000};
    c->sw[CHARGE_OC] = (Limit){300, 350, 400, 10, 100};
    c->sw[DISCHARGE_OC] = (Limit){1000, 1100, 1200, 10, 500};
    c->sw[CHARGE_HOT] = (Limit){800, 900, 950, 800, 1000};
    c->sw[CHARGE_COLD] = (Limit){450, 430, 400, 430, 1000};
    c->sw[DISCHARGE_HOT] = (Limit){900, 1000, 1050, 900, 1000};
    c->sw[DISCHARGE_COLD] = (Limit){300, 250, 200, 300, 1000};
    c->sw[MOS_HOT] = (Limit){1150, 1250, 1350, 1200, 1000};
    c->sw[CELL_DELTA] = (Limit){600, 800, 1000, 800, 1000};
    c->sw[SOC_LOW] = (Limit){20,10,5,11,1000};
    c->hw_ov_mv = nmc ? 4200 : 3750; c->hw_uv_mv = nmc ? 3000 : 2500;
    c->hw_ov_ms = c->hw_uv_ms = 1000;
    c->hw_d1_a10 = 1500; c->hw_c1_a10 = 600;
    c->hw_d2_a10 = 3000; c->hw_c2_a10 = 800;
    /* 铁锂沿用旧 D008 的 150 mV。分流电阻标称 250 µΩ。 */
    c->hw_sc_a10 = nmc ? 2000 : 6000;
    c->hw_d1_ms = c->hw_c1_ms = c->hw_d2_ms = c->hw_c2_ms = 100;
    c->hw_sc_us = 256;
    c->balance_on = 1; c->balance_mv = nmc ? 4100 : 3400;
    c->balance_start_mv = 50; c->balance_stop_mv = 30;
    c->gain_ppm = 1000000;
}

static bool in_range(uint32_t value, uint32_t lo, uint32_t hi)
{
    return value >= lo && value <= hi;
}

bool limit_low(unsigned i)
{
    return i==UV || i==PACK_UV || i==CHARGE_COLD || i==DISCHARGE_COLD || i==SOC_LOW;
}

bool config_valid(const Config *c)
{
    if (!in_range(c->cells, 4, 24) || c->nmc > 1 ||
        (c->nmc && c->cells > 23) || !in_range(c->capacity_dah, 1, 10000) ||
        !in_range(c->empty_mv, 1500, c->full_mv) || c->empty_mv + 100 >= c->full_mv ||
        c->full_mv > (c->nmc ? 4300 : 3700) ||
        c->offset_ma < -5000 || c->offset_ma > 5000 ||
        !in_range(c->gain_ppm, 800000, 1200000)) return false;
    for (unsigned i = 0; i < LIMIT_COUNT; ++i) {
        const Limit *p = &c->sw[i];
        bool low=limit_low(i);
        if (!p->delay_ms || (low ? p->release<=p->trip || p->alarm1<p->alarm2 || p->alarm2<p->trip :
                                   p->release>=p->trip || p->alarm1>p->alarm2 || p->alarm2>p->trip))
            return false;
        if (i>=CHARGE_HOT && i<=MOS_HOT &&
            (p->alarm1>1650 || p->alarm2>1650 || p->trip>1650 || p->release>1650)) return false;
    }
    if (c->sw[UV].release >= c->sw[OV].release || c->sw[UV].trip < 1500 ||
        c->sw[OV].trip > (c->nmc ? 4300 : 3700) ||
        c->sw[PACK_UV].release >= c->sw[PACK_OV].release ||
        c->sw[PACK_OV].trip > c->cells * (c->nmc ? 430u : 370u) ||
        c->sw[PACK_UV].trip < c->cells * 150u) return false;
    if (c->sw[SOC_LOW].alarm1>100 || c->sw[SOC_LOW].release>100) return false;
    return in_range(c->hw_ov_mv, c->sw[OV].trip, 4595) &&
        in_range(c->hw_uv_mv, 1500, c->sw[UV].trip) &&
        in_range(c->hw_ov_ms, 200, 8000) && in_range(c->hw_uv_ms, 200, 8000) &&
        in_range(c->hw_d1_a10, 10, 2550) && in_range(c->hw_c1_a10, 10, 2550) &&
        in_range(c->hw_d2_a10, 160, 10240) && in_range(c->hw_c2_a10, 160, 10240) &&
        in_range(c->hw_sc_a10, 400, 25200) && in_range(c->hw_sc_us, 8, 1992) &&
        in_range(c->hw_d1_ms, 8, 2048) && in_range(c->hw_c1_ms, 8, 2048) &&
        in_range(c->hw_d2_ms, 4, 1024) && in_range(c->hw_c2_ms, 4, 1024) &&
        c->balance_on <= 1 && in_range(c->balance_mv, c->empty_mv, c->hw_ov_mv) &&
        c->balance_start_mv > c->balance_stop_mv && c->balance_start_mv < 1000;
}
