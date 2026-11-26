#include "registers.h"
#include <string.h>
#if D008_COMM_TEST
#include "tests/hil.h"
#endif

#if D008_FLASH_TEST
#include "tests/flash_board.h"
#endif

static unsigned word(const uint8_t *p) {return ((unsigned)p[0]<<8)|p[1];}
static unsigned capacity_word(int64_t value) {return value>65535 ? 65535 : (unsigned)value;}
static unsigned report(unsigned index)
{
    const Sample *s=&device.sample;
    const Config *c=&device.config;
    bool ok=device.valid;
    unsigned hot=s->temp_x10[1], cold=s->temp_x10[2];
    if (cold>hot) {unsigned t=hot; hot=cold; cold=t;}
    if (index<32) return index>=c->cells || index>=24 ? NO_CELL : (ok?s->mv[index]:0);
    switch (index) {
    case 32: return ok?s->max_mv:0;
    case 33: return ok?s->min_mv:0;
    case 34: return ok?s->max_index+1:0;
    case 35: return ok?s->min_index+1:0;
    case 36: return ok?s->max_mv-s->min_mv:0;
    case 37: return ok?s->sum_mv/10:0;
    case 38: case 39: case 40: return ok?s->temp_x10[index-38]:0;
    case 46: case 48: return ok?hot:0;
    case 47: return ok?s->temp_x10[3]:0;
    case 49: return ok?cold:0;
    case 50: return ok && s->ma<0 ? -s->ma/100:0;
    case 51: return ok && s->ma>0 ? s->ma/100:0;
    case 52: return device.soc.state.percent;
    case 53: case 57: return UINT16_MAX;
    case 54: return capacity_word(device.soc.state.remaining_uas/36000000);
    case 55: case 56: return capacity_word(c->capacity_dah*10ul);
    case 60: {
        static const uint8_t bits[]={0,1,2,3,4,5,6,8,7,9,13,10};
        unsigned result=0;
        for (unsigned i=0;i<LIMIT_COUNT;++i)
            if (device.control.latched&(1u<<i)) result|=1u<<bits[i];
        return result;
    }
    case 61: return ok?(uint16_t)device.outputs.balance:0;
    case 62: return ok?device.outputs.balance>>16:0;
    default: return 0;
    }
}

bool register_read(const RtuSession *session,uint16_t r,uint16_t *value)
{
    const Config *c=&device.config;
    unsigned v=0;
#if D008_COMM_TEST
#if D008_FLASH_TEST
    if (r>=0x7c30 && r<=0x7c3e) return flash_test_read(r-0x7c30,value);
#endif
    if (r>=0x7c00 && r<0x7c1c) return hil_read(r-0x7c00,value);
#endif
    if (r<3) v=((unsigned)device_address[5-r*2]<<8)|device_address[4-r*2];
    else if (r>=0x0100 && r<0x010c) {
        static const uint8_t name[24]=D008_BLE_NAME;
        v=word(name+2*(r-0x0100));
    }
    else if (r>=0xd000 && r<=0xd03e) v=report(r-0xd000);
    else if (r>=0xd120 && r<=0xd12a) {
        const unsigned positions[]={0,0,37,0,52,48,49,47,32,33,36};
        v=r==0xd120?0x4253:r==0xd121?1:r==0xd123?report(50)-report(51):report(positions[r-0xd120]);
    } else if (r>=0xc002 && r<0xc032) {
        static const uint8_t hw[32]="HS-D008-24S100A-V1", fw[32]=
#if D008_COMM_TEST
            #if D008_FLASH_TEST
            "D008-FLASH-TEST";
#else
            "D008-COMM-V1";
#endif
#else
            "D008-CLEAN-V1";
#endif
        unsigned i=r-0xc002;
        v=word((i<16?c->sn:i<32?hw:fw)+2*(i%16));
    } else if (r>=0x2100 && r<=0x2140) {
        unsigned i=(r-0x2100)/5, field=(r-0x2100)%5;
        if (i<LIMIT_COUNT) v=field==2?c->sw[i].trip:field==3?c->sw[i].release:field==4?c->sw[i].delay_ms/10:0;
    } else if (r>=0x2500 && r<0x2563) {
        unsigned i=r-0x2500;
        if (i<35 || i>=64) {
            uint16_t words[35];
            hw_words(c,i>=64,words);
            v=i>=64 && !device.valid ? 0xffff:words[i%64];
        } else {
            switch (i) {
            case 35: v=0x7f; break;
            case 36: v=device.configured && device.valid; break;
            case 37: v=SHUNT_UOHM; break;
            case 38: v=c->cells; break;
            case 39: v=4; break;
            case 40: v=session->token!=0; break;
            case 41: v=device.valid?1:0; break;
            case 42: v=device.afe_fault?1:0; break;
            case 43: v=2; break;
            case 44: v=2000; break;
            case 45: v=8; break;
            }
        }
    } else if (r>=0x2e24 && r<=0x2e2b) {
        const int32_t values[]={c->offset_ma,c->gain_ppm,device.sample.raw_ma,device.sample.ma};
        v=(uint32_t)values[(r-0x2e24)/2]>>((r&1u)*16);
    } else if (r>=0x2e30 && r<=0x2e3f) v=word(c->sn+(r-0x2e30)*2);
    else if (r>=0x2e08 && r<=0x2e0f) {
        Config base;
        config_defaults(&base,D008_CELLS,D008_NMC);
        const unsigned defaults[]={base.capacity_dah,1,base.heat_start_x10,base.heat_stop_x10,
            base.balance_on,base.balance_mv,base.balance_start_mv,base.balance_stop_mv};
        v=defaults[r-0x2e08];
    } else switch (r) {
    case 0x1005: v=report(52); break;
    case 0x2318: v=c->capacity_dah; break;
    case 0x2319: v=report(57); break;
    case 0x2e00: v=0xd008; break;
    case 0x2e01: case 0x2e05: v=2; break;
    case 0x2e02: v=0x14df; break;
    case 0x2e03: v=session->result; break;
    case 0x2e04: v=session->sequence; break;
    case 0x2e06: v=session->sn_generation; break;
    case 0x2e07: v=1; break;
    case 0x2e18: v=c->full_mv; break;
    case 0x2e19: v=c->empty_mv; break;
    case 0x2e20: v=c->heat_on; break;
    case 0x2e21: v=c->heat_start_x10; break;
    case 0x2e22: v=c->heat_stop_x10; break;
    case 0x2e2c: v=device.valid; break;
    case 0x2e2d: v=device.soc.meter.last_tick; break;
    case 0x2e2e: v=device.soc.meter.last_tick>>16; break;
    case 0x2e70: v=c->balance_on; break;
    case 0x2e71: v=c->balance_mv; break;
    case 0x2e72: v=c->balance_start_mv; break;
    case 0x2e73: v=c->balance_stop_mv; break;
    case 0x2ea2: v=c->nmc+1; break;
    case 0x2f10: v=0x4d43; break;
    case 0x2f11: v=1; break;
    case 0x2f12: case 0x2f13: v=(device.allowed>>(r-0x2f12))&1; break;
    case 0x2f14: v=device.sample.fet; break;
    case 0x2f15: v=device.valid; break;
    case 0x2f16: v=device.valid?1:0; break;
    case 0x2f17: v=((device.outputs.drive&3)==3) | (((device.outputs.drive>>2)==3)<<1); break;
    case 0xd115: v=(device.valid?0x401:0)|(device.sample.fet<<2)|(device.outputs.heat<<8)|(device.outputs.balance?0x1000:0); break;
    case 0xd116: break;
    default: return false;
    }
    *value=v;
    return true;
}

