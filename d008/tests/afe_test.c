#include "../afe.h"
#include "../dvc_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t status, alarm, clear_mask;
static bool alarm_read_ok=true;
static uint16_t therm[]={0x4650,0x2710,0x2710,0x2710,0x2710};
static uint8_t failed_block=0xff;

bool dvc_read(uint8_t reg, uint8_t *data, uint8_t length)
{
    if (reg==failed_block) return false;
    memset(data,0,length);
    if (reg==0 && length==7) {
        data[0]=alarm;
        data[1]=status;
    } else if (reg==0 && length==1) {
        data[0]=alarm;
        return alarm_read_ok;
    } else if (reg==0x0f && length==10) {
        for (unsigned i=0;i<5;++i) {
            data[2*i]=therm[i]>>8;
            data[2*i+1]=(uint8_t)therm[i];
        }
    } else if (reg==0x7e && length==1) {
        data[0]=0x9c;
    } else if (reg>=0x1d && reg<=0x4c) {
        for (unsigned i=0;i<length;i+=2) data[i]=0x7d; /* 3,200 mV。 */
    }
    return true;
}

bool dvc_write(uint8_t reg, const uint8_t *data, uint8_t length)
{
    assert(reg==0 && length==1);
    clear_mask=(uint8_t)~data[0];
    return true;
}

bool dvc_set(uint8_t reg, uint8_t value, uint8_t mask)
{
    (void)reg; (void)value; (void)mask;
    return true;
}

int main(void)
{
    Config config;
    config_defaults(&config,16,false);
    afe_begin(&config);
    AfeResult result=AFE_WAIT;
    for (unsigned i=0;i<40 && result==AFE_WAIT;++i) result=afe_setup_step();
    assert(result==AFE_OK);

    Sample s;
    status=0x40; /* VADF，CC2 尚未完成。 */
    assert(afe_sample(&config,0,&s)==AFE_WAIT);
    status=0x10; /* CC2F，电流取得新样本。 */
    assert(afe_sample(&config,200,&s)==AFE_OK && s.current_fresh);
    status=0x40;
    assert(afe_sample(&config,400,&s)==AFE_OK && !s.current_fresh);
    assert(s.min_mv==3200 && s.max_mv==3200 && s.ma==0 && s.heater_ntc_valid);

    /* GP1 在实板超过 V1P8。其余温度和两个单体块仍须读出。 */
    const uint16_t board_therm[]={0x45dc,0x4624,0x22c9,0x22d7,0x22be};
    memcpy(therm,board_therm,sizeof(therm));
    status=0x50;
    assert(afe_sample(&config,600,&s)==AFE_OK);
    assert(!s.heater_ntc_valid && s.temp_x10[0]==0 && s.temp_x10[1]>600 && s.temp_x10[3]>600);
    assert(s.sum_mv==16u*3200u && s.mv[15]==3200);
    for (unsigned i=2;i<5;++i) {
        therm[i]=therm[0];
        assert(afe_sample(&config,800,&s)==AFE_ERROR);
        therm[i]=board_therm[i];
    }
    const uint8_t read_failures[]={0x0f,0x1d,0x2d};
    for (unsigned i=0;i<sizeof(read_failures);++i) {
        failed_block=read_failures[i];
        assert(afe_sample(&config,1000,&s)==AFE_ERROR);
    }

    uint8_t remaining=0;
    alarm=0x41;
    assert(afe_ack(1,&remaining) && clear_mask==1 && remaining==0x41);
    alarm=0x40;
    assert(afe_ack(1,&remaining) && remaining==0x40);
    alarm_read_ok=false;
    assert(!afe_ack(1,&remaining));
    puts("PASS AFE: CC2 freshness is distinct from VADC, W0C preserves unrequested alarms and reads back");
    return 0;
}
