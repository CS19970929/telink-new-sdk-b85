#include "soc.h"
#include <stddef.h>

static int64_t capacity(const Config *c)
{
    return (int64_t)c->capacity_dah * 360000000;
}

typedef char SocLayout[(sizeof(SocSnapshot)==32 && offsetof(SocSnapshot,capacity_dah)==24) ? 1 : -1];

SocSnapshot soc_snapshot(const Soc *p,const Config *c)
{
    return (SocSnapshot){p->state.remaining_uas,p->discharged_uas,p->soc_id,p->cycle_id,
                         c->capacity_dah,p->state.percent,p->factory_id};
}

void soc_resize(Soc *p,unsigned previous,unsigned next)
{
    if (p->initialized && previous && previous!=next)
        p->state.remaining_uas=(p->state.remaining_uas*next+previous/2)/previous;
}

bool soc_restore(Soc *p,const Config *c,const SocSnapshot *s)
{
    if (!s->capacity_dah || s->capacity_dah>10000 || s->percent>100 ||
        s->remaining_uas<0 || s->remaining_uas>(int64_t)s->capacity_dah*360000000) return false;
    *p=(Soc){0};
    p->initialized=true;
    p->state.remaining_uas=s->remaining_uas;
    p->state.percent=s->percent;
    p->discharged_uas=s->discharged_uas;
    p->soc_id=s->soc_id; p->cycle_id=s->cycle_id; p->factory_id=s->factory_id;
    soc_resize(p,s->capacity_dah,c->capacity_dah);
    return true;
}

uint16_t soc_cycles(const Soc *p,const Config *c)
{
    if (!p->initialized || !c->capacity_dah) return UINT16_MAX;
    uint64_t cycles=p->discharged_uas/(uint64_t)capacity(c);
    return cycles>=UINT16_MAX?UINT16_MAX-1:(uint16_t)cycles;
}

void soc_set_cycles(Soc *p,const Config *c,unsigned cycles)
{
    p->discharged_uas=(uint64_t)capacity(c)*cycles;
    p->discharge_remainder=0;
}

unsigned soc_estimate(const Config *c,const Sample *s)
{
    unsigned average=s->sum_mv/c->cells;
    if (s->ma < -320 || s->ma > 320) return 50;
    if (average<=c->empty_mv+50u) return 5;
    if (average>=c->full_mv-30u) return 95;
    if (c->nmc) return 5+90u*(average-c->empty_mv)/(c->full_mv-c->empty_mv);
    return 50;
}

static void count_discharge(Soc *p,int32_t ma,uint32_t tick,bool valid)
{
    uint32_t dt=tick-p->meter.last_tick;
    if (!p->meter.started || !p->meter.last_valid || !valid || dt>MAX_MEASURED_GAP) return;
    int32_t previous=p->meter.last_ma;
    if (previous<=0 && ma<=0) return;
    uint64_t area;
    if (previous>=0 && ma>=0) area=((uint64_t)ma+(uint32_t)previous)*dt;
    else {
        /* 线性变化过零时只累计放电三角形，先除后乘以防溢出。 */
        uint32_t positive=(uint32_t)(previous>0?previous:ma);
        uint64_t span=(uint64_t)((int64_t)positive-(previous>0?ma:previous));
        uint64_t scaled=(uint64_t)positive*dt;
        uint64_t ticks=scaled/span;
        area=ticks*positive+(scaled-ticks*span)*positive/span;
    }
    area+=p->discharge_remainder;
    uint64_t amount=area/(2u*TICKS_PER_MS);
    p->discharge_remainder=area-amount*(2u*TICKS_PER_MS);
    if (amount>UINT64_MAX-p->discharged_uas) p->discharged_uas=UINT64_MAX;
    else p->discharged_uas+=amount;
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
    count_discharge(p,s->ma,tick,valid);
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
        soc_manual(p,c,soc_estimate(c,s),ms);
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
