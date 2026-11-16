#ifndef CT_APP_H
#define CT_APP_H

#include <stdint.h>
#include "ct_protocol.h"

void CtApp_Init(void);
void CtApp_Poll(void);
void CtApp_HandleFrame(const CtFrame *frame);

#endif
