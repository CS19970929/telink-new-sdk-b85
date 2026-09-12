#ifndef DVC1124_COMMANDS_H_
#define DVC1124_COMMANDS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DVC1124 command/special-write API.
 *
 * These operations must not use the generic persistent RMW helpers because
 * their registers have W0C or self-clearing command semantics.
 */

/* 0x00 ALARM is W0C: every bit set in flag_mask is requested to clear. */
uint8_t DVC1124_ClearAlarmFlags(uint8_t flag_mask);

/* 0x55 CAMZ starts one CADC manual calibration and then self-clears. */
uint8_t DVC1124_StartCadcCalibration(void);

#ifdef __cplusplus
}
#endif

#endif /* DVC1124_COMMANDS_H_ */
