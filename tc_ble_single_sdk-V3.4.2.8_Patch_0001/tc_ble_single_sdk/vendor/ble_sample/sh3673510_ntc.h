#ifndef SH3673510_NTC_H_
#define SH3673510_NTC_H_
#include <stdint.h>
/* Existing SH product calibration: resistance / 100 ohm, (C + 40) * 10.
 * Shared by ADC conversion, AFE reporting and threshold quantization only. */
extern const uint16_t sh3673510_ntc_10k[60];
#endif
