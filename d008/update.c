#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "update.h"
#include "link.h"
#include "registers.h"

#define SLOT_BYTES 0x1f000u
enum { IDLE, ERASING, RECEIVING, VERIFYING, READY, COMMITTED, FAILED };
static struct {
    unsigned state, error;
    uint32_t id, base, size, build, descriptor, trailer, next, position, crc;
    uint32_t start_ms,last_ms;
} transfer;
static const volatile struct {
    char magic[8]; uint8_t version,product; uint16_t layout; uint32_t build,limit;
} identity={{'B','M','S','U','8','2','5','1'},1,8,1,D008_BUILD,SLOT_BYTES};

static uint32_t get32(const uint8_t *p)
{
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t v)
{
    for (unsigned i=0;i<4;++i) {p[i]=v; v>>=8;}
}
static uint32_t flash_word(uint32_t addr)
{
    uint8_t p[4];
    flash_read_page(addr,4,p);
    return get32(p);
}
bool uart_update_busy(void) {return transfer.state>IDLE && transfer.state<FAILED;}
static void end(unsigned error)
{
    update_lock(true);
    transfer.state=error?FAILED:IDLE; transfer.error=error;
}
static bool program(uint32_t addr,const uint8_t *p,unsigned n)
{
    uint8_t check[128];
    if (addr<transfer.base || n>128 || addr-transfer.base>SLOT_BYTES-n) return false;
    while (n) {
        unsigned part=256u-(addr&255u);
        if (part>n) part=n;
        flash_write_page(addr,part,(uint8_t *)p); flash_read_page(addr,part,check);
        if (memcmp(check,p,part)) return false;
        addr+=part; p+=part; n-=part;
    }
    return true;
}
static unsigned begin(const uint8_t *p,uint32_t id,uint32_t ms)
{
    uint32_t size=get32(p+1), desc=get32(p+9);
    if (!id || p[0]!=identity.product || size<64 || size>SLOT_BYTES || desc>size-24) return 5;
    if (update_active() || blc_ll_getCurrentState()==BLS_LINK_STATE_CONN ||
        !update_layout() || !device.valid || !device_save_soc()) return 2;
    memset(&transfer,0,sizeof(transfer));
    transfer.id=id; transfer.base=ota_program_offset; transfer.size=size;
    transfer.build=get32(p+5); transfer.descriptor=desc; transfer.trailer=get32(p+13);
    transfer.start_ms=transfer.last_ms=ms;
    transfer.state=ERASING; update_lock(false); return 0;
}
static unsigned data(const uint8_t *p,unsigned n)
{
    uint8_t buffer[128],check[128];
    uint32_t offset=get32(p);
    n-=4; p+=4;
    if (transfer.state!=RECEIVING || offset>transfer.next || n>transfer.size-offset) return 4;
    if (!offset && (n<32 || get32(p+8)!=0x544c4e4b || get32(p+24)!=transfer.size)) return 5;
    memcpy(buffer,p,n);
    if (offset<=8 && 8-offset<n) buffer[8-offset]=255;
    if (offset<transfer.next) {
        if (n>transfer.next-offset) return 4;
        flash_read_page(transfer.base+offset,n,check);
        return memcmp(check,buffer,n)?5:0;
    }
    if (!program(transfer.base+offset,buffer,n)) {end(6);return 6;}
    transfer.next+=n; return 0;
}
static unsigned commit(uint32_t ms)
{
    if (transfer.state!=READY || !update_layout()) return 4;
    uint8_t flag=0x4b, check;
    if (!program(transfer.base+8,&flag,1) || flash_word(transfer.base+8)!=0x544c4e4b) {
        end(6); return 6;
    }
    uint32_t old=(transfer.base?0:0x20000)+8;
    flag=0; flash_write_page(old,1,&flag); flash_read_page(old,1,&check);
    if (check) {end(6);return 6;}
    update_lock(true); transfer.state=COMMITTED; transfer.last_ms=ms;
    return 0;
}

