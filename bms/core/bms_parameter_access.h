#pragma once
#include "tl_common.h"
/* Shared parameter protocol v2, retaining the original D008 interface magic.
 * This is an interface signature, not a product ID. Frames stay <= 20 bytes. */
#define BMS_PARAMETER_INTERFACE_MAGIC 0xD008u
int bms_parameter_readable(u16 reg);
u16 bms_parameter_read(u16 reg);
u8 bms_parameter_write(u16 reg, u16 qty, const u8 *data);
u8 bms_reset_software_parameters(void);
u8 bms_reset_afe_parameters(void);
