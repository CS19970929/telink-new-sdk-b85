#include "../store.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

static uint8_t flash[0x10000],baseline[sizeof(flash)];
static int budget=-1;
static unsigned erases[16], programs[16];
bool nor_read(uint32_t at,void *out,unsigned n)
{
    assert(at>=0x40000 && at+n<=0x50000);
    memcpy(out,flash+at-0x40000,n); return true;
}
bool nor_write(uint32_t at,const void *p,unsigned n)
{
    ++programs[(at-0x40000)/4096];
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
    ++erases[(at-0x40000)/4096];
    assert(!(at&4095));
    for (unsigned i=0;i<4096;++i) {
        if (!budget) return false;
        if (budget>0) --budget;
        flash[at-0x40000+i]=255;
    }
    return true;
}
static void wear(Domain domain, unsigned sector, unsigned slots, unsigned saves)
{
    uint32_t value=0, latest=0;
    uint16_t event;
    unsigned size=domain==EVENT_DATA ? sizeof(event) : sizeof(value);
    memset(flash,255,sizeof(flash));
    memset(erases,0,sizeof(erases));
    memset(programs,0,sizeof(programs));
    assert(store_open(domain,&latest,size)==STORE_BLANK);
    for (unsigned i=1;i<=saves;++i) {
        value=i;
        event=(uint16_t)(i%17);
        const void *payload=domain==EVENT_DATA ? (void *)&event : (void *)&value;
        assert(store_append(domain,payload,size));
        if (domain!=EVENT_DATA) {
            /* 关机、OTA 和主机重发相同快照。 */
            for (unsigned retry=0;retry<4;++retry)
                assert(store_append(domain,payload,size));
        }
        if (!(i%997)) {
            assert(store_open(domain,&latest,size)==STORE_VALID);
            if (domain!=EVENT_DATA) assert(latest==value);
        }
    }
    unsigned rotations=(saves-1)/slots-1;
    assert(erases[sector]+erases[sector+1]==rotations);
    assert(erases[sector]==(rotations+1)/2 && erases[sector+1]==rotations/2);
    assert(programs[sector]+programs[sector+1]==2*saves);
    for (unsigned i=0;i<16;++i)
        if (i!=sector && i!=sector+1) assert(!erases[i] && !programs[i]);
    printf("domain=%u changes=%u duplicate_saves=%u programs=%u erase_A=%u erase_B=%u\n",
           domain,saves,domain==EVENT_DATA?0:4*saves,2*saves,erases[sector],erases[sector+1]);
}

static void first_config(void)
{
    uint8_t defaults[173], readback[sizeof(defaults)];
    for (unsigned i=0;i<sizeof(defaults);++i) defaults[i]=(uint8_t)(i*13u);
    for (unsigned cut=0;cut<=sizeof(defaults)+17;++cut) {
        memset(flash,255,sizeof(flash));
        assert(store_open_config(readback,defaults,sizeof(defaults))==STORE_BLANK);
        budget=(int)cut;
        bool committed=store_append(CONFIG_DATA,defaults,sizeof(defaults));
        budget=-1;
        StoreResult status=store_open_config(readback,defaults,sizeof(defaults));
        assert(status==(committed?STORE_VALID:cut?STORE_INITIAL:STORE_BLANK));
        assert(store_append(CONFIG_DATA,defaults,sizeof(defaults)));
        assert(store_open_config(readback,defaults,sizeof(defaults))==STORE_VALID);
        assert(!memcmp(defaults,readback,sizeof(defaults)));
    }
    /* 单条已提交记录 CRC 损坏，以及未知旧格式，两者都必须保留现场。 */
    flash[12]^=1;
    assert(store_open_config(readback,defaults,sizeof(defaults))==STORE_BAD);
    assert(!store_append(CONFIG_DATA,defaults,sizeof(defaults)));
    memset(flash,255,sizeof(flash)); flash[0]=0;
    assert(store_open_config(readback,defaults,sizeof(defaults))==STORE_BAD);
    assert(!store_append(CONFIG_DATA,defaults,sizeof(defaults)));
    puts("NOR: 191 first-write power cuts; corrupt and foreign config retained");
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
    first_config();
    wear(CONFIG_DATA,0,16,10000);
    wear(SOC_DATA,4,64,100000);
    wear(EVENT_DATA,12,128,250000);
    puts("NOR: 4121 power cuts, sector rotation, newest 100, domain isolation OK");
}
