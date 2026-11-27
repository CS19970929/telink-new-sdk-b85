#include "store.h"
#include <string.h>

enum { SECTOR = 4096, EVENTS = 100, COMMIT = 0x3c };
static const uint32_t base[] = {0x40000,0x44000,0x4c000};
static const uint16_t stride[] = {512,64,32};
static const uint8_t format[] = {CONFIG_FORMAT_VERSION,SOC_FORMAT_VERSION,LOG_FORMAT_VERSION};
typedef struct {
    uint32_t sequence, latest, pending;
    uint16_t next[2], bytes;
    bool ready;
} Journal;
static Journal journal[DOMAIN_COUNT];
bool store_deferred;
static struct {uint32_t seq, addr;} recent[EVENTS];
static unsigned count;
static uint32_t log_epoch, log_update, log_factory;
static bool log_degraded;

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
static bool valid(Domain d,const uint8_t *p,unsigned size)
{
    return p[0]=='D' && p[1]=='8' && p[2]==format[d] && p[3]==d &&
        p[4]==(size&255u) && p[5]==(size>>8) && p[stride[d]-1]==COMMIT &&
        get32(p+12+size)==crc(p,12+size);
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

static void index_log(const uint8_t *payload,uint32_t seq,uint32_t address)
{
    uint32_t epoch=get32(payload);
    if ((int32_t)(epoch-log_epoch)>0) {
        count=0;
        log_epoch=epoch;
    }
    if (epoch==log_epoch && (payload[12] || payload[13])) index_event(seq,address);
}

static void encode(Domain d,const void *data,unsigned size,uint32_t sequence,uint8_t *slot)
{
    memset(slot,0xff,stride[d]);
    slot[0]='D'; slot[1]='8'; slot[2]=format[d]; slot[3]=d;
    slot[4]=size; slot[5]=size>>8;
    put32(slot+8,sequence); memcpy(slot+12,data,size);
    put32(slot+12+size,crc(slot,12+size));
}

static StoreResult scan(Domain d,void *latest,unsigned size,const Config *assembly)
{
    uint8_t slot[512];
    if ((unsigned)d>=DOMAIN_COUNT || !size || size>(unsigned)stride[d]-17) return STORE_BAD;
    Journal *j=&journal[d];
    memset(j,0,sizeof(*j)); j->bytes=size;
    if (d==EVENT_DATA) {
        count=0;
        log_epoch=log_update=log_factory=0;
    }
    bool dirty=false, conflict=false;
    for (unsigned sector=0;sector<2;++sector) {
        for (unsigned off=0;off<SECTOR;off+=stride[d]) {
            uint32_t address=base[d]+sector*SECTOR+off;
            if (!nor_read(address,slot,stride[d])) {
                if (d==EVENT_DATA) count=0;
                return STORE_IO_ERROR;
            }
            if (blank(slot,stride[d])) continue;
            dirty=true; j->next[sector]=off+stride[d];
            if (!valid(d,slot,size)) continue;
            if (assembly) {
                Config candidate;
                memcpy(&candidate,slot+12,sizeof(candidate));
                if (candidate.cells!=assembly->cells || candidate.nmc!=assembly->nmc) {
                    conflict=true;
                    continue;
                }
                Config final=candidate;
                config_apply_updates(&final);
                if (!config_valid(&final)) {
                    if (config_valid(&candidate)) conflict=true;
                    continue;
                }
            }
            uint32_t seq=get32(slot+8);
            if (d==EVENT_DATA) index_log(slot+12,seq,address);
            if (!j->latest || (int32_t)(seq-j->sequence)>0) {
                j->latest=address; j->sequence=seq;
                if (d==EVENT_DATA) {
                    log_update=get32(slot+16);
                    log_factory=get32(slot+20);
                }
                if (latest) memcpy(latest,slot+12,size);
            }
        }
    }
    if (conflict) return STORE_CONFLICT;
    j->ready=true;
    return j->latest ? STORE_VALID : dirty ? STORE_BAD : STORE_BLANK;
}

StoreResult store_open(Domain d,void *latest,unsigned size)
{
    return scan(d,latest,size,NULL);
}

StoreResult store_open_config(Config *latest,const Config *assembly)
{
    return scan(CONFIG_DATA,latest,sizeof(*latest),assembly);
}

bool store_available(Domain d)
{
    return journal[d].ready;
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
    if (d==EVENT_DATA) {
        index_log(slot+12,j->sequence,address);
        log_update=get32(slot+16); log_factory=get32(slot+20);
    }
    return true;
}

static bool recycle_sector(Journal *j,unsigned sector,uint32_t address)
{
    bool empty;
    if (!nor_erase(address)) {
        if (!store_deferred) j->ready=false;
        return false;
    }
    if (!inspect_sector(address,&empty)) {j->ready=false;return false;}
    if (!empty) {j->ready=false;return false;}
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
    uint8_t slot[512],verify[512];
    store_deferred=false;
    if ((unsigned)d>=DOMAIN_COUNT || !data) return false;
    Journal *j=&journal[d];
    if (!j->ready || size!=j->bytes || (d==EVENT_DATA && size!=LOG_PAYLOAD_BYTES)) return false;
    if (d!=EVENT_DATA && j->latest) {
        if (!nor_read(j->latest,verify,stride[d])) {j->ready=false;return false;}
        if (!valid(d,verify,size) || get32(verify+8)!=j->sequence) {
            j->ready=false;
            return false;
        }
        if (!memcmp(verify+12,data,size)) return true;
    }
    encode(d,data,size,j->sequence+1u,slot);
    unsigned body=16+size;
    unsigned first=body>256?256:body;
    if (j->pending) {
        if (!nor_read(j->pending,verify,stride[d])) {j->ready=false;return false;}
        if (!memcmp(slot,verify,stride[d])) return commit_body(d,slot,verify);
        if (first<body && !memcmp(slot,verify,first) && blank(verify+first,stride[d]-first)) {
            (void)nor_write(j->pending+first,slot+first,body-first);
            if (!nor_read(j->pending,verify,stride[d])) {j->ready=false;return false;}
            if (!memcmp(slot,verify,stride[d])) return commit_body(d,slot,verify);
            if (!blank(verify+first,stride[d]-first)) j->pending=0;
            return false;
        }
        j->pending=0;
    }
    unsigned sector=j->latest >= base[d]+SECTOR;
    if (j->next[sector]>=SECTOR) {
        sector^=1;
        if (j->next[sector]>=SECTOR) {
            uint32_t erase=base[d]+sector*SECTOR;
            if (!recycle_sector(j,sector,erase)) return false;
            if (d==EVENT_DATA) {
                unsigned keep=0;
                for (unsigned i=0;i<count;++i)
                    if (recent[i].addr/SECTOR!=erase/SECTOR) recent[keep++]=recent[i];
                log_degraded|=keep<count;
                count=keep;
            }
        }
    }
    uint32_t addr=base[d]+sector*SECTOR+j->next[sector];
    if (!nor_read(addr,verify,stride[d])) {j->ready=false;return false;}
    if (!blank(verify,stride[d])) {j->ready=false;return false;}
    /* 跨页时记住已回读的第一页，BLE 暂缓不消耗新槽。 */
    (void)nor_write(addr,slot,first);
    if (!nor_read(addr,verify,stride[d])) {j->ready=false;return false;}
    if (blank(verify,stride[d])) return false;
    j->next[sector]+=stride[d];
    if (memcmp(slot,verify,first) || !blank(verify+first,stride[d]-first)) return false;
    if (first<body) {
        j->pending=addr;
        (void)nor_write(addr+first,slot+first,body-first);
        if (!nor_read(addr,verify,stride[d])) {j->ready=false;return false;}
        if (memcmp(slot,verify,stride[d])) {
            if (!blank(verify+first,stride[d]-first)) j->pending=0;
            return false;
        }
    }
    j->pending=addr;
    return commit_body(d,slot,verify);
}

StoreResult store_open_log(void)
{
    return store_open(EVENT_DATA,NULL,LOG_PAYLOAD_BYTES);
}

static bool write_log(uint32_t epoch,uint32_t update,uint32_t factory,uint16_t event)
{
    uint8_t payload[LOG_PAYLOAD_BYTES];
    put32(payload,epoch); put32(payload+4,update); put32(payload+8,factory);
    payload[12]=(uint8_t)event; payload[13]=event>>8;
    return store_append(EVENT_DATA,payload,sizeof(payload));
}

bool store_log_append(uint16_t event)
{
    return event && write_log(log_epoch,log_update,log_factory,event);
}

bool store_log_clear(uint32_t update_id,uint32_t factory_id)
{
    if (!journal[EVENT_DATA].ready) return false;
    if ((update_id || factory_id) && update_id<=log_update && factory_id<=log_factory) return true;
    return write_log(journal[EVENT_DATA].sequence+1u,update_id>log_update?update_id:log_update,
                     factory_id>log_factory?factory_id:log_factory,0);
}

uint32_t store_log_update_id(void) {return log_update;}
uint32_t store_log_factory_id(void) {return log_factory;}
bool store_log_degraded(void) {return log_degraded;}
unsigned store_events(void) {return journal[EVENT_DATA].ready?count:0;}
bool store_event_at(unsigned i,uint16_t *event)
{
    uint8_t slot[32];
    if (i>=store_events() || !event || !nor_read(recent[i].addr,slot,sizeof(slot)) ||
        !valid(EVENT_DATA,slot,LOG_PAYLOAD_BYTES) || get32(slot+12)!=log_epoch) return false;
    *event=(uint16_t)(slot[24]|slot[25]<<8);
    return true;
}
