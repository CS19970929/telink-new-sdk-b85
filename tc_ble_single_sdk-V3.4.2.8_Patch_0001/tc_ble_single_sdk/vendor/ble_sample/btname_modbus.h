#ifndef _BTNAME_MODBUS_H_
#define _BTNAME_MODBUS_H_

#include <stdint.h>
#include "flash_store_cfg.h"

/* ================= 用户可配置 ================= */
// btname_modbus.h 或 app_config.h
#define BTNAME_REG_COUNT        12      // 25字节名称需要13个寄存器 (13*2=26，多一个用于填充)

/* 固定一个 sector 专门存名字（4KB 对齐！必须确认不冲突） */
#ifndef BTNAME_SECTOR_ADDR
#define BTNAME_SECTOR_ADDR      FLASH_ADDR_BLE_NAME_BASE
#endif

/* 固定前缀（不可被客户修改） */
#ifndef BTNAME_PREFIX
#define BTNAME_PREFIX           "BT_"
#endif

/* 最终蓝牙名最大长度（建议 <= 20~24） */
#ifndef BTNAME_TOTAL_MAX_LEN
#define BTNAME_TOTAL_MAX_LEN    25u
#endif

/* Modbus holding register：客户写 suffix（推荐 16 words = 32 bytes） */
#ifndef BTNAME_REG_BASE
#define BTNAME_REG_BASE         0x0100u
#endif
#ifndef BTNAME_REG_WORDS
#define BTNAME_REG_WORDS        16u
#endif

/* 是否严格限制后缀字符为 [A-Za-z0-9_-] */
#ifndef BTNAME_SUFFIX_STRICT
#define BTNAME_SUFFIX_STRICT    1u
#endif

/* ================= 内部派生配置（别改） ================= */
#define BTNAME_PREFIX_LEN       3u   /* "BT_" */

#if (BTNAME_TOTAL_MAX_LEN <= BTNAME_PREFIX_LEN)
#error "BTNAME_TOTAL_MAX_LEN must be > 3"
#endif

#define BTNAME_SUFFIX_MAX_LEN   (BTNAME_TOTAL_MAX_LEN - BTNAME_PREFIX_LEN)

/* ================= API ================= */

/* 上电调用：从 Flash 读回名字（suffix），应用到 BLE 广播/扫描响应 */
void btname_init(void);

/* 获取当前最终名字（以 '\0' 结尾），形如 "BT_xxx" */
const char* btname_get(void);

/* 在 Modbus 写保持寄存器(0x10)回调里调用：客户写 suffix
 * 返回 1 表示已处理，0 表示不是本模块的地址范围
 */
int btname_modbus_on_write_holding(uint16_t addr, uint16_t qty, const uint16_t *regs);

#endif /* _BTNAME_MODBUS_H_ */