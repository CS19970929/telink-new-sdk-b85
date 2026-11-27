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

static SocWriteResult soc_result=SOC_WRITE_DONE;
static unsigned soc_attempts;

SocWriteResult device_set_soc(unsigned percent,uint32_t ms)
{
    (void)ms;
    ++soc_attempts;
    if (soc_result==SOC_WRITE_DONE) device.soc.state.percent=percent;
    return soc_result;
}
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

    /* Classic 和 Android 用同一 24 字读取。每字大端，Build ID 的低字在前。 */
    uint8_t build[]={1,3,0x2a,0,0,24,0,0};
    assert(request(&session,build,6,reply,0)==53 && reply[2]==48 && rtu_crc(reply,53)==0);
    assert(reply[3]==0x44 && reply[4]==0x47 && reply[5]==0 && reply[6]==1);
    assert(reply[7]==0 && reply[8]==0 && reply[29]==0 && reply[30]==0x0c);
    assert(!memcmp(reply+47,(uint8_t[]){0xab,0xcd,0x12,0x34},4));
    build[5]=25;
    assert(request(&session,build,6,reply,0)==5 && reply[1]==0x83 && reply[2]==2);
    uint8_t write_build[]={1,6,0x2a,0x16,0,0,0,0};
    assert(request(&session,write_build,6,reply,0)==5 && reply[1]==0x86 && reply[2]==2);
    build[5]=24;
    assert(request(&session,build,6,reply,0)==53 &&
           !memcmp(reply+47,(uint8_t[]){0xab,0xcd,0x12,0x34},4));

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
    RtuSession other={0};
    uint8_t set[]={1,6,0x10,5,0,37,0,0};
    uint8_t read[]={1,3,0x10,5,0,1,0,0};
    uint8_t set_group[]={1,16,0x10,5,0,1,2,0,100,0,0};
    device.soc.state.percent=60;
    soc_result=SOC_WRITE_DEFERRED;
    uint32_t start=UINT32_MAX-5;
    assert(request(&session,set,6,reply,start)==RTU_PENDING && soc_attempts==1);
    assert(request(&session,set,6,reply,start+9)==RTU_PENDING && soc_attempts==1);
    assert(request(&other,set_group,9,reply,start+9)==5 && reply[2]==4 && soc_attempts==1);
    assert(request(&other,read,6,reply,start+9)==7 && reply[4]==60);
    soc_result=SOC_WRITE_DONE;
    assert(request(&session,set,6,reply,start+10)==8 && !memcmp(set,reply,8));
    assert(soc_attempts==2 && request(&other,read,6,reply,start+11)==7 && reply[4]==37);
    assert(request(&session,set_group,9,reply,40)==8 && !memcmp(set_group,reply,6));
    assert(device.soc.state.percent==100);
    set[5]=0;
    assert(request(&session,set,6,reply,41)==8 && device.soc.state.percent==0);
    set[5]=101;
    assert(request(&session,set,6,reply,42)==5 && reply[2]==3 && device.soc.state.percent==0);
    set[0]=set_group[0]=0;
    set[5]=25;
    assert(!request(&session,set,6,reply,43));
    assert(!request(&session,set_group,9,reply,43));
    assert(device.soc.state.percent==0);
    set[0]=1;
    soc_result=SOC_WRITE_DEFERRED;
    assert(request(&session,set,6,reply,100)==RTU_PENDING);
    unsigned attempts=soc_attempts;
    assert(request(&session,set,6,reply,1100)==5 && reply[2]==4 && soc_attempts==attempts);
    assert(device.soc.state.percent==0);
    assert(request(&session,set,6,reply,1101)==RTU_PENDING);
    rtu_reset(&session);
    soc_result=SOC_WRITE_DONE;
    assert(request(&other,set,6,reply,1102)==8 && device.soc.state.percent==25);
    soc_result=SOC_WRITE_FAILED;
    set[5]=82;
    assert(request(&other,set,6,reply,1103)==5 && reply[2]==4 && device.soc.state.percent==25);
    puts("PASS D008 commands: read-only Build ID, legacy sleep, SOC echo/readback, retry, timeout, disconnect and broadcast");
}
