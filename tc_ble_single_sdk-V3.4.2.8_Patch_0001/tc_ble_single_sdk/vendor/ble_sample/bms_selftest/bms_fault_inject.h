#ifndef BMS_FAULT_INJECT_H_
#define BMS_FAULT_INJECT_H_

#include "tl_common.h"

u8 BMS_FaultInject_ShouldFail(u8 fault_id);
u32 BMS_FaultInject_GetMask(void);

#endif
