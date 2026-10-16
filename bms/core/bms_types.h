/* 旧协议/持久化源码使用的整数类型边界；PC/其他 MCU 无需 Telink SDK。
 * TC32 的 uint32_t 是 unsigned long，Vendor u32 是 unsigned int；
 * 保持旧接口的精确 C 类型与宽度，新增业务代码优先使用 stdint.h。 */
#ifndef BMS_TYPES_H_
#define BMS_TYPES_H_
#include <stdint.h>
/* TC32 GCC 4.5 不接受重复 typedef；量产沿用唯一 Vendor 类型定义。
 * PC/其他 MCU 不包含该项目宏，公共接口仅依赖下面的标准 C 类型。 */
#if defined(__PROJECT_8258_BLE_SAMPLE__)
#include "common/types.h"
#else
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#endif
typedef char bms_u8_width_check[(sizeof(u8) == sizeof(uint8_t)) ? 1 : -1];
typedef char bms_u16_width_check[(sizeof(u16) == sizeof(uint16_t)) ? 1 : -1];
typedef char bms_u32_width_check[(sizeof(u32) == sizeof(uint32_t)) ? 1 : -1];
#endif
