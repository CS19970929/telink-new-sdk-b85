#ifndef CT_MODBUS_BRIDGE_H
#define CT_MODBUS_BRIDGE_H

#include <stdint.h>

void CtModbusBridge_Init(void);
void CtModbusBridge_FeedPcByte(uint8_t byte);
void CtModbusBridge_Task(void);

#endif
