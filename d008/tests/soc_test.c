#include "../soc.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    Config c;
    config_defaults(&c,16,false);
    Soc p={0};
    soc_manual(&p,&c,80,0);
    p.state.remaining_uas+=1000000;
    p.soc_id=3; p.cycle_id=2;
    soc_set_cycles(&p,&c,7);
    SocSnapshot saved=soc_snapshot(&p,&c);
    c.capacity_dah=300;
    assert(soc_restore(&p,&c,&saved));
    assert(p.state.percent==80 && p.state.remaining_uas==86401500000LL);
    assert(p.discharged_uas==504000000000ULL && soc_cycles(&p,&c)==4);
    saved=soc_snapshot(&p,&c);
    assert(soc_restore(&p,&c,&saved) && p.state.remaining_uas==86401500000LL);
    soc_manual(&p,&c,60,0);
    assert(p.soc_id==3 && p.cycle_id==2 && soc_cycles(&p,&c)==4);
    soc_set_cycles(&p,&c,0);
    assert(soc_cycles(&p,&c)==0 && p.state.percent==60);
    const Sample s={.min_mv=3200,.max_mv=3200,.sum_mv=51200,.ma=1000};
    const Outputs o={0};
    soc_sample(&p,&c,&s,&o,true,true,0,0);
    Sample charging=s; charging.ma=-1000;
    soc_sample(&p,&c,&charging,&o,true,true,TICKS_PER_MS*200,200);
    assert(p.discharged_uas==50000);
    soc_sample(&p,&c,&s,&o,true,true,TICKS_PER_MS*400,400);
    assert(p.discharged_uas==100000);
    soc_sample(&p,&c,&s,&o,true,true,TICKS_PER_MS*2400,2400);
    assert(p.discharged_uas==100000);
    p.discharged_uas=UINT64_MAX-1;
    soc_sample(&p,&c,&s,&o,true,true,TICKS_PER_MS*2600,2600);
    assert(p.discharged_uas==UINT64_MAX && soc_cycles(&p,&c)==65534);
    c.capacity_dah=10000;
    soc_manual(&p,&c,100,2600);
    soc_resize(&p,10000,1);
    assert(p.state.remaining_uas==360000000);
    puts("PASS SOC: capacity ratio and restart, independent cycle/SOC, discharge area and bounds");
}