unsigned register_write(RtuSession *s,uint16_t r,unsigned n,const uint8_t *p,uint32_t ms)
{
    Config c=device.config;
    unsigned v=word(p);
    if (r==0x1005 && n==1) return v>100?3:device_set_soc(v,ms)?0:4;
    if ((r==0x2f12 || r==0x2f13) && n==1) {
        unsigned bit=1u<<(r-0x2f12);
        return v>1?3:device_set_allowed((device.allowed & ~bit)|(v?bit:0),ms)?0:4;
    }
    if (r==0x2500 && n==35) return s->token?hw_write(p):2;
    if (r==0x2318 && n==1) c.capacity_dah=v;
    else if (r==0x2e18 && n==2) {c.full_mv=v; c.empty_mv=word(p+2);}
    else if (r==0x2e20 && n==3) {
        c.heat_on=v; c.heat_start_x10=word(p+2); c.heat_stop_x10=word(p+4);
    } else if (r==0x2e70 && n==4) {
        c.balance_on=v; c.balance_mv=word(p+2); c.balance_start_mv=word(p+4); c.balance_stop_mv=word(p+6);
    } else if (r==0x2e24 && n==4) {
        if (!s->token) return 2;
        c.offset_ma=(uint32_t)v|((uint32_t)word(p+2)<<16);
        c.gain_ppm=word(p+4)|((uint32_t)word(p+6)<<16);
    } else if (r==0x2e40 && n==1) {
        if (!s->token) return 2;
        if (!v) {
            if (!++s->sn_generation) ++s->sn_generation;
            s->sn_mask=0; s->sn_ms=ms; s->staging_sn=true; return 0;
        }
        if (!s->staging_sn || v!=s->sn_generation || s->sn_mask!=65535 || ms-s->sn_ms>60000) return 3;
        memcpy(c.sn,s->sn,sizeof(c.sn)); s->staging_sn=false;
    } else if (r>=0x2e50 && r<=0x2e5f) {
        if (!s->token) return 2;
        if (!s->staging_sn || ms-s->sn_ms>60000 || n>4 || r+n>0x2e60) return 3;
        for (unsigned i=0;i<n;++i) s->sn_mask|=1u<<(r+i-0x2e50);
        memcpy(s->sn+(r-0x2e50)*2,p,n*2); return 0;
    } else if (r>=0x2100 && r+n<=0x2141) {
        for (unsigned j=0;j<n;++j) {
            unsigned offset=r-0x2100+j, i=offset/5, field=offset%5;
            v=word(p+2*j);
            if (i==LIMIT_COUNT || field<2) {if (v) return 3;}
            else if (field==2) c.sw[i].trip=v;
            else if (field==3) c.sw[i].release=v;
            else {if (v>6553) return 3; c.sw[i].delay_ms=v*10;}
        }
    } else return 2;
    return !config_valid(&c)?3:device_set_config(&c)?0:4;
}
