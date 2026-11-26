#include "../sif.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    Device d={0};
    uint8_t p[64];
    d.valid=true; d.soc.initialized=true; d.soc.state.percent=50;
    d.sample.sum_mv=48000; d.sample.temp_x10[1]=300; d.sample.temp_x10[2]=290;
    assert(sif_frame(&d,true,0,p)==6);
#if D008_LISHEN_EXTENDED
    const uint8_t example[]={0,50,48,246,0xc0,0x18};
#else
    const uint8_t example[]={0,50,48,246,0,0x58};
#endif
    assert(!memcmp(p,example,6));
    const int32_t ma[]={INT_MIN,-127500,-127499,-500,-499,0,499,500,127499,127500,INT_MAX};
    const uint8_t expected[]={128,128,129,255,0,0,0,1,127,128,128};
    for (unsigned i=0;i<sizeof(ma)/sizeof(*ma);++i) {
        d.sample.ma=ma[i]; d.control.latched=(1u<<CHARGE_HOT)|(1u<<OV);
        assert(sif_frame(&d,true,0,p)==6);
        assert(p[0]==(D008_LISHEN_EXTENDED?expected[i]:0) && p[4]==(D008_LISHEN_EXTENDED?0xc7:0));
        assert((uint8_t)(p[0]+p[1]+p[2]+p[3]+p[4])==p[5]);
    }
    d.sample.temp_x10[1]=1674; assert(sif_frame(&d,true,0,p)==6 && p[3]==127);
    d.sample.temp_x10[1]=1675; assert(!sif_frame(&d,true,0,p));
    d.sample.temp_x10[1]=d.sample.temp_x10[2]=0;
    d.sample.sum_mv=499; assert(!sif_frame(&d,true,0,p));
    d.sample.sum_mv=500; assert(sif_frame(&d,true,0,p)==6 && p[2]==1 && p[3]==216);
    d.sample.sum_mv=255500; assert(!sif_frame(&d,true,0,p));
    d.sample.sum_mv=48000; d.soc.state.percent=101; assert(!sif_frame(&d,true,0,p));
    d.soc.state.percent=50; d.valid=false; assert(!sif_frame(&d,true,0,p));
    printf("Lishen extended=%d: 6-byte vectors, signed current, faults, validity, checksum OK\n",D008_LISHEN_EXTENDED);
}
