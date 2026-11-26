#include "history.h"
#include "store.h"

enum { DEPTH=100, WEEK_MS=168u*3600000u, RETRY_MS=5000 };
static uint16_t pending[DEPTH], previous_faults;
static unsigned first,count,last_id;
static uint32_t observed_ms,interval_ms,retry_ms;
static bool deferred;

static void advance(uint32_t ms)
{
    uint32_t dt=ms-observed_ms;
    observed_ms=ms;
    uint32_t space=WEEK_MS+1000-interval_ms;
    interval_ms+=dt<space?dt:space;
}

bool history_note(unsigned id,uint32_t ms)
{
    advance(ms);
    if (id>2 && id==last_id && interval_ms<=60000) return true;
    unsigned code=0;
    if (id!=1) code=interval_ms<=60000?171:interval_ms>WEEK_MS?170:(interval_ms+3599999)/3600000;
    bool fits=count<DEPTH;
    if (!fits) {first=(first+1)%DEPTH; --count;}
    pending[(first+count)%DEPTH]=(uint16_t)(id<<8|code);
    ++count; last_id=id; interval_ms=0;
    return fits;
}

bool history_observe(uint16_t active,uint32_t ms)
{
    /* 顺序对应 Limit，再接开线、AFE、存储和加热。MOS 高温无独立历史 ID。 */
    static const uint8_t ids[]={6,9,7,10,8,11,14,12,15,13,0,16,17,18,20,4};
    uint16_t rising=active&~previous_faults;
    previous_faults=active;
    advance(ms);
    bool fits=true;
    for (unsigned i=0;i<16;++i)
        if ((rising&(1u<<i)) && ids[i] && !history_note(ids[i],ms)) fits=false;
    return fits;
}

bool history_flush(uint32_t ms)
{
    if (!count || (deferred && ms-retry_ms<RETRY_MS)) return true;
    if (!store_append(EVENT_DATA,&pending[first],sizeof(*pending))) {
        deferred=true; retry_ms=ms; return store_deferred;
    }
    deferred=false; first=(first+1)%DEPTH; --count;
    return true;
}
