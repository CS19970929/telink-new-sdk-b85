#ifndef CT_SELF_IAP_H
#define CT_SELF_IAP_H

#include <stdint.h>

void CtSelfIap_Init(void);
void CtSelfIap_FeedUartByte(uint8_t byte);
void CtSelfIap_PollCan(void);
void CtSelfIap_Task(void);

#endif
