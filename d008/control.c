#include "control.h"

#define BIT_OF(i) (1u << (i))
enum { COV = 0x40, CUV = 0x20, OCD = 0x14, OCC = 0x0a, SCD = 1, IWT = 0x80 };
enum { REVERSE_MA = 320, LOAD_RELEASE_MS = 1000, CURRENT_ALARMS = OCD|OCC|SCD };

void control_gap(Control *p)
{
    p->pending[0]=p->pending[1]=p->pending[2]=0;
    p->alarm[0]=p->alarm[1]=0;
    p->load_seen=false;
    p->balance = 0;
}

void control_acknowledged(Control *p, uint8_t ack, uint8_t alarms)
{
    /* 重配可能清掉 AFE 的易失告警。只在获得恢复证据并读回后释放锁存。 */
    p->current_faults = (p->current_faults & ~ack) | (alarms & CURRENT_ALARMS);
}

Outputs control_sample(Control *p, const Config *c, const Sample *s, uint32_t ms,
                       uint8_t allowed, bool current_qualified, bool load_removed)
{
    Outputs out = {0};
    uint16_t cold = s->temp_x10[NTC_BAT1], hot = s->temp_x10[NTC_BAT2];
    if (cold > hot) {uint16_t tmp = cold; cold = hot; hot = tmp;}
    int32_t value[SOC_LOW] = {
        s->max_mv,s->min_mv,(int32_t)s->sum_mv/10,(int32_t)s->sum_mv/10,
        -s->ma/100,s->ma/100,hot,cold,hot,cold,s->temp_x10[NTC_MOS],s->max_mv-s->min_mv
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
    for (unsigned i = 0; i < SOC_LOW; ++i) {
        const Limit *limits = &c->sw[i];
        bool low=limit_low(i);
        const uint16_t threshold[]={limits->alarm1,limits->alarm2,limits->trip};
        const unsigned bit=BIT_OF(i);
        for (unsigned level=0;level<3;++level) {
            uint16_t *flags=level<2?&p->alarm[level]:&p->latched;
            bool trip=low?value[i]<=threshold[level]:value[i]>=threshold[level];
            if (trip) {
                if (!(p->pending[level]&bit)) {
                    p->pending[level]|=bit;
                    p->since[i][level]=ms;
                }
                if (ms-p->since[i][level]>=limits->delay_ms) *flags|=bit;
            } else {
                p->pending[level]&=~bit;
                bool recover=low?value[i]>=limits->release:value[i]<=limits->release;
                if (i==CHARGE_OC) recover &= current_fresh && reverse_discharge;
                if (i==DISCHARGE_OC) recover &= current_fresh && (released || reverse_charge);
                if (level<2 || recover) *flags&=~bit;
            }
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
    bool common = s->min_mv < 500 || s->max_mv > 4800 ||
        (p->latched & (BIT_OF(MOS_HOT)|BIT_OF(CELL_DELTA))) || (alarms & (IWT|SCD));
    bool chg = (allowed & CHARGE) && !(p->latched & charge_faults) && !(alarms & (OCC|COV));
    bool dsg = (allowed & DISCHARGE) && !(p->latched & discharge_faults) && !(alarms & (OCD|CUV));

    if (!common && (chg || dsg)) {
        /* 单侧保护可用 AFE 自主续流。上位机单关须有有效反向电流才放行。 */
        bool auto_charge = current_qualified && (allowed & CHARGE);
        bool auto_discharge = current_qualified && (allowed & DISCHARGE);
        unsigned cm = chg || reverse_discharge ? 3u : (auto_charge ? 2u : 0u);
        unsigned dm = dsg || reverse_charge ? 3u : (auto_discharge ? 2u : 0u);
        out.drive = (dm << 2) | cm;
        out.reverse_modes = ((cm && !chg) ? 0x03u : 0u) | ((dm && !dsg) ? 0x0cu : 0u);
    }

    if (hot >= 1000 || s->temp_x10[NTC_MOS] >= 1000) p->balance_hot = true;
    if (hot <= 900 && s->temp_x10[NTC_MOS] <= 900) p->balance_hot = false;
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

void control_soc(Control *p,const Config *c,unsigned percent,uint32_t ms)
{
    const Limit *l=&c->sw[SOC_LOW];
    const uint16_t trip[]={l->alarm1,l->alarm2,l->trip};
    const unsigned bit=BIT_OF(SOC_LOW);
    for (unsigned level=0;level<3;++level) {
        uint16_t *flags=level<2?&p->alarm[level]:&p->latched;
        bool active=(*flags&bit)!=0;
        unsigned release=l->release>trip[level]?l->release:trip[level]+1u;
        if (release>100) release=100;
        if (!trip[level]) *flags&=~bit;
        bool change=trip[level] && (active?percent>=release:percent<=trip[level]);
        if (!change) p->pending[level]&=~bit;
        else {
            if (!(p->pending[level]&bit)) {
                p->pending[level]|=bit;
                p->since[SOC_LOW][level]=ms;
            }
            if (ms-p->since[SOC_LOW][level]>=l->delay_ms) {
                *flags^=bit;
                p->pending[level]&=~bit;
            }
        }
    }
}
