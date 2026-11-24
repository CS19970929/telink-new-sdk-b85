"""读取生产 20-word 休眠快照，观察不得推进状态或计时。"""
from validation_support import run_c

run_c(r'''
#include <assert.h>
#include <stdint.h>
#include "bms_sleep_status.h"
static uint32_t now;
uint32_t bms_time_now_32k(void) { return now; }
static uint16_t word(const uint8_t *data, unsigned index)
{ return (uint16_t)(((uint16_t)data[index * 2u] << 8) | data[index * 2u + 1u]); }
static uint32_t value(const uint8_t *data, unsigned index)
{ return (uint32_t)word(data, index) | ((uint32_t)word(data, index + 1u) << 16); }
int main(void)
{
    uint8_t data[40];
    now = UINT32_MAX - 15u;
    assert(bms_sleep_status_read(BMS_SLEEP_STATUS_BASE, 20u, data));
    assert(word(data, 0) == 0x534Cu && word(data, 1) == 1u && word(data, 2) == BMS_SLEEP_STATE_INIT);
    bms_sleep_status_publish(BMS_SLEEP_REASON_LOW, 0u, 12000u, 60000u, 0u, 1u);
    now = 32u;
    assert(bms_sleep_status_read(BMS_SLEEP_STATUS_BASE, 20u, data));
    assert(word(data, 2) == BMS_SLEEP_STATE_COUNTING && word(data, 3) == BMS_SLEEP_REASON_LOW);
    assert(value(data, 8) == 12000u && value(data, 10) == 60000u && value(data, 12) == 48000u);
    assert(value(data, 16) == UINT32_MAX - 15u && value(data, 18) == now);
    for (unsigned i = 0u; i < 50u; ++i) {
        now += 200u;
        assert(bms_sleep_status_read(BMS_SLEEP_STATUS_BASE, 20u, data));
        assert(value(data, 12) == 48000u && value(data, 18) == now);
    }
    bms_sleep_status_publish(BMS_SLEEP_REASON_COMMAND, BMS_SLEEP_BLOCK_UART_TX, 500u, 500u, 3000u, 0u);
    bms_sleep_status_committed();
    assert(bms_sleep_status_read(BMS_SLEEP_STATUS_BASE, 20u, data));
    assert(word(data, 2) == BMS_SLEEP_STATE_COMMITTED && value(data, 6) == 0u && value(data, 14) == 0u);
    assert(!bms_sleep_status_read(BMS_SLEEP_STATUS_END - 1u, 2u, data));
    assert(!bms_sleep_status_read(65535u, 2u, data));
    return 0;
}
''', ["bms/core/bms_sleep_status.c"], name="sleep-status")
