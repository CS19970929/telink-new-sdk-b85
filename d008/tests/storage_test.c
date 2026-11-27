#include "../store.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

static uint8_t flash[0x10000],baseline[sizeof(flash)];
static int budget=-1;
static bool refuse_marker, refuse_second, incomplete_erase;
static uint32_t fail_read;
static unsigned erases[16], programs[16];
bool nor_read(uint32_t at,void *out,unsigned n)
{
    assert(at>=0x40000 && at+n<=0x50000);
    if (fail_read>=at && fail_read-at<n) return false;
    memcpy(out,flash+at-0x40000,n); return true;
}
bool nor_write(uint32_t at,const void *p,unsigned n)
{
    if ((refuse_marker && n==1) || (refuse_second && n>1 && (at&511)==256)) {store_deferred=true; return false;}
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
    if (incomplete_erase) flash[at-0x40000+4095]=0;
    return true;
}
static void prepare_soc_space(void)
{
    uint32_t value=0;
    memset(flash,255,sizeof(flash));
    memset(erases,0,sizeof(erases));
    flash[0]=0xaa; flash[0xc000]=0xbb;
    assert(store_open(SOC_DATA,&value,sizeof(value))==STORE_BLANK);
    assert(store_prepare_soc_space() && !erases[4] && !erases[5]);
    for (value=1;value<=128;++value) assert(store_append(SOC_DATA,&value,sizeof(value)));
    memcpy(baseline,flash,sizeof(flash));
    budget=97;
    assert(!store_prepare_soc_space());
    budget=-1;
    assert(store_open(SOC_DATA,&value,sizeof(value))==STORE_VALID && value==128);
    assert(store_prepare_soc_space() && erases[4]==2 && !erases[5]);
    assert(!memcmp(flash+0x5000,baseline+0x5000,4096));
    assert(flash[0]==0xaa && flash[0xc000]==0xbb);
    assert(store_prepare_soc_space() && erases[4]==2);
    ++value;
    assert(store_append(SOC_DATA,&value,sizeof(value)));
    assert(store_open(SOC_DATA,&value,sizeof(value))==STORE_VALID && value==129);
    puts("NOR: prepared SOC spare sector preserves committed state through an interrupted erase");
}

