#include "sif.h"
#include <string.h>

static void le(uint8_t *p,unsigned value) {p[0]=value; p[1]=value>>8;}
static uint8_t fault_code(const Device *d)
{
    unsigned f=d->control.latched, a=d->sample.alarms;
    if (f&(1u<<DISCHARGE_COLD)) return 9;
    if ((f&(1u<<CHARGE_OC)) || (a&0x0a)) return 8;
    if ((f&((1u<<OV)|(1u<<PACK_OV))) || (a&0x40)) return 7;
    if ((f&((1u<<UV)|(1u<<PACK_UV))) || (a&0x20)) return 6;
    if (f&(1u<<DISCHARGE_HOT)) return 5;
    if (f&(1u<<CHARGE_HOT)) return 4;
    if (f&(1u<<CHARGE_COLD)) return 3;
    if ((f&(1u<<DISCHARGE_OC)) || (a&0x15)) return 1;
    return 0;
}
unsigned sif_frame(const Device *d,bool lishen,uint8_t type,uint8_t *p)
{
    const Sample *s=&d->sample;
    bool ok=d->valid;
    bool soc_ok=ok && d->soc.initialized && d->soc.state.percent<=100;
    unsigned soc=soc_ok?d->soc.state.percent*2:255;
    unsigned hot=s->temp_x10[1],cold=s->temp_x10[2];
    if (cold>hot) {unsigned t=cold; cold=hot; hot=t;}
    int32_t a10=s->ma/100;
    unsigned current=ok && a10>=-5000 && a10<=5000 ? 5000+a10:65535;
    unsigned status=current==65535 || !a10?255:a10<0?1:0;
    unsigned n=0;
    memset(p,255,64);
    if (lishen) {
        unsigned v=(s->sum_mv+500)/1000;
        if (!soc_ok || !v || v>255 || hot>1650) return 0;
        p[0]=0; p[1]=soc/2; p[2]=v; p[3]=(uint8_t)((int)(hot+5)/10-40); p[4]=0;
        n=6;
    } else if (type==1) {
        p[0]=1; p[1]=0x13; p[4]=d->config.nmc?3:1; le(p+7,d->config.capacity_dah);
        p[9]=soc; le(p+10,ok?s->sum_mv/100:65535); le(p+12,current);
        p[14]=ok?hot/10:255; p[15]=ok?cold/10:255; p[16]=ok?s->temp_x10[3]/10:255;
        p[17]=fault_code(d); p[18]=status; n=20;
    } else if (type==0x3a) {
        p[0]=type; p[1]=0x13; p[2]=29; p[3]=soc;
        le(p+4,ok?s->sum_mv/100:65535); le(p+6,current);
        p[8]=ok?hot/10:255; p[9]=ok?cold/10:255; p[10]=ok?s->temp_x10[3]/10:255;
        p[11]=fault_code(d); p[12]=status; le(p+16,ok?s->max_mv:65535); le(p+18,ok?s->min_mv:65535);
        p[20]=ok?s->max_index+1:255; p[21]=ok?s->min_index+1:255; p[27]=0; n=33;
    } else if (type==0x3b) {
        if (d->config.cells<4 || d->config.cells>24) return 0;
        p[0]=type; p[1]=0x13; p[2]=2*d->config.cells;
        for (unsigned i=0;i<d->config.cells;++i) le(p+3+2*i,ok && s->mv[i]!=NO_CELL?s->mv[i]:65535);
        n=4+p[2];
    } else if (type==0x3c) {
        unsigned size=0; bool visible=false;
        while (size<sizeof(d->config.sn) && d->config.sn[size]) {
            uint8_t ch=d->config.sn[size];
            if (ch<32 || ch>126) {visible=false; break;}
            if (ch!=32) visible=true;
            p[3+size++]=ch;
        }
        if (!visible) {size=1; p[3]=255;}
        p[0]=type; p[1]=0x13; p[2]=size; n=4+size;
    } else return 0;
    unsigned sum=0;
    for (unsigned i=0;i<n-1;++i) sum+=p[i];
    p[n-1]=sum;
    return n;
}
