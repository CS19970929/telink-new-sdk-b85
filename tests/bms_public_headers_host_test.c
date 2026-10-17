/* 仅 Core/Application include 路径；验证公共协议头和实际授权事务不需要 MCU SDK。 */
#include <assert.h>
#include <stdint.h>
#include "bms_afe_hw_access.h"
#include "bms_afe_hw_modbus.h"
#include "bms_event_log.h"
#include "bms_parameter_access.h"
#include "modbus_rtu.h"
#include "app_power.h"

static uint32_t now;
uint32_t bms_time_ms(void) { return now; }
u16 bms_afe_hw_profile_expected_model(void) { return 0x1124u; }
u8 bms_afe_hw_write_complete_frame(const u8 *frame, u32 length)
{
    (void)frame;
    (void)length;
    assert(0); /* 此场景没有提交完整硬件配置。 */
    return 1u;
}

int main(void)
{
    u8 request[] = {1u, 0x42u, 1u, 0x41u, 0x46u, 0x45u, 0x48u, 0u, 0u};
    u8 response[20];
    u32 length = 0u;
    uint16_t crc = mb_crc16(request, 7u);
    request[7] = (uint8_t)crc;
    request[8] = (uint8_t)(crc >> 8);
    now = UINT32_MAX - 100u;
    assert(bms_afe_hw_access_modbus_on_frame(request, sizeof(request), response, &length));
    assert(length == 13u && response[3] == 0u && bms_afe_hw_access_is_active());
    assert(bms_afe_hw_access_remaining_seconds() == 60u);
    now += 1000u;
    assert(bms_afe_hw_access_remaining_seconds() == 59u);
    now += 59u * 1000u;
    assert(bms_afe_hw_access_is_active() && !bms_afe_hw_access_remaining_seconds());
    now++;
    assert(!bms_afe_hw_access_is_active());
    return 0;
}