unsigned uart_update_frame(const uint8_t *p,unsigned n,uint8_t *out,uint32_t ms)
{
    if (n<11 || n>143 || p[0]!=1 || p[1]!=0x46 || rtu_crc(p,n)) return 0;
    unsigned cmd=p[2],payload=n-11,error=0;
    uint32_t id=get32(p+5);
    if (cmd==0 && !payload) {
        if (id && id!=transfer.id) error=3;
        else if (id && uart_update_busy() && transfer.state!=COMMITTED) transfer.last_ms=ms;
        if (!error && transfer.state==FAILED) error=transfer.error;
    } else if (cmd==1 && payload==17) error=begin(p+9,id,ms);
    else if (!id || id!=transfer.id) error=3;
    else if (cmd==2 && payload>=5 && payload<=132) error=data(p+9,payload);
    else if (cmd==3 && !payload) {
        if (transfer.state!=RECEIVING || transfer.next!=transfer.size) error=4;
        else {transfer.state=VERIFYING; transfer.position=0; transfer.crc=UINT32_MAX;}
    } else if (cmd==4 && !payload) error=commit(ms);
    else if (cmd==5 && !payload && uart_update_busy() && transfer.state!=COMMITTED) end(0);
    else error=1;
    if (!error && cmd && transfer.state!=COMMITTED) transfer.last_ms=ms;
    memcpy(out,p,5); out[5]=transfer.state; out[6]=error; out[7]=identity.product;
    put32(out+8,transfer.id); put32(out+12,transfer.next); put32(out+16,transfer.size);
    uint32_t running=ota_program_offset?0:0x20000, size=flash_word(running+24);
    put32(out+20,identity.build); put32(out+24,size>=64 && size<=SLOT_BYTES?flash_word(running+size-4):0);
    unsigned crc=rtu_crc(out,28); out[28]=crc; out[29]=crc>>8;
    return 30;
}
void uart_update_poll(uint32_t ms)
{
    uint8_t block[128];
    if (!uart_update_busy()) return;
    if (transfer.state==COMMITTED) {
        if (ms-transfer.last_ms>=1000 && !uart_tx_is_busy()) start_reboot();
        return;
    }
    if (ms-transfer.start_ms>=600000 || ms-transfer.last_ms>=15000) {end(7);return;}
    if (!device.valid || blc_ll_getCurrentState()==BLS_LINK_STATE_CONN) {end(2);return;}
    if (transfer.state==ERASING) {
        uint32_t address=transfer.base+transfer.position;
        flash_erase_sector(address);
        for (unsigned i=0;i<4096;i+=sizeof(block)) {
            flash_read_page(address+i,sizeof(block),block);
            for (unsigned j=0;j<sizeof(block);++j) if (block[j]!=255) {end(6);return;}
        }
        transfer.position+=4096;
        if (transfer.position==SLOT_BYTES) transfer.state=RECEIVING;
    } else if (transfer.state==VERIFYING) {
        unsigned n=transfer.size-transfer.position;
        if (n>sizeof(block)) n=sizeof(block);
        flash_read_page(transfer.base+transfer.position,n,block);
        if (!transfer.position) block[8]=0x4b;
        for (unsigned i=0;i<n;++i) {
            transfer.crc^=block[i];
            for (unsigned b=0;b<8;++b)
                transfer.crc=(transfer.crc>>1)^((0u-(transfer.crc&1u))&0xedb88320u);
        }
        transfer.position+=n;
        if (transfer.position!=transfer.size) return;
        flash_read_page(transfer.base+transfer.descriptor,20,block);
        if (transfer.crc || memcmp(block,(const void *)&identity,12) ||
            get32(block+12)!=transfer.build || get32(block+16)!=SLOT_BYTES ||
            flash_word(transfer.base+transfer.size-4)!=transfer.trailer) end(5);
        else transfer.state=READY;
    }
}
