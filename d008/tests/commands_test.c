#include "../registers.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

Device device;
uint8_t device_address[6];
static unsigned shutdown_requests;
static uint32_t shutdown_ms;
static bool shutdown_allowed=true;

bool device_request_shutdown(uint32_t ms)
{
    if (!shutdown_allowed) return false;
    ++shutdown_requests;
    shutdown_ms=ms;
    return true;
}

bool device_set_soc(unsigned percent,uint32_t ms) {(void)percent;(void)ms;return false;}
bool device_set_config(const Config *config) {(void)config;return false;}
bool device_set_allowed(uint8_t allowed,uint32_t ms) {(void)allowed;(void)ms;return false;}
uint16_t device_mos_status(void) {return 0;}
unsigned store_events(void) {return 0;}
bool store_event_at(unsigned index,uint16_t *event) {(void)index;(void)event;return false;}

static unsigned request(RtuSession *session,uint8_t *frame,unsigned bytes,uint8_t *reply,uint32_t ms)
{
    uint16_t crc=rtu_crc(frame,bytes);
    frame[bytes]=(uint8_t)crc;
    frame[bytes+1]=crc>>8;
    return rtu_channel(session,frame,bytes+2,reply,ms);
}

int main(void)
{
    RtuSession session={0};
    uint8_t reply[256];
    uint8_t sleep[]={1,6,0x11,2,0,10,0,0};
    uint8_t group[]={1,16,0x11,2,0,1,2,0,10,0,0};
    assert(request(&session,sleep,6,reply,125)==8 && !memcmp(sleep,reply,8));
    assert(shutdown_requests==1 && shutdown_ms==125);
    assert(request(&session,group,9,reply,251)==8 && !memcmp(group,reply,6));
    assert(shutdown_requests==2 && shutdown_ms==251);

    sleep[0]=group[0]=0;
    assert(!request(&session,sleep,6,reply,300));
    assert(!request(&session,group,9,reply,300));
    assert(shutdown_requests==2);
    sleep[0]=1;
    sleep[5]=9;
    assert(request(&session,sleep,6,reply,301)==5 && reply[1]==0x86 && reply[2]==2);
    sleep[5]=10;
    assert(!rtu_channel(&session,sleep,sizeof(sleep),reply,302)); /* 上一帧 CRC 与命令值不符。 */
    assert(shutdown_requests==2);
    shutdown_allowed=false;
    assert(request(&session,sleep,6,reply,303)==5 && reply[2]==4);
    assert(shutdown_requests==2);
    puts("PASS D008 commands: legacy unicast sleep, echo, CRC, broadcast and busy rejection");
}
