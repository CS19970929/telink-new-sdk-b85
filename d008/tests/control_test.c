#include "../control.h"
#include <assert.h>
#include <stdio.h>

static Sample healthy_sample(void)
{
    Sample s={0};
    s.sum_mv=16u*3200u;
    s.min_mv=s.max_mv=3200;
    for (unsigned i=0;i<16;++i) s.mv[i]=3200;
    for (unsigned i=0;i<4;++i) s.temp_x10[i]=650;
    s.current_fresh=true;
    s.heater_ntc_valid=true;
    return s;
}

static void remote_permissions(const Config *c)
{
    Control state={0};
    Sample s=healthy_sample();
    assert(control_sample(&state,c,&s,0,3,true,false,false).drive==0x0f);
    assert(control_sample(&state,c,&s,200,DISCHARGE,true,false,false).drive==0x0c);
    s.ma=320;
    assert(control_sample(&state,c,&s,400,DISCHARGE,true,false,false).drive==0x0c);
    s.ma=321;
    Outputs out=control_sample(&state,c,&s,600,DISCHARGE,true,false,false);
    assert(out.drive==0x0f && out.reverse_modes==0x03);
    assert(control_sample(&state,c,&s,800,DISCHARGE,false,false,false).drive==0x0c);
    assert(control_sample(&state,c,&s,1000,DISCHARGE,true,false,true).drive==0);
    assert(control_sample(&state,c,&s,1200,0,true,false,false).drive==0);
    s.ma=-1000;
    out=control_sample(&state,c,&s,1400,CHARGE,true,false,false);
    assert(out.drive==0x0f && out.reverse_modes==0x0c);
    s.ma=0;
    assert(control_sample(&state,c,&s,1600,CHARGE,true,false,false).drive==0x03);
}

static void software_protection(const Config *c)
{
    Control state={0};
    Sample s=healthy_sample();
    s.max_mv=3650;
    assert(control_sample(&state,c,&s,0,3,true,false,false).drive==0x0f);
    assert(control_sample(&state,c,&s,1000,3,true,false,false).drive==0x0e);
    s.ma=1000;
    Outputs out=control_sample(&state,c,&s,1200,3,true,false,false);
    assert(out.full_trip && out.drive==0x0f && out.reverse_modes==0x03);
    out=control_sample(&state,c,&s,1400,3,false,false,false);
    assert(out.full_trip && out.drive==0x0c);
    s.max_mv=3550;
    assert(control_sample(&state,c,&s,1600,3,true,false,false).drive==0x0f);
}

static void short_circuit_recovery(const Config *c)
{
    Control state={0};
    Sample s=healthy_sample();
    s.alarms=1;
    assert(control_sample(&state,c,&s,0,3,true,false,false).drive==0);
    control_gap(&state);
    s=healthy_sample();  /* AFE 重配置后告警寄存器可为 0。 */
    assert(control_sample(&state,c,&s,200,3,true,false,false).drive==0);
    s.ma=-1000;
    assert(control_sample(&state,c,&s,400,3,false,false,false).ack==0);
    s.current_fresh=false;
    assert(control_sample(&state,c,&s,600,3,true,false,false).ack==0);
    s.current_fresh=true;
    Outputs out=control_sample(&state,c,&s,800,3,true,false,false);
    assert(out.drive==0 && out.ack==1);
    control_acknowledged(&state,out.ack,1);  /* 已发清除但 AFE 仍报故障。 */
    s.ma=0;
    assert(control_sample(&state,c,&s,1000,3,true,true,false).ack==0);
    assert(control_sample(&state,c,&s,1800,3,true,true,false).ack==0);
    out=control_sample(&state,c,&s,2000,3,true,true,false);
    assert(out.drive==0 && out.ack==1);
    control_acknowledged(&state,out.ack,0);
    assert(control_sample(&state,c,&s,2200,3,true,false,false).drive==0x0f);
}

static void heater_sensor_loss(const Config *c)
{
    Control state={0};
    Sample s=healthy_sample();
    s.temp_x10[1]=s.temp_x10[2]=350;
    s.ma=-1000;
    assert(control_sample(&state,c,&s,0,3,true,false,false).heat);
    s.heater_ntc_valid=false;
    s.temp_x10[0]=1350;
    Outputs out=control_sample(&state,c,&s,200,3,true,false,false);
    assert(!out.heat && !out.fuse && !state.fuse_timing);

    s.temp_x10[1]=s.temp_x10[2]=650;
    s.ma=0;
    out=control_sample(&state,c,&s,11000,3,true,false,false);
    assert(!out.heat && !out.fuse);
#if D008_FUSE_FITTED
    assert(out.drive==0 && !out.balance);
#else
    assert(out.drive==0x0f);
#endif
}

int main(void)
{
    Config config;
    config_defaults(&config,16,false);
    assert(config_valid(&config));
    remote_permissions(&config);
    software_protection(&config);
    short_circuit_recovery(&config);
    heater_sensor_loss(&config);
    puts("PASS protection: normal drive, explicit off, reverse expiry, OV and SCD readback");
    return 0;
}
