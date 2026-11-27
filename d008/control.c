#include "control.h"

#define BIT_OF(i) (1u << (i))
enum { COV = 0x40, CUV = 0x20, OCD = 0x14, OCC = 0x0a, SCD = 1, IWT = 0x80 };
enum { REVERSE_MA = 320, LOAD_RELEASE_MS = 1000, CURRENT_ALARMS = OCD|OCC|SCD };

void control_gap(Control *p)
{
    p->pending = 0; p->load_seen = false; p->charging = false; p->heat = false;
    p->balance = 0; p->fuse_timing = false;
}

void control_acknowledged(Control *p, uint8_t ack, uint8_t alarms)
{
    /* 重配可能清掉 AFE 的易失告警。只在获得恢复证据并读回后释放锁存。 */
    p->current_faults = (p->current_faults & ~ack) | (alarms & CURRENT_ALARMS);
}

Outputs control_sample(Control *p, const Config *c, const Sample *s, uint32_t ms,
                       uint8_t allowed, bool current_qualified, bool load_removed, bool wire_fault)
{
    Outputs out = {0};
    uint16_t cold = s->temp_x10[1], hot = s->temp_x10[2];
    if (cold > hot) {uint16_t tmp = cold; cold = hot; hot = tmp;}
    int32_t value[LIMIT_COUNT] = {
        s->max_mv,s->min_mv,(int32_t)s->sum_mv/10,(int32_t)s->sum_mv/10,
        -s->ma/100,s->ma/100,hot,cold,hot,cold,s->temp_x10[3],s->max_mv-s->min_mv
    };
    p->current_faults |= s->alarms & CURRENT_ALARMS;
    uint8_t alarms = s->alarms | p->current_faults;
    bool reverse_charge = current_qualified && s->ma < -REVERSE_MA;
    bool reverse_discharge = current_qualified && s->ma > REVERSE_MA;
    if (load_removed && current_qualified && !(s->fet & DISCHARGE) &&
        !reverse_charge && !reverse_discharge) {
        if (!p->load_seen) {p->load_seen = true; p->load_since = ms;}
    } else p->load_seen = false;
    bool released = p->load_seen && ms-p->load_since >= LOAD_RELEASE_MS;
    bool current_fresh = current_qualified && s->current_fresh;
    for (unsigned i = 0; i < LIMIT_COUNT; ++i) {
        const Limit *limits = &c->sw[i];
        bool low = i == UV || i == PACK_UV || i == CHARGE_COLD || i == DISCHARGE_COLD;
        bool trip = low ? value[i] <= limits->trip : value[i] >= limits->trip;
        bool recover = low ? value[i] >= limits->release : value[i] <= limits->release;
        if (i == CHARGE_OC) recover &= current_fresh && reverse_discharge;
        if (i == DISCHARGE_OC) recover &= current_fresh && (released || reverse_charge);
        unsigned bit = BIT_OF(i);
        if (trip) {
            if (!(p->pending & bit)) {p->pending |= bit; p->since[i] = ms;}
            if (ms-p->since[i] >= limits->delay_ms) p->latched |= bit;
        } else {
            p->pending &= ~bit;
            if (recover) p->latched &= ~bit;
        }
    }
    if (s->max_mv <= c->sw[OV].release) out.ack |= COV;
    if (s->min_mv >= c->sw[UV].release) out.ack |= CUV;
    if (current_fresh && (reverse_charge || released)) out.ack |= OCD|SCD;
    if (current_fresh && reverse_discharge) out.ack |= OCC;
    out.ack &= alarms;
    out.full_trip = (alarms & COV) || (p->latched & BIT_OF(OV));
    out.empty_trip = (alarms & CUV) || (p->latched & BIT_OF(UV));

    const unsigned charge_faults = BIT_OF(OV)|BIT_OF(PACK_OV)|BIT_OF(CHARGE_OC)|
        BIT_OF(CHARGE_HOT)|BIT_OF(CHARGE_COLD);
    const unsigned discharge_faults = BIT_OF(UV)|BIT_OF(PACK_UV)|BIT_OF(DISCHARGE_OC)|
        BIT_OF(DISCHARGE_HOT)|BIT_OF(DISCHARGE_COLD);
    bool common = wire_fault || s->min_mv < 500 || s->max_mv > 4800 ||
        (p->latched & (BIT_OF(MOS_HOT)|BIT_OF(CELL_DELTA))) || (alarms & (IWT|SCD));
#if D008_FUSE_FITTED
    common |= !s->heater_ntc_valid;
#endif
    bool chg = (allowed & CHARGE) && !(p->latched & charge_faults) && !(alarms & (OCC|COV));
    bool dsg = (allowed & DISCHARGE) && !(p->latched & discharge_faults) && !(alarms & (OCD|CUV));

    /* 加热关充使电流归零时保留会话，放电、热故障及最长会话立即撤销。 */
    if (reverse_charge) p->charging = true;
    if (reverse_discharge || common || !(allowed & CHARGE) || (!reverse_charge && !p->heat))
        p->charging = false;
    if (!s->heater_ntc_valid || !c->heat_on || !p->charging || !dsg || (alarms & (OCC|COV)) ||
        (p->latched & (charge_faults & ~BIT_OF(CHARGE_COLD))) ||
        s->temp_x10[0] >= 1200 || cold >= c->heat_stop_x10 ||
        (p->heat && ms-p->heat_since >= 600000u)) {
        p->heat = false;
    } else if (!p->heat && cold <= c->heat_start_x10) {
        p->heat = true; p->heat_since = ms;
    }
    if (s->heater_ntc_valid && !p->heat && s->temp_x10[0] >= 1350) {
        if (!p->fuse_timing) {p->fuse_timing = true; p->fuse_since = ms;}
        if (ms-p->fuse_since >= 10000) p->fuse_trip = true;
    } else p->fuse_timing = false;
    out.fuse = p->fuse_trip;
    common |= out.fuse;
    if (p->heat) chg = false;
    if (!common && (chg || dsg)) {
        /* 单侧保护可用 AFE 自主续流。上位机单关须有有效反向电流才放行。 */
        bool auto_charge = current_qualified && !p->heat && (allowed & CHARGE);
        bool auto_discharge = current_qualified && (allowed & DISCHARGE);
        unsigned cm = chg || (!p->heat && reverse_discharge) ? 3u : (auto_charge ? 2u : 0u);
        unsigned dm = dsg || reverse_charge ? 3u : (auto_discharge ? 2u : 0u);
        out.drive = (dm << 2) | cm;
        out.reverse_modes = ((cm && !chg) ? 0x03u : 0u) | ((dm && !dsg) ? 0x0cu : 0u);
        out.heat = p->heat && !(s->fet & CHARGE);
    }

    if (hot >= 1000 || s->temp_x10[3] >= 1000) p->balance_hot = true;
    if (hot <= 900 && s->temp_x10[3] <= 900) p->balance_hot = false;
    if (c->balance_on && reverse_charge && chg && !common && !p->balance_hot) {
        /* 每次选最高的四个合格通道，已开启的通道允许压差回差。 */
        for (unsigned slot = 0; slot < 4; ++slot) {
            unsigned best = c->cells;
            for (unsigned i = 0; i < c->cells; ++i) {
                uint32_t bit = 1ul << i;
                unsigned delta = s->mv[i]-s->min_mv;
                bool qualified = p->balance & bit ? delta > c->balance_stop_mv : delta >= c->balance_start_mv;
                if (!(out.balance & bit) && qualified && s->mv[i] >= c->balance_mv &&
                    (best == c->cells || s->mv[i] > s->mv[best])) best = i;
            }
            if (best == c->cells) break;
            out.balance |= 1ul << best;
        }
    }
    p->balance = out.balance;
    return out;
}
