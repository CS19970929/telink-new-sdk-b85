#include "store.h"
#include <string.h>

enum { SECTOR = 4096, EVENTS = 100, FORMAT = 1, COMMIT = 0x3c,
       INIT_CONFIG_INDEX = 2 * (DOMAIN_COUNT - 1) };
static const uint32_t base[] = {0x40000,0x44000,0x4c000};
static const uint16_t stride[] = {256,64,32};
typedef struct {
    uint32_t sequence, latest, pending;
    uint16_t next[2], bytes;
    bool ready;
} Journal;
static Journal journal[DOMAIN_COUNT];
bool store_deferred;
static struct {uint32_t seq, addr;} recent[EVENTS];
static unsigned count;

static void put32(uint8_t *p,uint32_t n)
{
    for (unsigned i=0;i<4;++i) {p[i]=(uint8_t)n; n>>=8;}
}
static uint32_t get32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint32_t crc(const uint8_t *p,unsigned n)
{
    uint32_t value=UINT32_MAX;
    while (n--) {
        value ^= *p++;
        for (unsigned b=0;b<8;++b) value=(value>>1)^((0u-(value&1u))&0xedb88320u);
    }
    return ~value;
}
static bool blank(const uint8_t *p,unsigned n)
{
    while (n--) if (*p++ != 0xff) return false;
    return true;
}
static bool current_format(const uint8_t *p)
{
    return p[0]=='D' && p[1]=='8' && p[2]==FORMAT;
}

static bool valid(Domain d,const uint8_t *p,unsigned size)
{
    return current_format(p) && p[3]==d && p[4]==size &&
        p[stride[d]-1]==COMMIT && get32(p+12+size)==crc(p,12+size);
}

/* 标识字节损坏时，余下的完整记录仍足以识别本代格式。 */
static bool known_config(uint8_t *p,unsigned size)
{
    if (current_format(p)) return true;
    p[0]='D'; p[1]='8'; p[2]=FORMAT;
    return valid(CONFIG_DATA,p,size);
}

static void index_event(uint32_t seq,uint32_t addr)
{
    unsigned i=0;
    while (i<count && (int32_t)(seq-recent[i].seq)<0) ++i;
    if (i==EVENTS) return;
    if (count<EVENTS) ++count;
    for (unsigned j=count-1;j>i;--j) recent[j]=recent[j-1];
    recent[i].seq=seq; recent[i].addr=addr;
}

static void encode(Domain d,const void *data,unsigned size,uint32_t sequence,uint8_t *slot)
{
    memset(slot,0xff,stride[d]);
    slot[0]='D'; slot[1]='8'; slot[2]=FORMAT; slot[3]=d; slot[4]=size;
    put32(slot+8,sequence); memcpy(slot+12,data,size);
    put32(slot+12+size,crc(slot,12+size));
}

/* 首次写入断电允许有些位仍为 1。已提交的损坏记录或不符的装配不得自动覆盖。 */
static bool partial_factory(const uint8_t *slot,const uint8_t *factory)
{
    unsigned marker=stride[CONFIG_DATA]-1;
    if (slot[marker]==COMMIT) return false;
    for (unsigned i=0;i<marker;++i)
        if ((slot[i]&factory[i])!=factory[i]) return false;
    if (slot[marker]==0xff) return true;
    return (slot[marker]&COMMIT)==COMMIT && !memcmp(slot,factory,marker);
}

static StoreResult scan(Domain d,void *latest,unsigned size,
                        const uint8_t *factory,const uint8_t *prior_factory)
{
    uint8_t slot[256];
    if ((unsigned)d>=DOMAIN_COUNT || !size || size>(unsigned)stride[d]-17) return STORE_BAD;
    Journal *j=&journal[d];
    memset(j,0,sizeof(*j)); j->bytes=size;
    if (d==EVENT_DATA) count=0;
    bool dirty=false, current=false, first_only=factory!=NULL;
    for (unsigned sector=0;sector<2;++sector) {
        for (unsigned off=0;off<SECTOR;off+=stride[d]) {
            uint32_t address=base[d]+sector*SECTOR+off;
            if (!nor_read(address,slot,stride[d])) return STORE_IO_ERROR;
            if (blank(slot,stride[d])) continue;
            dirty=true; j->next[sector]=off+stride[d];
            if (!valid(d,slot,size)) {
                if (first_only && !partial_factory(slot,factory) &&
                    (!prior_factory || !partial_factory(slot,prior_factory))) first_only=false;
                if (factory && !current && known_config(slot,size)) current=true;
                continue;
            }
            uint32_t seq=get32(slot+8);
            if (d==EVENT_DATA) index_event(seq,address);
            if (!j->latest || (int32_t)(seq-j->sequence)>0) {
                j->latest=address; j->sequence=seq;
                if (latest) memcpy(latest,slot+12,size);
            }
        }
    }
    j->ready=j->latest || !dirty || first_only || d!=CONFIG_DATA;
    if (j->latest) return STORE_VALID;
    if (!dirty) return STORE_BLANK;
    if (first_only) return STORE_INITIAL;
    return factory && !current ? STORE_FOREIGN : STORE_BAD;
}

