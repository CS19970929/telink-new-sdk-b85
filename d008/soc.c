#include "soc.h"

static int64_t capacity(const Config *c)
{
    return (int64_t)c->capacity_dah * 360000000;
}

void soc_manual(Soc *p, const Config *c, unsigned percent, uint32_t ms)
{
    if (percent > 100) return;
    p->state.remaining_uas = capacity(c)/100 * percent;
    p->state.percent = percent; p->initialized = true;
    p->meter = (Meter){0}; p->display_ms = ms;
    p->confirmed_full = false;
    p->near_full = p->near_empty = p->charging = p->discharging = false;
}

void soc_sample(Soc *p, const Config *c, const Sample *s, const Outputs *o,
                bool valid, bool voltage_valid, uint32_t tick, uint32_t ms)
{
    uint32_t missing;
    int64_t delta = meter_sample(&p->meter,tick,s->ma,valid,&missing);
    if (!valid || missing) {
        p->charging = p->discharging = p->near_full = p->near_empty = false;
        p->display_ms = ms;
    }
    if (!valid) return;
    unsigned average = s->sum_mv/c->cells;
    bool consistent = voltage_valid && s->max_mv-s->min_mv <= 100;
    if (!p->initialized) {
        if (!consistent || s->max_mv > 4500 || s->min_mv < 1500) return;
        /* 首估只做一次。平台区与带载时采用保守中值，端点另行确认。 */
        unsigned estimate = 50;
        if (s->ma >= -320 && s->ma <= 320) {
            if (average <= c->empty_mv+50u) estimate = 5;
            else if (average >= c->full_mv-30u) estimate = 95;
            else if (c->nmc)
                estimate = 5 + 90u*(average-c->empty_mv)/(c->full_mv-c->empty_mv);
        }
        soc_manual(p,c,estimate,ms);
        meter_sample(&p->meter,tick,s->ma,true,&missing);
        return;
    }
    if (s->ma < -320 || s->ma > 320) {
        p->charging = s->ma < 0; p->discharging = s->ma > 0; p->direction_ms = ms;
    } else if (ms-p->direction_ms > 5000) {
        p->charging = p->discharging = false;
    }
    const int64_t full = capacity(c);
    p->state.remaining_uas -= delta;
    if (p->state.remaining_uas > full) p->state.remaining_uas = full;
    if (p->state.remaining_uas < 0) p->state.remaining_uas = 0;
    bool full_zone = p->charging && consistent && average+30u >= c->full_mv &&
                     s->min_mv+60u >= c->full_mv;
    bool empty_zone = p->discharging && consistent && average <= c->empty_mv+50u &&
                      s->max_mv <= c->empty_mv+100u;
    if ((full_zone && !p->near_full) || (empty_zone && !p->near_empty)) p->edge_ms = ms;
    p->near_full = full_zone; p->near_empty = empty_zone;
    /* 硬件截断可令电流立即归零；最近方向、整组包络和保护沿共同提供证据。 */
    bool full_end = full_zone && ((o->full_trip && s->ma <= 320) ||
                                 (average >= c->full_mv && ms-p->edge_ms >= 2000));
    bool empty_end = empty_zone && ((o->empty_trip && s->ma < 320) ||
                                  (average <= c->empty_mv && ms-p->edge_ms >= 5000 &&
                                   s->ma < (int32_t)c->capacity_dah*20));
    if (!voltage_valid || s->ma > 320) p->confirmed_full=false;
    if (full_end) {p->state.remaining_uas=full; p->confirmed_full=true;}
    if (empty_end) p->state.remaining_uas = 0;
    unsigned target = (unsigned)((p->state.remaining_uas*100 + full/2)/full);
    if (!p->confirmed_full && target == 100 && p->state.percent < 100) target = 99;
    if (!empty_end && !target && p->state.percent) target = 1;
    if (empty_end && o->empty_trip) {p->state.percent = 0; p->display_ms = ms;}
    if (ms-p->display_ms >= 1000) {
        p->display_ms = ms;
        if (target > p->state.percent && p->charging) ++p->state.percent;
        if (target < p->state.percent && p->discharging) --p->state.percent;
        /* 截断后进入静置，只允许已确认的满端显示完成。 */
        if (s->ma >= -320 && s->ma <= 320 && p->confirmed_full && !p->charging &&
            p->state.percent < 100 && !p->discharging) ++p->state.percent;
    }
}
