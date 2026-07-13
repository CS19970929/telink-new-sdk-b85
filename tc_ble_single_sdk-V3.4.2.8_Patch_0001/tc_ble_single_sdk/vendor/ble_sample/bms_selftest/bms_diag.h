#ifndef BMS_DIAG_H_
#define BMS_DIAG_H_

#include "tl_common.h"

#define BMS_DIAG_REG_BASE  0xd1e0u
#define BMS_DIAG_REG_COUNT 32u

u8 BMS_Diag_ReadReg(u16 reg, u16 *value);

#endif