static void wear(Domain domain, unsigned sector, unsigned slots, unsigned saves)
{
    uint32_t value=0, latest=0;
    uint16_t event;
    unsigned size=domain==EVENT_DATA ? LOG_PAYLOAD_BYTES : sizeof(value);
    memset(flash,255,sizeof(flash));
    memset(erases,0,sizeof(erases));
    memset(programs,0,sizeof(programs));
    assert((domain==EVENT_DATA?store_open_log():store_open(domain,&latest,size))==STORE_BLANK);
    for (unsigned i=1;i<=saves;++i) {
        value=i;
        event=(uint16_t)(1+i%17);
        const void *payload=domain==EVENT_DATA ? (void *)&event : (void *)&value;
        assert(domain==EVENT_DATA?store_log_append(event):store_append(domain,payload,size));
        if (domain!=EVENT_DATA) {
            /* 关机、OTA 和主机重发相同快照。 */
            for (unsigned retry=0;retry<4;++retry)
                assert(store_append(domain,payload,size));
        }
        if (!(i%997)) {
            assert((domain==EVENT_DATA?store_open_log():store_open(domain,&latest,size))==STORE_VALID);
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

static void independent_initialization(void)
{
    Config defaults, readback, wrong;
    config_defaults(&defaults,16,false);
    wrong=defaults; wrong.cells=20;
    uint32_t soc=123;
    uint16_t log=0x1234;
    memset(flash,255,sizeof(flash));
    assert(store_open(SOC_DATA,NULL,4)==STORE_BLANK && store_append(SOC_DATA,&soc,4));
    assert(store_open_log()==STORE_BLANK && store_log_append(log));
    memset(flash,0x55,8192);
    flash[0]='D'; flash[1]='8'; flash[2]=1;
    memcpy(baseline,flash,sizeof(flash));
    for (unsigned cut=0;cut<=4096+17+sizeof(defaults);++cut) {
        memcpy(flash,baseline,sizeof(flash));
        assert(store_open_config(&readback,&defaults)==STORE_BAD);
        budget=(int)cut;
        bool committed=store_append(CONFIG_DATA,&defaults,sizeof(defaults));
        budget=-1;
        assert(store_open_config(&readback,&defaults)==(committed?STORE_VALID:STORE_BAD));
        assert(store_append(CONFIG_DATA,&defaults,sizeof(defaults)));
        assert(store_open_config(&readback,&defaults)==STORE_VALID);
        assert(!memcmp(&defaults,&readback,sizeof(defaults)));
        assert(!memcmp(flash+8192,baseline+8192,sizeof(flash)-8192));
    }
    fail_read=0x41000;
    assert(store_open_config(&readback,&defaults)==STORE_IO_ERROR);
    assert(!store_append(CONFIG_DATA,&defaults,sizeof(defaults)));
    fail_read=0;
    assert(store_open_config(&readback,&defaults)==STORE_VALID);
    Config invalid=defaults;
    invalid.capacity_dah=0;
    assert(store_append(CONFIG_DATA,&invalid,sizeof(invalid)));
    assert(store_open_config(&readback,&defaults)==STORE_VALID);
    assert(readback.capacity_dah==defaults.capacity_dah);
    assert(store_append(CONFIG_DATA,&wrong,sizeof(wrong)));
    assert(store_open_config(&readback,&defaults)==STORE_CONFLICT);
    assert(!store_append(CONFIG_DATA,&defaults,sizeof(defaults)));
    puts("NOR: independent old format take-over, power cuts, invalid fallback, read failure and assembly gate");
}

static void deferred_commit(void)
{
    uint32_t v=0;
    memset(flash,255,sizeof(flash));
    memset(erases,0,sizeof(erases)); memset(programs,0,sizeof(programs));
    assert(store_open(CONFIG_DATA,&v,sizeof(v))==STORE_BLANK);
    for (v=1;v<=16;++v) assert(store_append(CONFIG_DATA,&v,sizeof(v)));
    refuse_marker=true;
    for (unsigned i=0;i<2000;++i) assert(!store_append(CONFIG_DATA,&v,sizeof(v)));
    assert(store_deferred);
    refuse_marker=false;
    assert(store_append(CONFIG_DATA,&v,sizeof(v)));
    assert(!store_deferred);
    assert(erases[0]==1 && erases[1]==0 && programs[0]+programs[1]==34);
    v=0;
    assert(store_open(CONFIG_DATA,&v,sizeof(v))==STORE_VALID && v==17);
    puts("NOR: 2000 denied commit windows; one body, one erase, one visible update");
}

static void deferred_second_page(void)
{
    memset(flash,255,sizeof(flash));
    memset(programs,0,sizeof(programs));
    Config defaults, readback;
    config_defaults(&defaults,16,false);
    assert(store_open_config(&readback,&defaults)==STORE_BLANK);
    refuse_second=true;
    for (unsigned i=0;i<100;++i) {
        assert(!store_append(CONFIG_DATA,&defaults,sizeof(defaults)));
        assert(store_deferred && programs[0]==1);
    }
    refuse_second=false; refuse_marker=true;
    assert(!store_append(CONFIG_DATA,&defaults,sizeof(defaults)) && store_deferred);
    assert(programs[0]==2);
    refuse_marker=false;
    assert(store_append(CONFIG_DATA,&defaults,sizeof(defaults)) && programs[0]==3);
    assert(store_open_config(&readback,&defaults)==STORE_VALID);
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

static void log_clear_cuts(void)
{
    memset(flash,255,sizeof(flash));
    assert(store_open_log()==STORE_BLANK);
    for (unsigned i=1;i<=256;++i) assert(store_log_append(i));
    memcpy(baseline,flash,sizeof(flash));
    for (int cut=0;cut<=4096+LOG_PAYLOAD_BYTES+17;++cut) {
        memcpy(flash,baseline,sizeof(flash));
        assert(store_open_log()==STORE_VALID);
        budget=cut;
        bool committed=store_log_clear(7,3);
        budget=-1;
        assert(store_open_log()==STORE_VALID);
        assert(store_events()==(committed?0:100));
        assert(store_log_update_id()==(committed?7:0));
        assert(store_log_factory_id()==(committed?3:0));
        assert(store_log_clear(7,3) && store_events()==0);
        assert(store_log_append(777));
        assert(store_open_log()==STORE_VALID && store_events()==1);
        assert(store_log_clear(7,3) && store_events()==1);
    }
    /* 两个扇区都已满，有效记录散落于大量坏槽之间。 */
    memset(flash,0,8192);
    memset(flash+0xc000,255,8192);
    assert(store_open_log()==STORE_BLANK);
    for (unsigned i=1;i<=256;++i) assert(store_log_append(i));
    for (unsigned i=0;i<256;++i)
        if (i%32) flash[0xc000+i*32+31]=0;
    assert(store_open_log()==STORE_VALID && store_events()==8);
    assert(store_log_append(999) && store_log_degraded() && store_events()==5);
    uint16_t event=0;
    assert(store_open_log()==STORE_VALID && store_event_at(0,&event) && event==999);
    puts("PASS log: clear receipts, rotate power cuts, old epoch suppression and damaged slots");
}

int main(void)
{
    memset(flash,255,sizeof(flash));
    uint32_t cfg=0, saved=123;
    assert(store_open(CONFIG_DATA,&cfg,4)==STORE_BLANK);
    assert(store_open(SOC_DATA,&cfg,4)==STORE_BLANK);
    assert(store_append(SOC_DATA,&saved,4));
    for (cfg=1;cfg<=16;++cfg) assert(store_append(CONFIG_DATA,&cfg,4));
    memcpy(baseline,flash,sizeof(flash));
    for (int cut=0;cut<=4120;++cut) {
        memcpy(flash,baseline,sizeof(flash)); budget=-1;
        assert(store_open(CONFIG_DATA,&cfg,4)==STORE_VALID && cfg==16);
        cfg=17; budget=cut; bool success=store_append(CONFIG_DATA,&cfg,4); budget=-1;
        assert(store_open(CONFIG_DATA,&cfg,4)==STORE_VALID);
        assert(cfg==(success?17u:16u));
        assert(store_open(SOC_DATA,&cfg,4)==STORE_VALID && cfg==123);
    }
    assert(store_open_log()==STORE_BLANK);
    for (unsigned i=1;i<=650;++i) {
        uint16_t event=i;
        assert(store_log_append(event));
        if (!(i%29)) assert(store_open_log()==STORE_VALID);
        assert(store_events()==(i<100?i:100));
        for (unsigned j=0;j<store_events();++j) {
            assert(store_event_at(j,&event) && event==i-j);
        }
    }
    domain_cuts(CONFIG_DATA,sizeof(Config),0,8);
    domain_cuts(SOC_DATA,12,4,64);
    log_clear_cuts();
    deferred_commit();
    deferred_second_page();
    prepare_soc_space();
    independent_initialization();
    wear(CONFIG_DATA,0,8,10000);
    wear(SOC_DATA,4,64,100000);
    wear(EVENT_DATA,12,128,250000);
    puts("NOR: 4121 power cuts, sector rotation, newest 100, domain isolation OK");
}
