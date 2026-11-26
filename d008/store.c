#include "store.h"
#include <string.h>

enum { SECTOR = 4096, EVENTS = 100, COMMIT = 0x3c };
static const uint32_t base[] = {0x40000,0x44000,0x4c000};
static const uint16_t stride[] = {256,64,32};
typedef struct {
    uint32_t sequence, latest;
    uint16_t next[2], bytes;
    bool ready;
} Journal;
static Journal journal[DOMAIN_COUNT];
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
static bool valid(Domain d,const uint8_t *p,unsigned size)
{
    return p[0]=='D' && p[1]=='8' && p[2]==1 && p[3]==d && p[4]==size &&
        p[stride[d]-1]==COMMIT && get32(p+12+size)==crc(p,12+size);
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
    slot[0]='D'; slot[1]='8'; slot[2]=1; slot[3]=d; slot[4]=size;
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

static StoreResult scan(Domain d,void *latest,unsigned size,const uint8_t *factory)
{
    uint8_t slot[256];
    if ((unsigned)d>=DOMAIN_COUNT || !size || size>(unsigned)stride[d]-17) return STORE_BAD;
    Journal *j=&journal[d];
    memset(j,0,sizeof(*j)); j->bytes=size;
    if (d==EVENT_DATA) count=0;
    bool dirty=false, first_only=factory!=NULL;
    for (unsigned sector=0;sector<2;++sector) {
        for (unsigned off=0;off<SECTOR;off+=stride[d]) {
            uint32_t address=base[d]+sector*SECTOR+off;
            if (!nor_read(address,slot,stride[d])) return STORE_BAD;
            if (blank(slot,stride[d])) continue;
            dirty=true; j->next[sector]=off+stride[d];
            if (!valid(d,slot,size)) {
                if (first_only && !partial_factory(slot,factory)) first_only=false;
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
    if (dirty && first_only) return STORE_INITIAL;
    return dirty ? STORE_BAD : STORE_BLANK;
}

StoreResult store_open(Domain d,void *latest,unsigned size)
{
    return scan(d,latest,size,NULL);
}

StoreResult store_open_config(void *latest,const void *defaults,unsigned size)
{
    uint8_t factory[256];
    if (!defaults || !size || size>(unsigned)stride[CONFIG_DATA]-17) return STORE_BAD;
    encode(CONFIG_DATA,defaults,size,1,factory);
    return scan(CONFIG_DATA,latest,size,factory);
}

bool store_append(Domain d,const void *data,unsigned size)
{
    uint8_t slot[256],verify[256];
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
    unsigned sector=j->latest >= base[d]+SECTOR;
    if (j->next[sector]>=SECTOR) {
        sector^=1;
        if (j->next[sector]) {
            uint32_t erase=base[d]+sector*SECTOR;
            if (d==EVENT_DATA)
                for (unsigned i=0;i<count;++i)
                    if (recent[i].addr/SECTOR==erase/SECTOR) return false;
            if (!nor_erase(erase)) return false;
            for (unsigned off=0;off<SECTOR;off+=sizeof(verify))
                if (!nor_read(erase+off,verify,sizeof(verify)) || !blank(verify,sizeof(verify)))
                    return false;
        }
        j->next[sector]=0;
    }
    uint32_t addr=base[d]+sector*SECTOR+j->next[sector];
    encode(d,data,size,j->sequence+1u,slot);
    if (!nor_read(addr,verify,stride[d]) || !blank(verify,stride[d])) return false;
    /* 未获准写入时保留空槽。若部分编程则跳过该槽，复位扫描也会跳过。 */
    if (!nor_write(addr,slot,16+size) || !nor_read(addr,verify,16+size) ||
        memcmp(slot,verify,16+size)) {
        if (!nor_read(addr,verify,stride[d])) j->ready=false;
        else if (!blank(verify,stride[d])) j->next[sector]+=stride[d];
        return false;
    }
    j->next[sector]+=stride[d];
    slot[stride[d]-1]=COMMIT;
    /* 即使写接口返回失败，也以整条读回判定是否已提交。 */
    nor_write(addr+stride[d]-1,slot+stride[d]-1,1);
    if (!nor_read(addr,verify,stride[d])) {j->ready=false;return false;}
    if (memcmp(slot,verify,stride[d])) return false;
    j->latest=addr; ++j->sequence;
    if (d==EVENT_DATA) index_event(j->sequence,addr);
    return true;
}

unsigned store_events(void) {return count;}
bool store_event_at(unsigned i,uint16_t *event)
{
    uint8_t slot[32];
    if (i>=count || !event || !nor_read(recent[i].addr,slot,sizeof(slot)) ||
        !valid(EVENT_DATA,slot,2)) return false;
    memcpy(event,slot+12,2); return true;
}
