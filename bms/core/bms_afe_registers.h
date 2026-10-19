/* 原厂寄存器目录的受控开发接口；不进入 Config/State/Event。 */
#ifndef BMS_AFE_REGISTERS_H_
#define BMS_AFE_REGISTERS_H_
#include "bms_afe_backend.h"
#include <stdint.h>
#ifndef BMS_AFE_REGISTER_DEBUG_ENABLE
#define BMS_AFE_REGISTER_DEBUG_ENABLE 0
#endif
#if BMS_AFE_REGISTER_DEBUG_ENABLE != 0 && BMS_AFE_REGISTER_DEBUG_ENABLE != 1
#error "BMS_AFE_REGISTER_DEBUG_ENABLE must be 0 or 1"
#endif
#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD && BMS_AFE_REGISTER_DEBUG_ENABLE
#error "Production firmware must disable AFE register debugging"
#endif
/* 固定字段源码预设；保护字段仍由独立 profile 唯一管理。 */
#ifndef BMS_AFE_REGISTER_PRESET_COUNT
#define BMS_AFE_REGISTER_PRESET_COUNT 0u
#endif
#if BMS_AFE_REGISTER_PRESET_COUNT > 145u
#error "Too many AFE source register presets"
#endif
#if BMS_AFE_REGISTER_PRESET_COUNT
uint8_t bms_afe_register_config_apply(void);
#else
static inline uint8_t bms_afe_register_config_apply(void) { return 1u; }
#endif
/* INFO=0x10；其它子命令由原 0x42 会话校验令牌后分派。 */
#if BMS_AFE_REGISTER_DEBUG_ENABLE
uint8_t bms_afe_register_command(const uint8_t *request, uint32_t length,
                               uint8_t *payload, uint8_t *payload_length);
/* Guard 拥有调试冻结，断连/超时保持关闭，显式恢复后重新获取完整测量资格。 */
uint8_t bms_afe_register_debug_held(void);
uint8_t bms_afe_register_debug_begin(void);
uint8_t bms_afe_register_debug_restore(void);
uint8_t bms_afe_register_bus_access_allowed(void);
#else
#define bms_afe_register_debug_held() 0u
static inline uint8_t bms_afe_register_command(const uint8_t *request, uint32_t length,
    uint8_t *payload, uint8_t *payload_length)
{
    (void)request; (void)length; (void)payload;
    if (payload_length) *payload_length = 0u;
    return 3u;
}
#endif
#endif
