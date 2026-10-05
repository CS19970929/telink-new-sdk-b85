/*
 * 文件功能：BLE 名称后缀校验、存储和 Modbus 写入；构造广播名称并保持既有名称长度和字
 * 符约束。
 * bms/core/btname_modbus.h；实际编译归属见各产品 sources.txt。
 */
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

/* 加载名称后缀并构造应用 BLE 名称。 */
void btname_init(void);
/* 取得当前 BLE 名称及其长度信息。 */
const char* btname_get(void);
/* 校验名称寄存器写入并保存、更新广播名称。 */
int btname_modbus_on_write_holding(uint16_t addr, uint16_t qty, const uint16_t *regs);

#endif
