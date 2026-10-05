/* 文件功能：SH3673510 NTC 查表数据；电阻/温度编码供采样与保护阈值换算共同使用。
 * bms/afe/sh3673510/sh3673510_ntc.h；实际编译归属见各产品 sources.txt。
 */
#ifndef SH3673510_NTC_H_
#define SH3673510_NTC_H_
#include <stdint.h>
/* Existing SH product calibration: resistance / 100 ohm, (C + 40) * 10.
 * Shared by ADC conversion, AFE reporting and threshold quantization only. */
extern const uint16_t sh3673510_ntc_10k[60];
#endif
