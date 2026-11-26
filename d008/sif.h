#pragma once
#include "device.h"
/* 调用者提供 64 字节，type 是 A1.3.3 的报文标识。 */
unsigned sif_frame(const Device *d,bool lishen,uint8_t type,uint8_t *p);
