/* 仅 --flash-test 台架构建。复用产品日志和 NOR 驱动，以固定偏移隔离业务页。 */
#include "tl_common.h"
#include "drivers.h"
#include "vendor/common/ble_flash.h"
#include "flash_board.h"
#include "hil.h"
#include "../link.h"

enum { IDLE, INSPECT, RUNNING, VERIFY, PASSED, REFUSED, FAILED };
enum { CFG_RECORDS=33, SOC_RECORDS=129, EVENT_RECORDS=257 };
static const uint16_t limits[]={CFG_RECORDS,SOC_RECORDS,EVENT_RECORDS};
static unsigned state,domain,done[3],failures;
static uint32_t last_ms,start_ms,elapsed_ms,missed_start,missed,max_ticks;
uint16_t flash_test_erases,flash_test_programs;

unsigned flash_test_request(unsigned key)
{
    if (key!=0xd008 || state!=IDLE || !device.valid || device.allowed || device.sample.fet ||
        device.sample.ma < -1000 || device.sample.ma > 1000) return 3;
    state=INSPECT; return 0;
}

static bool verify(void)
{
    uint32_t v=0;
    if (store_open(CONFIG_DATA,&v,4)!=STORE_VALID || v!=CFG_RECORDS ||
        store_open(SOC_DATA,&v,4)!=STORE_VALID || v!=SOC_RECORDS ||
        store_open(EVENT_DATA,NULL,2)!=STORE_VALID || store_events()!=100) return false;
    for (unsigned i=0;i<100;++i) {
        uint16_t event;
        if (!store_event_at(i,&event) || event!=EVENT_RECORDS-i) return false;
    }
    return true;
}
void flash_test_poll(uint32_t ms)
{
    if (state==IDLE || state>VERIFY || ms-last_ms<25 || update_active()) return;
    if (!device.valid || device.allowed || device.sample.fet ||
        device.sample.ma < -1000 || device.sample.ma > 1000) {state=FAILED;return;}
    last_ms=ms;
    uint32_t tick=clock_time();
    if (state==INSPECT) {
        unsigned blank=0;
        for (unsigned i=0;i<3;++i) {
            StoreResult result=store_open((Domain)i,NULL,i==EVENT_DATA?2:4);
            if (result==STORE_BLANK) ++blank;
        }
        /* 已完成的夹具可在复位后只读验收。非空且不匹配时绝不清空重跑。 */
        if (blank!=3) {state=verify()?PASSED:REFUSED;return;}
        start_ms=ms; missed_start=hil_missed; state=RUNNING;
    } else if (state==RUNNING) {
        uint32_t value=done[domain]+1;
        uint16_t event=(uint16_t)value;
        const void *p=domain==EVENT_DATA?(const void *)&event:(const void *)&value;
        unsigned size=domain==EVENT_DATA?2:4;
        if (store_append((Domain)domain,p,size)) {
            if (domain!=EVENT_DATA && !store_append((Domain)domain,p,size)) {state=FAILED;return;}
            if (++done[domain]==limits[domain] && ++domain==DOMAIN_COUNT) state=VERIFY;
        } else {
            ++failures;
            if (ms-start_ms>=120000) state=FAILED;
        }
    } else state=verify()?PASSED:FAILED;
    uint32_t dt=clock_time()-tick;
    if (dt>max_ticks) max_ticks=dt;
    elapsed_ms=ms-start_ms; missed=hil_missed-missed_start;
}

bool flash_test_read(unsigned offset,uint16_t *word)
{
    const uint16_t values[]={state,done[0],done[1],done[2],flash_test_programs,flash_test_erases,
        failures,missed,(uint16_t)(max_ticks>>16),(uint16_t)max_ticks,
        (uint16_t)(elapsed_ms>>16),(uint16_t)elapsed_ms,
        (uint16_t)(blc_flash_mid>>16),(uint16_t)blc_flash_mid,0xd008};
    if (offset>=sizeof(values)/sizeof(values[0])) return false;
    *word=values[offset]; return true;
}
