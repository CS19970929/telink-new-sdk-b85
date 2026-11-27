#include "registers.h"
#include <string.h>

uint16_t rtu_crc(const uint8_t *p,unsigned n)
{
    uint16_t crc=65535;
    while (n--) {
        crc^=*p++;
        for (unsigned b=0;b<8;++b) crc=(crc>>1)^((crc&1)?0xa001:0);
    }
    return crc;
}
static uint16_t be(const uint8_t *p) {return ((unsigned)p[0]<<8)|p[1];}
static void put(uint8_t *p,unsigned v) {p[0]=v>>8; p[1]=v;}
static unsigned finish(uint8_t *p,unsigned n)
{
    unsigned crc=rtu_crc(p,n);
    p[n]=crc; p[n+1]=crc>>8;
    return n+2;
}

void hw_words(const Config *c,bool effective,uint16_t out[35])
{
    const uint16_t words[35]={1,0x1124,c->hw_ov_mv,c->hw_ov_ms,c->sw[OV].release,0,
        c->hw_uv_mv,c->hw_uv_ms,c->sw[UV].release,0,c->hw_d1_a10,c->hw_d1_ms,
        c->hw_d2_a10,c->hw_d2_ms,0,1000,c->hw_c1_a10,c->hw_c1_ms,c->hw_c2_a10,c->hw_c2_ms,
        0,0,c->hw_sc_a10,c->hw_sc_us,1000,0,0,0,0,0,0,0,0,0,0x7f};
    memcpy(out,words,sizeof(words));
    if (effective) {
        for (unsigned i=3;i<=7;i+=4)
            out[i]=out[i]<1000?200+(out[i]-200)/100*100:out[i]/1000*1000;
        out[10]=out[10]/10*10; out[16]=out[16]/10*10;
        out[11]=out[11]/8*8; out[17]=out[17]/8*8;
        out[12]=out[12]/160*160; out[18]=out[18]/160*160;
        out[13]=out[13]/4*4; out[19]=out[19]/4*4;
        out[22]=out[22]/400*400; out[23]=((uint32_t)out[23]*100/781)*781/100;
    }
}
unsigned hw_write(const uint8_t *p)
{
    Config c=device.config;
    uint16_t previous[35];
    hw_words(&c,false,previous);
    for (unsigned i=0;i<35;++i) {
        unsigned v=be(p+2*i);
        switch (i) {
        case 2: c.hw_ov_mv=v; break;
        case 3: c.hw_ov_ms=v; break;
        case 6: c.hw_uv_mv=v; break;
        case 7: c.hw_uv_ms=v; break;
        case 10: c.hw_d1_a10=v; break;
        case 11: c.hw_d1_ms=v; break;
        case 12: c.hw_d2_a10=v; break;
        case 13: c.hw_d2_ms=v; break;
        case 16: c.hw_c1_a10=v; break;
        case 17: c.hw_c1_ms=v; break;
        case 18: c.hw_c2_a10=v; break;
        case 19: c.hw_c2_ms=v; break;
        case 22: c.hw_sc_a10=v; break;
        case 23: c.hw_sc_us=v; break;
        default: if (v!=previous[i]) return 3;
        }
    }
    return !config_valid(&c)?3:device_set_config(&c)?0:4;
}

static unsigned maintenance(RtuSession *s,const uint8_t *p,unsigned n,uint8_t *out,uint32_t ms)
{
    unsigned cmd=p[2], length=4;
    out[2]=cmd; out[3]=0;
    if (cmd==1) {
        if (n!=9 || memcmp(p+3,"AFEH",4)) out[3]=2;
        else {
            if (!++s->generation) ++s->generation;
            s->token=s->generation; s->opened_ms=ms; s->received=0; s->staging_sn=false;
            put(out+4,s->token); put(out+6,60); out[8]=2; put(out+9,0x1124); length=11;
        }
    } else if (n<7 || (cmd!=5 && n!=7)) out[3]=1;
    else if (!s->token || be(p+3)!=s->token) out[3]=2;
    else {
        s->opened_ms=ms;
        if (cmd==2 || cmd==4) {put(out+4,s->token); put(out+6,60); length=8;}
        else if (cmd==3) {s->token=0; s->received=0; s->staging_sn=false;}
        else if (cmd==5) {
            if (n<10 || !p[6] || p[6]>11 || n!=9u+p[6] || p[5]!=s->received ||
                p[6]+s->received>sizeof(s->frame)) out[3]=1;
            else {
                memcpy(s->frame+s->received,p+7,p[6]); s->received+=p[6];
                s->fragment_ms=ms; put(out+4,s->token); out[6]=s->received; length=7;
            }
        } else if (cmd==6) {
            const uint8_t header[]={1,0x10,0x25,0,0,35,70};
            if (s->received!=sizeof(s->frame) || memcmp(s->frame,header,sizeof(header)) ||
                rtu_crc(s->frame,sizeof(s->frame))) out[3]=1;
            else if (hw_write(s->frame+7)) out[3]=4;
            s->received=0;
        } else out[3]=3;
    }
    if (out[3]) s->received=0;
    return finish(out,length);
}

/* 调用者为每个物理链路提供独立会话及 256 字节应答空间。 */
unsigned rtu_channel(RtuSession *s,const uint8_t *p,unsigned n,uint8_t *out,uint32_t ms)
{
    if (n<4 || n>256 || p[0]>1 || rtu_crc(p,n)) return 0;
    if (s->token && ms-s->opened_ms>=60000) {s->token=0; s->received=0; s->staging_sn=false;}
    if (s->received && ms-s->fragment_ms>5000) s->received=0;
    unsigned op=p[1], error=1, length=0;
    out[0]=p[0]; out[1]=op;
    if (op==0x7f && p[0]) {memcpy(out,p,n); return n;}
    if (op==0x42) return p[0] && n>=5?maintenance(s,p,n,out,ms):0;
    if (n>=8) {
        unsigned reg=be(p+2), count=op==6?1:be(p+4);
        if (op==3 && p[0]) {
            error=3;
            if (n==8 && count && count<=125 && reg+count<=65536u) {
                out[2]=2*count; length=3+2*count; error=0;
                for (unsigned i=0;i<count;++i) {
                    uint16_t value=0;
                    if (reg==0xc008 && count<=100) {
                        if (i<store_events() && !store_event_at(i,&value)) {error=4; break;}
                    } else if (!register_read(s,reg+i,&value)) {error=2; break;}
                    put(out+3+2*i,value);
                }
            }
        } else if (op==6 || op==16) {
            error=3;
            if ((op==6 && n==8) || (op==16 && count && count<=123 && reg+count<=65536u &&
                n==9+count*2 && p[6]==count*2)) {
                /* 广播无会话，禁止修改 MOS 或借用本地维护权限。 */
                if (!p[0] && (s->token || (reg<0x2f18 && reg+count>0x2f10))) return 0;
                error=register_write(s,reg,count,p+(op==6?4:7),ms);
                ++s->sequence; s->result=error;
                memcpy(out,p,6); length=6;
            }
        }
    }
    if (!p[0]) return 0;
    if (error) {out[1]=op|0x80; out[2]=error; length=3;}
    return finish(out,length);
}
