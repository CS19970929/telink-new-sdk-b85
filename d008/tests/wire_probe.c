/* 通信台架在 PC3 空闲时模拟仪表接入，只请求两帧。PC2 读入来自实际 GPIO。 */
#include "tl_common.h"
#include "drivers.h"
#include "../device.h"
#include "../link.h"
#include "wire_probe.h"

enum { FRAME_TICKS=758, LEVEL_WORDS=48 };
volatile bool wire_probe_requested,wire_probe_frame;
static volatile unsigned completed,ticks;
static volatile uint32_t first[2],last_tick,shortest,longest;
static uint32_t start_ms;
static uint16_t levels[2][LEVEL_WORDS];
static uint8_t frames[2][6];

unsigned wire_probe_arm(unsigned key,uint32_t ms)
{
    if (!D008_LISHEN || key!=0x5111 || wire_probe_requested || wired_busy() ||
        gpio_read(GPIO_PC3) || update_active() || !device.valid || device.allowed ||
        device.sample.fet || device.sample.ma < -1000 || device.sample.ma > 1000) return 3;
    completed=ticks=0; shortest=UINT32_MAX; longest=0; start_ms=ms;
    memset(levels,0,sizeof(levels)); memset(frames,0,sizeof(frames));
    gpio_set_input_en(GPIO_PC2,1); wire_probe_requested=true; return 0;
}
void wire_probe_expire(uint32_t ms)
{
    if (wire_probe_requested && (ms-start_ms>6000 || !device.valid || device.sample.fet ||
        update_active() || gpio_read(GPIO_PC3))) {
        u8 irq=irq_disable();
        wire_probe_requested=wire_probe_frame=false;
        irq_restore(irq); gpio_set_input_en(GPIO_PC2,0);
    }
}
void wire_probe_begin(const uint8_t *frame)
{
    memcpy(frames[completed],frame,6);
    ticks=0; wire_probe_frame=true;
}
_attribute_ram_code_ static void capture(unsigned i,bool level)
{
    if (level) levels[completed][i/16]|=(uint16_t)(1u<<(i%16));
}
_attribute_ram_code_ void wire_probe_tick(uint32_t tick,bool level)
{
    if (!ticks) first[completed]=tick;
    else {
        capture(ticks-1,level);
        uint32_t interval=tick-last_tick;
        if (interval<shortest) shortest=interval;
        if (interval>longest) longest=interval;
    }
    last_tick=tick; ++ticks;
}
_attribute_ram_code_ void wire_probe_end(bool level)
{
    if (ticks==FRAME_TICKS) capture(ticks-1,level);
    wire_probe_frame=false;
    if (++completed==2) {
        wire_probe_requested=false;
        gpio_set_input_en(GPIO_PC2,0);
    }
}
bool wire_probe_read(unsigned i,uint16_t *v)
{
    if (i==0) {*v=completed;return true;}
    if (i==1) {*v=wire_probe_requested;return true;}
    if (wire_probe_requested || completed!=2) return false;
    if (i<8) {
        uint32_t n=i<4?first[1]-first[0]:i<6?shortest:longest;
        *v=(uint16_t)(n>>(i%2?0:16));
    } else if (i<14) {
        unsigned off=i-8; const uint8_t *p=frames[off/3]+2*(off%3);
        *v=(uint16_t)(p[0]<<8|p[1]);
    } else if (i<110) *v=levels[(i-14)/LEVEL_WORDS][(i-14)%LEVEL_WORDS];
    else if (i==110) *v=ticks;
    else return false;
    return true;
}
