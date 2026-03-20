#ifndef _RUNTIME_H_
#define _RUNTIME_H_

#include "tl_common.h"

#define FACTORY_TIME_LIMIT_MIN   (60 * 24 * 7)   // 7Ìì
// #define FACTORY_TIME_LIMIT_MIN   (1)   // 7Ìì

typedef enum
{
    MODE_FACTORY = 0,
    MODE_NORMAL
}bms_mode_t;

void Runtime_Init(void);
void Runtime_1MinTask(void);
bms_mode_t Runtime_GetMode(void);
u32 Runtime_Get_runtime(void);

#endif