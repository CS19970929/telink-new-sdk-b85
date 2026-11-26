#include "../history.h"
#include "../store.h"
#include <assert.h>
#include <stdio.h>
static uint16_t seen[120];
static unsigned attempts,used;
static bool permit;
bool store_deferred;
bool store_append(Domain d,const void *data,unsigned size)
{
    assert(d==EVENT_DATA && size==2); ++attempts;
    if (!permit) return false;
    seen[used++]=*(const uint16_t *)data; return true;
}
int main(void)
{
    uint32_t start=UINT32_MAX-1000u;
    assert(history_note(1,start));
    assert(history_observe((1u<<OV)|(1u<<DISCHARGE_OC),start+100));
    assert(!history_flush(start+150));
    assert(history_flush(start+200) && attempts==1);
    store_deferred=true;
    assert(history_flush(start+5150) && attempts==2);
    permit=true; store_deferred=false;
    for (unsigned i=0;i<3;++i) assert(history_flush(start+10200));
    assert(used==3 && seen[0]==0x0100 && seen[1]==0x06ab && seen[2]==0x0bab);
    assert(history_observe(0,start+10300));
    assert(history_observe(1u<<DISCHARGE_OC,start+10400));
    assert(history_flush(start+10500) && used==3);
    assert(history_note(2,start+3610600) && history_flush(start+3610600));
    assert(seen[3]==0x0202);
    for (unsigned i=0;i<101;++i) {
        bool fits=history_note(6+i%2,start+3610700+i);
        assert(fits==(i<100));
    }
    for (unsigned i=0;i<100;++i) assert(history_flush(start+3611000+i));
    assert(used==104 && seen[4]==0x07ab && seen[103]==0x06ab);
    puts("history: simultaneous edges, retry, wrap, repeated fault, most recent 100 OK");
}
