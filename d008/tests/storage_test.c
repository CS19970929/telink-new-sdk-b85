#include "../store.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

static uint8_t flash[0x10000],baseline[sizeof(flash)];
static int budget=-1;
static bool refuse_marker;
static unsigned erases[16], programs[16];
bool nor_read(uint32_t at,void *out,unsigned n)
{
    assert(at>=0x40000 && at+n<=0x50000);
    memcpy(out,flash+at-0x40000,n); return true;
}
bool nor_write(uint32_t at,const void *p,unsigned n)
{
    if (refuse_marker && n==1) {store_deferred=true; return false;}
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

static void deferred_commit(void)
{
    uint32_t v=0;
    memset(flash,255,sizeof(flash));
    memset(erases,0,sizeof(erases)); memset(programs,0,sizeof(programs));
    assert(store_open(CONFIG_DATA,&v,sizeof(v))==STORE_BLANK);
    for (v=1;v<=32;++v) assert(store_append(CONFIG_DATA,&v,sizeof(v)));
    refuse_marker=true;
    for (unsigned i=0;i<2000;++i) assert(!store_append(CONFIG_DATA,&v,sizeof(v)));
    assert(store_deferred);
    refuse_marker=false;
    assert(store_append(CONFIG_DATA,&v,sizeof(v)));
    assert(!store_deferred);
    assert(erases[0]==1 && erases[1]==0 && programs[0]+programs[1]==66);
    v=0;
    assert(store_open(CONFIG_DATA,&v,sizeof(v))==STORE_VALID && v==33);
    puts("NOR: 2000 denied commit windows; one body, one erase, one visible update");
}

static void domain_cuts(Domain domain,unsigned record_bytes,unsigned sector,unsigned slots)
{
    uint8_t payload[sizeof(Config)], recovered[sizeof(Config)];
    assert(record_bytes<=sizeof(payload));
    memset(flash,255,sizeof(flash));
    assert(store_open(domain,recovered,record_bytes)==STORE_BLANK);
    for (unsigned i=0;i<2*slots;++i) {
        memset(payload,(int)i,record_bytes);
        assert(store_append(domain,payload,record_bytes));
    }
    memcpy(baseline,flash,sizeof(flash));
    unsigned old=(2*slots-1)&255u;
    memset(payload,0x5a,record_bytes);
    unsigned last=4096+17+record_bytes;
    for (unsigned cut=0;cut<=last;++cut) {
        memcpy(flash,baseline,sizeof(flash));
        assert(store_open(domain,recovered,record_bytes)==STORE_VALID);
        budget=(int)cut;
        bool committed=store_append(domain,payload,record_bytes);
        budget=-1;
        assert(store_open(domain,recovered,record_bytes)==STORE_VALID);
        for (unsigned i=0;i<record_bytes;++i) assert(recovered[i]==(committed?0x5a:old));
        assert(!memcmp(flash,baseline,sector*4096));
        unsigned end=(sector+2)*4096;
        assert(!memcmp(flash+end,baseline+end,sizeof(flash)-end));
        if (domain==EVENT_DATA) {
            uint16_t event=0;
            assert(store_events()==100 && store_event_at(99,&event));
            assert(event==(uint16_t)((old-99+(committed?1:0))*257u));
        }
    }
    /* 最后一次已提交载荷、CRC 或标志翻转一位时，只采用前一条完整记录。 */
    unsigned slot=(sector+2)*4096-4096/slots;
    for (unsigned i=0;i<record_bytes+4;++i) {
        memcpy(flash,baseline,sizeof(flash)); flash[slot+12+i]^=1;
        assert(store_open(domain,recovered,record_bytes)==STORE_VALID && recovered[0]==((old-1)&255u));
    }
    printf("domain=%u payload=%u rotation_cuts=%u CRC_fallback=%u\n",
           domain,record_bytes,last+1,record_bytes+4);
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
    domain_cuts(CONFIG_DATA,sizeof(Config),0,16);
    domain_cuts(SOC_DATA,12,4,64);
    domain_cuts(EVENT_DATA,2,12,128);
    deferred_commit();
    first_config();
    wear(CONFIG_DATA,0,16,10000);
    wear(SOC_DATA,4,64,100000);
    wear(EVENT_DATA,12,128,250000);
    puts("NOR: 4121 power cuts, sector rotation, newest 100, domain isolation OK");
}