StoreResult store_open(Domain d,void *latest,unsigned size)
{
    return scan(d,latest,size,NULL,NULL);
}

StoreResult store_open_config(void *latest,const void *defaults,unsigned size)
{
    uint8_t factory[256];
    if (!defaults || !size || size>(unsigned)stride[CONFIG_DATA]-17) return STORE_BAD;
    encode(CONFIG_DATA,defaults,size,1,factory);
    return scan(CONFIG_DATA,latest,size,factory,NULL);
}

StoreResult store_open_product_config(Config *latest,const Config *defaults)
{
    if (defaults->nmc && defaults->capacity_dah==200)
        return store_open_config(latest,defaults,sizeof(*defaults));

    /* 识别格式 1 旧版本的首次写入残片。旧版本的默认容量固定为 20 Ah。 */
    Config prior=*defaults;
    prior.capacity_dah=200;
    if (!prior.nmc) {
        prior.sw[PACK_OV].release=prior.cells*prior.sw[OV].release/10;
        prior.sw[PACK_UV].release=prior.cells*prior.sw[UV].release/10;
        prior.hw_sc_a10=2000;
    }
    uint8_t factory[256], prior_factory[256];
    encode(CONFIG_DATA,defaults,sizeof(*defaults),1,factory);
    encode(CONFIG_DATA,&prior,sizeof(prior),1,prior_factory);
    return scan(CONFIG_DATA,latest,sizeof(*defaults),factory,prior_factory);
}

static uint32_t initialization_sector(unsigned index)
{
    /* 清理状态和历史在前，默认配置的提交在最后。 */
    const Domain order[]={SOC_DATA,EVENT_DATA,CONFIG_DATA};
    return base[order[index/2]]+(index%2)*SECTOR;
}

static bool inspect_sector(uint32_t address,bool *empty)
{
    uint8_t data[256];
    *empty=true;
    for (unsigned off=0;off<SECTOR;off+=sizeof(data)) {
        if (!nor_read(address+off,data,sizeof(data))) return false;
        if (!blank(data,sizeof(data))) *empty=false;
    }
    return true;
}

bool store_prepare_init(StoreResult config_status,uint8_t *erase_mask)
{
    if (config_status!=STORE_FOREIGN && config_status!=STORE_BLANK &&
        config_status!=STORE_INITIAL) return false;
    uint8_t mask=0;
    for (unsigned i=0;i<2*DOMAIN_COUNT;++i) {
        bool empty;
        if (!inspect_sector(initialization_sector(i),&empty)) return false;
        /* 留下已识别的默认配置残片，追加完成首次提交即可。 */
        if (!empty && (i<INIT_CONFIG_INDEX || config_status!=STORE_INITIAL))
            mask|=1u<<i;
    }
    *erase_mask=mask;
    return true;
}

StoreInitResult store_initialize(uint8_t *erase_mask,const Config *defaults)
{
    for (unsigned i=0;i<2*DOMAIN_COUNT;++i) {
        uint8_t bit=1u<<i;
        if (!(*erase_mask&bit)) continue;
        uint32_t address=initialization_sector(i);
        bool empty;
        if (!nor_erase(address) || !inspect_sector(address,&empty) || !empty)
            return STORE_INIT_FAILED;
        *erase_mask&=(uint8_t)~bit;
        return STORE_INIT_WAIT;
    }
    Config readback;
    StoreResult status=store_open_product_config(&readback,defaults);
    if (status!=STORE_BLANK && status!=STORE_INITIAL) return STORE_INIT_FAILED;
    return store_append(CONFIG_DATA,defaults,sizeof(*defaults)) ? STORE_INIT_DONE : STORE_INIT_FAILED;
}

