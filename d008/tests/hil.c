/* 仅通信台架链接。独立 Soc 实例绝不交给保护、Flash 或功率输出。 */
#include "hil.h"
#include <string.h>
bool hil_awake;
uint32_t hil_pm_exits,hil_samples,hil_missed,hil_last_ms,hil_largest_ticks;
static RtuSession *owner;
static Soc simulation;
static Sample input;
static Outputs endpoints;
static bool current_valid,voltage_valid,ready;
static uint16_t token,generation,scale=1;
static uint8_t sequence,sample_state,dir,before;
static uint32_t activity_ms,applied,dt32k,delta_as10,previous_tick,v_tick,v_ms,v_fraction;
static bool started, switched;
static uint64_t transition_tick;
static uint32_t transition_interval;
static uint64_t virtual_ticks;
static int64_t charged;

static unsigned get(const uint8_t *p) {return (unsigned)p[0]<<8|p[1];}
static void u16be(uint8_t *p,unsigned v) {p[0]=v>>8; p[1]=v;}
static void u32be(uint8_t *p,uint32_t v) {u16be(p,v>>16);u16be(p+2,v);}
static unsigned estimate(void)
{
    int64_t full=(int64_t)device.config.capacity_dah*360000000;
    return full?(unsigned)((simulation.state.remaining_uas*100+full/2)/full):0;
}
static bool safe(void)
{
    return device.valid && !device.allowed && !device.sample.fet &&
        device.sample.ma>=-1000 && device.sample.ma<=1000;
}
void hil_sample(uint32_t tick,uint32_t ms)
{
    ++hil_samples; hil_last_ms=ms;
    if (!token || !ready) return;
    if (ms-activity_ms>=8000 || !safe()) {token=0; ready=false; return;}
    uint32_t dt=started?tick-previous_tick:0;
    if (switched) {
        transition_tick=virtual_ticks; transition_interval=dt; switched=false;
    }
    previous_tick=tick; started=true;
    uint64_t scaled=(uint64_t)dt*scale;
    int64_t initial=simulation.state.remaining_uas;
    before=simulation.state.percent;
    /* 真实缺口一次交给积分器拒绝；加速只细分正常调度区间。 */
    bool gap=dt>MAX_MEASURED_GAP;
    uint32_t steps=gap?1u:scale;
    for (unsigned i=0;i<steps;++i) {
        uint32_t elapsed=dt;
        v_tick+=elapsed; virtual_ticks+=elapsed;
        v_ms+=elapsed/TICKS_PER_MS; v_fraction+=elapsed%TICKS_PER_MS;
        if (v_fraction>=TICKS_PER_MS) {++v_ms;v_fraction-=TICKS_PER_MS;}
        soc_sample(&simulation,&device.config,&input,&endpoints,current_valid,voltage_valid,v_tick,v_ms);
    }
    int64_t amount=initial-simulation.state.remaining_uas;
    charged+=amount;
    dt32k=(uint32_t)(((gap?dt:scaled)*32768)/16000000);
    dir=amount>0?1:amount<0?2:0;
    delta_as10=(uint32_t)((amount<0?-amount:amount)/100000);
    sample_state=!current_valid?1:gap?4:2;
    ++applied;
}
unsigned hil_command(RtuSession *link,const uint8_t *p,unsigned n,uint8_t *out,uint32_t ms)
{
    unsigned cmd=p[2],length=4,error=0;
    out[2]=cmd;
    if (token && ms-activity_ms>=8000) {token=0;ready=false;}
    if (cmd==1) {
        if (n!=11 || memcmp(p+3,"SOCH",4) || p[7]>100 || p[8]) error=1;
        else if (!safe()) error=device.valid?4:5;
        else if (token) error=2;
        else {
            if (!++generation) ++generation;
            owner=link; token=generation; activity_ms=ms; sequence=0;
            ready=started=false; applied=0; virtual_ticks=0; charged=0;
            v_tick=UINT32_MAX-SAMPLE_TICKS; v_ms=v_fraction=0;
            memset(&simulation,0,sizeof(simulation)); memset(&input,0,sizeof(input));
            soc_manual(&simulation,&device.config,p[7],0);
            u16be(out+4,token); u16be(out+6,8); out[8]=1; out[9]=p[7]; length=10;
        }
    } else if (n<7 || !token || owner!=link || get(p+3)!=token) error=2;
    else {
        activity_ms=ms;
        if (cmd==3 && n==20) {
            int32_t ma=(int32_t)(((uint32_t)get(p+6)<<16)|get(p+8));
            uint32_t flags=((uint32_t)p[15]<<16)|get(p+16);
            unsigned lo=get(p+10), hi=get(p+12);
            if (ma < -3000000 || ma>3000000 || lo>hi || hi>6000) error=1;
            else {
                input.ma=ma; input.min_mv=lo; input.max_mv=hi;
                input.sum_mv=(lo+hi)*device.config.cells/2u;
                for (unsigned i=0;i<4;++i) input.temp_x10[i]=p[14]*10u;
                current_valid=(flags&1) && !(flags&((1u<<7)|(1u<<9)));
                voltage_valid=(flags&2) && !(flags&((1u<<5)|(1u<<6)|(1u<<10)));
                endpoints.full_trip=(flags&(1u<<11))!=0; endpoints.empty_trip=(flags&(1u<<12))!=0;
                sequence=p[5]; ready=true; switched=true; u16be(out+4,token); out[6]=sequence;
                u32be(out+7,applied); length=11;
            }
        } else if (cmd==2 && n==7) {u16be(out+4,token); u16be(out+6,8); length=8;}
        else if (cmd==4 && n==7) {
            memset(out+4,0,38); u16be(out+4,token); u16be(out+6,8); out[8]=sequence;
            out[9]=ready; u32be(out+10,applied); out[14]=estimate(); out[15]=simulation.state.percent;
            out[16]=sample_state; out[17]=dir; out[19]=before; out[20]=simulation.state.percent;
            out[21]=estimate(); u32be(out+22,dt32k); u32be(out+26,delta_as10);
            u32be(out+30,input.ma); u16be(out+34,input.min_mv); u16be(out+36,input.max_mv); length=42;
        } else if (cmd==5 && n==7) {ready=false; token=0; scale=1;}
        else error=1;
    }
    out[3]=error;
    unsigned crc=rtu_crc(out,length); out[length]=crc; out[length+1]=crc>>8;
    return length+2;
}
bool hil_read(unsigned i,uint16_t *v)
{
    const uint32_t pairs[]={hil_last_ms,hil_samples,hil_pm_exits,hil_missed,hil_largest_ticks,
        (uint32_t)(virtual_ticks>>32),(uint32_t)virtual_ticks,(uint32_t)((uint64_t)charged>>32),
        (uint32_t)charged,applied};
    if (i<20) {*v=pairs[i/2]>>(i%2?0:16);return true;}
    if (i==20) {*v=hil_awake;return true;}
    if (i==21) {*v=scale;return true;}
    if (i>=22 && i<=27) {
        uint32_t mark=i<24?(uint32_t)(transition_tick>>32):i<26?(uint32_t)transition_tick:transition_interval;
        *v=mark>>(i%2?0:16); return true;
    }
    return false;
}
unsigned hil_control(unsigned offset,unsigned value)
{
    if (offset==0 && value<=1) {hil_awake=value;return 0;}
    if (offset==1 && value>=1 && value<=100 && !token) {scale=value;return 0;}
    return 3;
}
