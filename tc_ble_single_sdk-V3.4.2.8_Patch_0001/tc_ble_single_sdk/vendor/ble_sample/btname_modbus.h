#ifndef _BTNAME_MODBUS_H_
#define _BTNAME_MODBUS_H_

#include <stdint.h>

#define BTNAME_REG_COUNT        12

#ifndef BTNAME_PREFIX
#define BTNAME_PREFIX           "BT_"
#endif

#ifndef BTNAME_TOTAL_MAX_LEN
#define BTNAME_TOTAL_MAX_LEN    25u
#endif

#ifndef BTNAME_REG_BASE
#define BTNAME_REG_BASE         0x0100u
#endif
#ifndef BTNAME_REG_WORDS
#define BTNAME_REG_WORDS        16u
#endif

#ifndef BTNAME_SUFFIX_STRICT
#define BTNAME_SUFFIX_STRICT    1u
#endif

#define BTNAME_PREFIX_LEN       3u

#if (BTNAME_TOTAL_MAX_LEN <= BTNAME_PREFIX_LEN)
#error "BTNAME_TOTAL_MAX_LEN must be > 3"
#endif

#define BTNAME_SUFFIX_MAX_LEN   (BTNAME_TOTAL_MAX_LEN - BTNAME_PREFIX_LEN)

void btname_init(void);
const char* btname_get(void);
int btname_modbus_on_write_holding(uint16_t addr, uint16_t qty, const uint16_t *regs);

#endif