static bool commit_body(Domain d,uint8_t *slot,uint8_t *verify)
{
    Journal *j=&journal[d];
    unsigned marker=stride[d]-1;
    uint32_t address=j->pending;
    slot[marker]=COMMIT;
    nor_write(address+marker,slot+marker,1);
    if (!nor_read(address,verify,stride[d])) {j->ready=false;return false;}
    if (memcmp(slot,verify,stride[d])) {
        /* BLE 空窗不足时，同一请求只重试提交标志。部分编程不再使用该槽。 */
        if (verify[marker]!=0xff || memcmp(slot,verify,marker)) j->pending=0;
        return false;
    }
    j->pending=0; j->latest=address; ++j->sequence;
    if (d==EVENT_DATA) index_event(j->sequence,address);
    return true;
}

static bool recycle_sector(Journal *j,unsigned sector,uint32_t address)
{
    bool empty;
    if (!nor_erase(address) || !inspect_sector(address,&empty) || !empty) return false;
    j->next[sector]=0;
    if (j->pending>=address && j->pending-address<SECTOR) j->pending=0;
    return true;
}

bool store_prepare_soc_space(void)
{
    store_deferred=false;
    Journal *j=&journal[SOC_DATA];
    if (!j->ready || j->next[0]<SECTOR || j->next[1]<SECTOR) return true;
    /* 空闲且无 BLE 连接时周转旧扇区，最新检查点所在扇区继续保留。 */
    unsigned spare=j->latest<base[SOC_DATA]+SECTOR;
    return recycle_sector(j,spare,base[SOC_DATA]+spare*SECTOR);
}

bool store_append(Domain d,const void *data,unsigned size)
{
    uint8_t slot[256],verify[256];
    store_deferred=false;
    if ((unsigned)d>=DOMAIN_COUNT || !data) return false;
    Journal *j=&journal[d];
    if (!j->ready || size!=j->bytes) return false;
    if (d!=EVENT_DATA && j->latest) {
        if (!nor_read(j->latest,verify,stride[d])) return false;
        if (!valid(d,verify,size) || get32(verify+8)!=j->sequence) {
            j->ready=false;
            return false;
        }
        if (!memcmp(verify+12,data,size)) return true;
    }
    encode(d,data,size,j->sequence+1u,slot);
    if (j->pending) {
        if (!nor_read(j->pending,verify,stride[d])) return false;
        if (!memcmp(slot,verify,stride[d])) return commit_body(d,slot,verify);
        j->pending=0;
    }
    unsigned sector=j->latest >= base[d]+SECTOR;
    if (j->next[sector]>=SECTOR) {
        sector^=1;
        if (j->next[sector]>=SECTOR) {
            uint32_t erase=base[d]+sector*SECTOR;
            if (d==EVENT_DATA)
                for (unsigned i=0;i<count;++i)
                    if (recent[i].addr/SECTOR==erase/SECTOR) return false;
            if (!recycle_sector(j,sector,erase)) return false;
        }
    }
    uint32_t addr=base[d]+sector*SECTOR+j->next[sector];
    if (!nor_read(addr,verify,stride[d]) || !blank(verify,stride[d])) return false;
    /* 未获准写入时保留空槽。若部分编程则跳过该槽，复位扫描也会跳过。 */
    if (!nor_write(addr,slot,16+size) || !nor_read(addr,verify,16+size) ||
        memcmp(slot,verify,16+size)) {
        if (!nor_read(addr,verify,stride[d])) j->ready=false;
        else if (!blank(verify,stride[d])) j->next[sector]+=stride[d];
        return false;
    }
    j->next[sector]+=stride[d];
    j->pending=addr;
    return commit_body(d,slot,verify);
}

unsigned store_events(void) {return count;}
bool store_event_at(unsigned i,uint16_t *event)
{
    uint8_t slot[32];
    if (i>=count || !event || !nor_read(recent[i].addr,slot,sizeof(slot)) ||
        !valid(EVENT_DATA,slot,2)) return false;
    memcpy(event,slot+12,2); return true;
}
