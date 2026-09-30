#pragma once
#include "tl_common.h"
/* D008 parameter protocol v2. Ordinary frames stay <= 20 bytes on MTU23. */
int bms_parameter_readable(u16 reg);
u16 bms_parameter_read(u16 reg);
u8 bms_parameter_write(u16 reg, u16 qty, const u8 *data);
u8 bms_reset_software_parameters(void);
u8 bms_reset_afe_parameters(void);
