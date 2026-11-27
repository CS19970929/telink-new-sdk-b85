#include "../afe.h"
#include "../dvc_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t status, alarm, clear_mask;
static bool alarm_read_ok=true;

bool dvc_read(uint8_t reg, uint8_t *data, uint8_t length)
{
    memset(data,0,length);
    if (reg==0 && length==7) {
        data[0]=alarm;
        data[1]=status;
    } else if (reg==0 && length==1) {
        data[0]=alarm;
        return alarm_read_ok;
    } else if (reg==0x0f && length==10) {
        const uint8_t temperatures[]={0x46,0x50,0x27,0x10,0x27,0x10,0x27,0x10,0x27,0x10};
        memcpy(data,temperatures,sizeof(temperatures));
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
    assert(s.min_mv==3200 && s.max_mv==3200 && s.ma==0);

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
