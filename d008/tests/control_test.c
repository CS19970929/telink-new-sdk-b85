#include "../control.h"
#include <assert.h>
#include <stdio.h>

static Sample healthy_sample(void)
{
    Sample s={0};
    s.sum_mv=16u*3200u;
    s.min_mv=s.max_mv=3200;
    for (unsigned i=0;i<16;++i) s.mv[i]=3200;
    for (unsigned i=0;i<NTC_COUNT;++i) s.temp_x10[i]=650;
    s.current_fresh=true;
    return s;
}

static void remote_permissions(const Config *c)
{
    Control state={0};
    Sample s=healthy_sample();
    assert(control_sample(&state,c,&s,0,3,true,false).drive==0x0f);
    assert(control_sample(&state,c,&s,200,DISCHARGE,true,false).drive==0x0c);
    s.ma=320;
    assert(control_sample(&state,c,&s,400,DISCHARGE,true,false).drive==0x0c);
    s.ma=321;
    Outputs out=control_sample(&state,c,&s,600,DISCHARGE,true,false);
    assert(out.drive==0x0f && out.reverse_modes==0x03);
    assert(control_sample(&state,c,&s,800,DISCHARGE,false,false).drive==0x0c);
    s.min_mv=499;
    assert(control_sample(&state,c,&s,1000,DISCHARGE,true,false).drive==0);
    s.min_mv=3200;
    assert(control_sample(&state,c,&s,1200,0,true,false).drive==0);
    s.ma=-1000;
    out=control_sample(&state,c,&s,1400,CHARGE,true,false);
    assert(out.drive==0x0f && out.reverse_modes==0x0c);
    s.ma=0;
    assert(control_sample(&state,c,&s,1600,CHARGE,true,false).drive==0x03);
}

static void software_protection(const Config *c)
{
    Control state={0};
    Sample s=healthy_sample();
    s.max_mv=3650;
    assert(control_sample(&state,c,&s,0,3,true,false).drive==0x0f);
    assert(control_sample(&state,c,&s,1000,3,true,false).drive==0x0e);
    s.ma=1000;
    Outputs out=control_sample(&state,c,&s,1200,3,true,false);
    assert(out.full_trip && out.drive==0x0f && out.reverse_modes==0x03);
    out=control_sample(&state,c,&s,1400,3,false,false);
    assert(out.full_trip && out.drive==0x0c);
    s.max_mv=3550;
    assert(control_sample(&state,c,&s,1600,3,true,false).drive==0x0f);
}

static void short_circuit_recovery(const Config *c)
{
    Control state={0};
    Sample s=healthy_sample();
    s.alarms=1;
    assert(control_sample(&state,c,&s,0,3,true,false).drive==0);
    control_gap(&state);
    s=healthy_sample();  /* AFE 重配置后告警寄存器可为 0。 */
    assert(control_sample(&state,c,&s,200,3,true,false).drive==0);
    s.ma=-1000;
    assert(control_sample(&state,c,&s,400,3,false,false).ack==0);
    s.current_fresh=false;
    assert(control_sample(&state,c,&s,600,3,true,false).ack==0);
    s.current_fresh=true;
    Outputs out=control_sample(&state,c,&s,800,3,true,false);
    assert(out.drive==0 && out.ack==1);
    control_acknowledged(&state,out.ack,1);  /* 已发清除但 AFE 仍报故障。 */
    s.ma=0;
    assert(control_sample(&state,c,&s,1000,3,true,true).ack==0);
    assert(control_sample(&state,c,&s,1800,3,true,true).ack==0);
    out=control_sample(&state,c,&s,2000,3,true,true);
    assert(out.drive==0 && out.ack==1);
    control_acknowledged(&state,out.ack,0);
    assert(control_sample(&state,c,&s,2200,3,true,false).drive==0x0f);

    /* 负载仍在时，零电流和等待超过解除时间也不能清除短路。 */
    s.alarms=1;
    for (uint32_t ms=2400;ms<=5600;ms+=200) {
        out=control_sample(&state,c,&s,ms,3,true,false);
        assert(out.drive==0 && out.ack==0);
    }
}

int main(void)
{
    Config config;
    config_defaults(&config,16,false);
    assert(config_valid(&config));
    remote_permissions(&config);
    software_protection(&config);
    short_circuit_recovery(&config);
    puts("PASS protection: normal drive, explicit off, reverse expiry, OV and SCD readback");
    return 0;
}
