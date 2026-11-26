#include "../store.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

static uint8_t flash[0x10000],baseline[sizeof(flash)];
static int budget=-1;
bool nor_read(uint32_t at,void *out,unsigned n)
{
    assert(at>=0x40000 && at+n<=0x50000);
    memcpy(out,flash+at-0x40000,n); return true;
}
bool nor_write(uint32_t at,const void *p,unsigned n)
{
    assert(n<=256-(at&255));
    for (unsigned i=0;i<n;++i) {
        if (!budget) return false;
        if (budget>0) --budget;
        flash[at-0x40000+i]&=((const uint8_t *)p)[i];
    }
    return true;
}
bool nor_erase(uint32_t at)
{
    assert(!(at&4095));
    for (unsigned i=0;i<4096;++i) {
        if (!budget) return false;
        if (budget>0) --budget;
        flash[at-0x40000+i]=255;
    }
    return true;
}
int main(void)
{
    memset(flash,255,sizeof(flash));
    uint32_t cfg=0, saved=123;
    assert(store_open(CONFIG_DATA,&cfg,4)==STORE_BLANK);
    assert(store_open(SOC_DATA,&cfg,4)==STORE_BLANK);
    assert(store_append(SOC_DATA,&saved,4));
    for (cfg=1;cfg<=32;++cfg) assert(store_append(CONFIG_DATA,&cfg,4));
    memcpy(baseline,flash,sizeof(flash));
    for (int cut=0;cut<=4120;++cut) {
        memcpy(flash,baseline,sizeof(flash)); budget=-1;
        assert(store_open(CONFIG_DATA,&cfg,4)==STORE_VALID && cfg==32);
        cfg=33; budget=cut; bool success=store_append(CONFIG_DATA,&cfg,4); budget=-1;
        assert(store_open(CONFIG_DATA,&cfg,4)==STORE_VALID);
        assert(cfg==(success?33u:32u));
        assert(store_open(SOC_DATA,&cfg,4)==STORE_VALID && cfg==123);
    }
    assert(store_open(EVENT_DATA,NULL,2)==STORE_BLANK);
    for (unsigned i=1;i<=650;++i) {
        uint16_t event=i;
        assert(store_append(EVENT_DATA,&event,2));
        if (!(i%29)) assert(store_open(EVENT_DATA,NULL,2)==STORE_VALID);
        assert(store_events()==(i<100?i:100));
        for (unsigned j=0;j<store_events();++j) {
            assert(store_event_at(j,&event) && event==i-j);
        }
    }
    puts("NOR: 4121 power cuts, sector rotation, newest 100, domain isolation OK");
}
